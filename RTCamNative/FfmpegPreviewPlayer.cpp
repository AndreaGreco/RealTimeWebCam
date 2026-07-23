#include "FfmpegPreviewPlayer.h"
#include "Logger.h"
#include <cstring> // memcpy

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

FfmpegPreviewPlayer::~FfmpegPreviewPlayer()
{
	stop();
}

void FfmpegPreviewPlayer::setVideoPath(LPCWSTR path)
{
	_url = (path != nullptr) ? path : L"";
	_hasStreamInfo = false;
}

void FfmpegPreviewPlayer::setWindowHandle(HWND hwnd)
{
	_hwnd.store(hwnd);
}

int FfmpegPreviewPlayer::initialize()
{
	if (_url.empty())
	{
		DebugLog("FfmpegPreviewPlayer::initialize - no source configured");
		return -1;
	}

	FfmpegProbeInfo probe = FfmpegRtspSource::Probe(_url);
	if (!probe.valid)
	{
		DebugLog("FfmpegPreviewPlayer::initialize - probe failed");
		return -2;
	}

	_streamInfo = StreamInfo{};
	_streamInfo.width = probe.width;
	_streamInfo.height = probe.height;
	_streamInfo.fpsNum = probe.fpsNum;
	_streamInfo.fpsDen = probe.fpsDen;
	_streamInfo.subtype = GUID_NULL; // cosmetic: the Frame Server always outputs NV12
	_streamInfo.bitrate = 0;
	_hasStreamInfo = true;

	// Cache the human-readable connection characteristics for the UI table.
	_connInfo = ConnectionInfo{};
	_connInfo.bitrate = probe.bitrate;
	_connInfo.width = probe.width;
	_connInfo.height = probe.height;
	_connInfo.fpsNum = probe.fpsNum;
	_connInfo.fpsDen = probe.fpsDen;
	_connInfo.valid = 1;
	memcpy(_connInfo.container, probe.container, sizeof(_connInfo.container));
	memcpy(_connInfo.transport, probe.transport, sizeof(_connInfo.transport));
	memcpy(_connInfo.videoCodec, probe.videoCodec, sizeof(_connInfo.videoCodec));
	memcpy(_connInfo.profile, probe.profile, sizeof(_connInfo.profile));
	memcpy(_connInfo.pixelFormat, probe.pixelFormat, sizeof(_connInfo.pixelFormat));
	return 0;
}

int FfmpegPreviewPlayer::play()
{
	if (_url.empty())
		return -1;
	if (_playing.load())
		return 0;

	// Decode at the source's native size when known; the GDI blit fits it to the panel.
	uint32_t w = (_hasStreamInfo && _streamInfo.width > 0) ? _streamInfo.width : 1920;
	uint32_t h = (_hasStreamInfo && _streamInfo.height > 0) ? _streamInfo.height : 1080;

	_received.store(0);
	_rendered.store(0);
	_lastRenderMs.store(0.0);

	// Start the render thread first so it is ready to consume frames the moment the
	// decode core begins delivering them.
	_renderStop.store(false);
	{
		std::lock_guard<std::mutex> lk(_frameMutex);
		_frameDirty = false;
	}
	_renderThread = std::thread(&FfmpegPreviewPlayer::RenderLoop, this);

	FfmpegRtspSource::FrameSink sink =
		[this](const uint8_t* y, int sy, const uint8_t* uv, int suv, uint32_t fw, uint32_t fh)
		{
			OnFrame(y, sy, uv, suv, fw, fh);
		};

	if (!_source.Start(_url, w, h, _hasStreamInfo ? _streamInfo.fpsNum : 30,
	                   _hasStreamInfo ? _streamInfo.fpsDen : 1, sink))
	{
		DebugLog("FfmpegPreviewPlayer::play - failed to start decode core");
		// Unwind the render thread we just started.
		_renderStop.store(true);
		_frameCv.notify_all();
		if (_renderThread.joinable())
			_renderThread.join();
		return -2;
	}

	_playing.store(true);
	return 0;
}

int FfmpegPreviewPlayer::pause()
{
	// The preview has no clock to pause; MainForm never calls this in the live flow.
	return 0;
}

int FfmpegPreviewPlayer::stop()
{
	_source.Stop();       // joins the decode thread — no more OnFrame after this
	_playing.store(false);

	// Then stop the render thread (it may be blocked waiting for a frame).
	_renderStop.store(true);
	_frameCv.notify_all();
	if (_renderThread.joinable())
		_renderThread.join();

	// Safe to release renderer resources now: both threads that could touch them
	// (decode -> OnFrame, render -> RenderLoop) are joined.
	ReleaseBackBuffer();
	if (_swsBgra)
	{
		sws_freeContext(_swsBgra);
		_swsBgra = nullptr;
	}
	return 0;
}

int FfmpegPreviewPlayer::getVideoStreamInfo(StreamInfo* pInfo, UINT32 maxCount, UINT32* count)
{
	if (!pInfo || !count)
		return E_INVALIDARG;

	if (_hasStreamInfo && maxCount >= 1)
	{
		pInfo[0] = _streamInfo;
		*count = 1;
	}
	else
	{
		*count = 0;
	}
	return S_OK;
}

int FfmpegPreviewPlayer::getPreviewStats(PreviewStats* pStats)
{
	if (!pStats)
		return E_INVALIDARG;

	pStats->receivedFrames = _received.load();
	pStats->renderedFrames = _rendered.load();
	pStats->droppedFrames = 0;
	pStats->driftMs = (double)_source.LastLagMs();
	pStats->lastRenderMs = _lastRenderMs.load();
	return S_OK;
}

int FfmpegPreviewPlayer::getConnectionInfo(ConnectionInfo* pInfo)
{
	if (!pInfo)
		return E_INVALIDARG;

	*pInfo = _connInfo; // valid==0 until initialize() has probed the source
	return S_OK;
}

void FfmpegPreviewPlayer::ReleaseBackBuffer()
{
	if (_memDC)
	{
		if (_oldBmp)
			SelectObject(_memDC, _oldBmp);
		DeleteDC(_memDC);
		_memDC = nullptr;
	}
	if (_memBmp)
	{
		DeleteObject(_memBmp);
		_memBmp = nullptr;
	}
	_oldBmp = nullptr;
	_bbW = 0;
	_bbH = 0;
}

// Decode thread: pack the latest NV12 frame tightly (stride == width) into the
// staging buffer and wake the render thread. Cheap — a couple of memcpys, no
// convert/blit — so the decode loop is never throttled by rendering.
void FfmpegPreviewPlayer::OnFrame(const uint8_t* y, int strideY, const uint8_t* uv, int strideUV,
                                  uint32_t w, uint32_t h)
{
	_received.fetch_add(1);

	if (w == 0 || h == 0 || !y || !uv)
		return;

	const size_t ySize = (size_t)w * h;
	const size_t uvSize = (size_t)w * (h / 2); // NV12: h/2 rows of w interleaved UV bytes

	{
		std::lock_guard<std::mutex> lk(_frameMutex);
		_stagingNv12.resize(ySize + uvSize);
		uint8_t* dstY = _stagingNv12.data();
		uint8_t* dstUV = dstY + ySize;
		// Drop the source's alignment padding: destination stride == width.
		for (uint32_t r = 0; r < h; ++r)
			memcpy(dstY + (size_t)r * w, y + (size_t)r * strideY, w);
		for (uint32_t r = 0; r < h / 2; ++r)
			memcpy(dstUV + (size_t)r * w, uv + (size_t)r * strideUV, w);
		_stgW = w;
		_stgH = h;
		_frameDirty = true;
	}
	_frameCv.notify_one();
}

// Render thread: wait for a staged frame, convert NV12 -> BGRA, and blit it into
// the panel (double-buffered, letterboxed). Decoupled from decoding, so the
// convert+blit cost does not cap the receive rate. Latest-wins: if this thread is
// slower than the decoder, intermediate frames are simply overwritten in staging.
void FfmpegPreviewPlayer::RenderLoop()
{
	LARGE_INTEGER freq{};
	QueryPerformanceFrequency(&freq);

	for (;;)
	{
		uint32_t w = 0, h = 0;
		{
			std::unique_lock<std::mutex> lk(_frameMutex);
			_frameCv.wait(lk, [this] { return _frameDirty || _renderStop.load(); });
			if (_renderStop.load())
				break;
			_renderNv12.swap(_stagingNv12); // O(1): take the latest staged frame
			w = _stgW;
			h = _stgH;
			_frameDirty = false;
		}
		if (w == 0 || h == 0 ||
			_renderNv12.size() < (size_t)w * h + (size_t)w * (h / 2))
			continue;

		LARGE_INTEGER t0{}, t1{};
		QueryPerformanceCounter(&t0);

		// NV12 (tight, stride == width) -> BGRA.
		_swsBgra = sws_getCachedContext(_swsBgra,
			(int)w, (int)h, AV_PIX_FMT_NV12,
			(int)w, (int)h, AV_PIX_FMT_BGRA,
			SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (!_swsBgra)
			continue;

		const uint8_t* srcY = _renderNv12.data();
		const uint8_t* srcUV = srcY + (size_t)w * h;
		const int dstStride = (int)w * 4;
		_bgra.resize((size_t)dstStride * h);

		const uint8_t* srcData[4] = { srcY, srcUV, nullptr, nullptr };
		int srcStride[4] = { (int)w, (int)w, 0, 0 };
		uint8_t* dstData[4] = { _bgra.data(), nullptr, nullptr, nullptr };
		int dstLines[4] = { dstStride, 0, 0, 0 };
		sws_scale(_swsBgra, srcData, srcStride, 0, (int)h, dstData, dstLines);

		HWND hwnd = _hwnd.load();
		if (!hwnd || !IsWindow(hwnd))
			continue;

		RECT rc{};
		GetClientRect(hwnd, &rc);
		const int cw = rc.right - rc.left;
		const int ch = rc.bottom - rc.top;
		if (cw <= 0 || ch <= 0)
			continue;

		HDC winDC = GetDC(hwnd);
		if (!winDC)
			continue;

		// (Re)create the back buffer if the panel size changed.
		if (!_memDC || _bbW != cw || _bbH != ch)
		{
			ReleaseBackBuffer();
			_memDC = CreateCompatibleDC(winDC);
			if (_memDC)
			{
				_memBmp = CreateCompatibleBitmap(winDC, cw, ch);
				if (_memBmp)
				{
					_oldBmp = SelectObject(_memDC, _memBmp);
					_bbW = cw;
					_bbH = ch;
					SetStretchBltMode(_memDC, COLORONCOLOR); // fast stretch (decode is decoupled now)
				}
				else
				{
					DeleteDC(_memDC);
					_memDC = nullptr;
				}
			}
		}

		if (_memDC && _memBmp)
		{
			// Black background, then the frame fit into it preserving aspect ratio.
			RECT full{ 0, 0, cw, ch };
			FillRect(_memDC, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));

			const double scale = min((double)cw / (double)w, (double)ch / (double)h);
			const int dw = (int)(w * scale);
			const int dh = (int)(h * scale);
			const int dx = (cw - dw) / 2;
			const int dy = (ch - dh) / 2;

			BITMAPINFO bmi{};
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = (LONG)w;
			bmi.bmiHeader.biHeight = -(LONG)h; // top-down
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;

			StretchDIBits(_memDC, dx, dy, dw, dh, 0, 0, (int)w, (int)h,
				_bgra.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

			BitBlt(winDC, 0, 0, cw, ch, _memDC, 0, 0, SRCCOPY);
			_rendered.fetch_add(1);
		}

		ReleaseDC(hwnd, winDC);

		QueryPerformanceCounter(&t1);
		_lastRenderMs.store(freq.QuadPart > 0
			? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart
			: 0.0);
	}
}
