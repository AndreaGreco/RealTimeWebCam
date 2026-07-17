#include "FfmpegPreviewPlayer.h"
#include "Logger.h"

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
	_hwnd = hwnd;
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

	FfmpegRtspSource::FrameSink sink =
		[this](const uint8_t* y, int sy, const uint8_t* uv, int suv, uint32_t fw, uint32_t fh)
		{
			OnFrame(y, sy, uv, suv, fw, fh);
		};

	if (!_source.Start(_url, w, h, _hasStreamInfo ? _streamInfo.fpsNum : 30,
	                   _hasStreamInfo ? _streamInfo.fpsDen : 1, sink))
	{
		DebugLog("FfmpegPreviewPlayer::play - failed to start decode core");
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

	// Safe to release renderer resources now: the only thread that touched them is gone.
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

// Decode thread: convert the NV12 frame to BGRA and blit it into the panel,
// double-buffered so there is no flicker and letterboxed to keep aspect ratio.
void FfmpegPreviewPlayer::OnFrame(const uint8_t* y, int strideY, const uint8_t* uv, int strideUV,
                                  uint32_t w, uint32_t h)
{
	LARGE_INTEGER freq{}, t0{}, t1{};
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&t0);

	_received.fetch_add(1);

	if (w == 0 || h == 0)
		return;

	// NV12 -> BGRA (no resize here; the GDI stretch fits it to the panel).
	_swsBgra = sws_getCachedContext(_swsBgra,
		(int)w, (int)h, AV_PIX_FMT_NV12,
		(int)w, (int)h, AV_PIX_FMT_BGRA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!_swsBgra)
		return;

	const int dstStride = (int)w * 4;
	_bgra.resize((size_t)dstStride * h);

	const uint8_t* srcData[4] = { y, uv, nullptr, nullptr };
	int srcStride[4] = { strideY, strideUV, 0, 0 };
	uint8_t* dstData[4] = { _bgra.data(), nullptr, nullptr, nullptr };
	int dstLines[4] = { dstStride, 0, 0, 0 };
	sws_scale(_swsBgra, srcData, srcStride, 0, (int)h, dstData, dstLines);

	if (!_hwnd || !IsWindow(_hwnd))
		return;

	RECT rc{};
	GetClientRect(_hwnd, &rc);
	const int cw = rc.right - rc.left;
	const int ch = rc.bottom - rc.top;
	if (cw <= 0 || ch <= 0)
		return;

	HDC winDC = GetDC(_hwnd);
	if (!winDC)
		return;

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
				SetStretchBltMode(_memDC, HALFTONE);
				SetBrushOrgEx(_memDC, 0, 0, nullptr);
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

	ReleaseDC(_hwnd, winDC);

	QueryPerformanceCounter(&t1);
	_lastRenderMs.store(freq.QuadPart > 0
		? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart
		: 0.0);
}
