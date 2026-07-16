#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "MediaStream.h"
#include "VCamMediaSource.h"
#include "StatsPublisher.h"
#include "FrameChannelReader.h"
#include <mfreadwrite.h>
#include <mfapi.h>

HRESULT MediaStream::BuildFrameTypeNV12(wil::com_ptr_nothrow<IMFMediaType>& nv12Type)
{
	HRESULT hr;

	hr = MFCreateMediaType(&nv12Type);
	RETURN_IF_FAILED(hr);

	hr = nv12Type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	RETURN_IF_FAILED(hr);

	hr = nv12Type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	RETURN_IF_FAILED(hr);
	this->_format = MFVideoFormat_NV12;

	// Progressive video
	hr = nv12Type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	RETURN_IF_FAILED(hr);

	// Webcam-like stream
	hr = nv12Type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	RETURN_IF_FAILED(hr);

	hr = nv12Type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE);
	RETURN_IF_FAILED(hr);

	// --------------------------------------------------------------------
	// Frame geometry
	// --------------------------------------------------------------------
	hr = MFSetAttributeSize(nv12Type.get(), MF_MT_FRAME_SIZE, _videoWidth, _videoHeight);
	RETURN_IF_FAILED(hr);

	hr = MFSetAttributeRatio(nv12Type.get(), MF_MT_FRAME_RATE, _hintFpsNum, _hintFpsDen);
	RETURN_IF_FAILED(hr);

	hr = MFSetAttributeRatio(nv12Type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	RETURN_IF_FAILED(hr);

	// --------------------------------------------------------------------
	// Sample size
	// NV12 = width * height * 3 / 2
	// --------------------------------------------------------------------
	const UINT32 sampleSize = (_videoWidth * _videoHeight * 3) / 2;
	hr = nv12Type->SetUINT32(MF_MT_SAMPLE_SIZE, sampleSize);
	RETURN_IF_FAILED(hr);

	// --------------------------------------------------------------------
	// Optional bitrate hint
	//
	// Raw NV12 webcam streams do not really use bitrate,
	// but some consumers expect a reasonable value.
	// --------------------------------------------------------------------
	const UINT32 approxBitrate = _videoWidth * _videoHeight * 12 * _hintFpsNum / max(1u, _hintFpsDen);
	hr = nv12Type->SetUINT32(MF_MT_AVG_BITRATE, approxBitrate);
	RETURN_IF_FAILED(hr);

	// --------------------------------------------------------------------
// IMPORTANT:
// DO NOT set MF_MT_DEFAULT_STRIDE manually.
//
// Real stride depends on:
// - decoder
// - GPU alignment
// - D3D texture allocation
//
// Width != stride in general.
// --------------------------------------------------------------------
	WINTRACE(
		L"BuildDescriptor - advertised: "
		L"NV12 %ux%u @ %u/%u sample=%u",
		_videoWidth,
		_videoHeight,
		_hintFpsNum,
		_hintFpsDen,
		sampleSize);
	return S_OK;
}

// Builds or rebuilds _descriptor using the current _videoWidth/_videoHeight/_hintFpsNum/_hintFpsDen/_format.
HRESULT MediaStream::BuildDescriptor()
{
	wil::com_ptr_nothrow<IMFStreamDescriptor> newDesc;
	wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
	wil::com_ptr_nothrow<IMFMediaType> nv12Type;
	IMFMediaType* rawType;
	HRESULT hr;

	hr = BuildFrameTypeNV12(nv12Type);
	RETURN_IF_FAILED(hr);

	rawType = nv12Type.get();
	hr = MFCreateStreamDescriptor(_index, 1, &rawType, &newDesc);
	RETURN_IF_FAILED_MSG(hr, "MFCreateStreamDescriptor failed");

	hr = newDesc->GetMediaTypeHandler(&handler);
	RETURN_IF_FAILED(hr);

	hr = handler->SetCurrentMediaType(nv12Type.get());
	RETURN_IF_FAILED(hr);

	_descriptor = std::move(newDesc);
	return S_OK;
}

HRESULT MediaStream::Initialize(IMFMediaSource* source, int index)
{
	RETURN_HR_IF_NULL(E_POINTER, source);
	_source = source;
	_index = index;

	RETURN_IF_FAILED(SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE));
	RETURN_IF_FAILED(SetUINT32(MF_DEVICESTREAM_STREAM_ID, index));
	RETURN_IF_FAILED(SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1));
	RETURN_IF_FAILED(SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes::MFFrameSourceTypes_Color));

	RETURN_IF_FAILED(MFCreateEventQueue(&_queue));

	RETURN_IF_FAILED(BuildDescriptor());

	return S_OK;
}

// Copies a contiguous NV12 source (Y plane of `height` rows at `srcPitch`, then the
// interleaved UV plane of `height/2` rows at `srcPitch`, starting at src + srcPitch*height)
// into the destination buffer, honoring the dest's real pitch. The source is the
// shared-memory slot the app producer filled (system memory), so the copy is CPU.
HRESULT MediaStream::CopyNv12ToSample(IMFMediaBuffer* dstBuffer, const BYTE* src, LONG srcPitch, UINT32 width, UINT32 height)
{
	RETURN_HR_IF_NULL(E_POINTER, dstBuffer);
	RETURN_HR_IF_NULL(E_POINTER, src);
	const DWORD cbExpected = width * height * 3 / 2; // NV12

	// Destination: D3D texture via IMF2DBuffer gives the real GPU pitch; else flat copy.
	HRESULT hrCopy = S_OK;
	BYTE* pbScan0 = nullptr;
	LONG  dstPitch = 0;
	wil::com_ptr_nothrow<IMF2DBuffer> dst2D;
	if (SUCCEEDED(dstBuffer->QueryInterface(IID_PPV_ARGS(&dst2D))))
	{
		hrCopy = dst2D->Lock2D(&pbScan0, &dstPitch);
		if (SUCCEEDED(hrCopy))
		{
			// Y plane: height rows of width bytes.
			MFCopyImage(pbScan0, dstPitch, src, srcPitch, width, height);
			// UV plane: half height. Src UV follows the Y plane at srcPitch * height.
			MFCopyImage(pbScan0 + dstPitch * (LONG)height, dstPitch,
				src + srcPitch * (LONG)height, srcPitch,
				width, height / 2);
			dst2D->Unlock2D();
			dstBuffer->SetCurrentLength(cbExpected);
		}
	}
	else
	{
		BYTE* pbDst = nullptr; DWORD cbDstMax = 0;
		hrCopy = dstBuffer->Lock(&pbDst, &cbDstMax, nullptr);
		if (SUCCEEDED(hrCopy))
		{
			DWORD cb = min(cbExpected, cbDstMax);
			MFCopyImage(pbDst, (LONG)width, src, srcPitch, width, height);
			MFCopyImage(pbDst + width * height, (LONG)width,
				src + srcPitch * (LONG)height, srcPitch,
				width, height / 2);
			dstBuffer->Unlock();
			dstBuffer->SetCurrentLength(cb);
		}
	}
	return hrCopy;
}

namespace
{
	// Compact 5x7 bitmap font for digits 0-9. Each row is the low 5 bits, MSB = leftmost.
	static const uint8_t kDigitFont[10][7] = {
		{ 0x0E,0x11,0x13,0x15,0x19,0x11,0x0E }, // 0
		{ 0x04,0x0C,0x04,0x04,0x04,0x04,0x0E }, // 1
		{ 0x0E,0x11,0x01,0x02,0x04,0x08,0x1F }, // 2
		{ 0x1F,0x02,0x04,0x02,0x01,0x11,0x0E }, // 3
		{ 0x02,0x06,0x0A,0x12,0x1F,0x02,0x02 }, // 4
		{ 0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E }, // 5
		{ 0x06,0x08,0x10,0x1E,0x11,0x11,0x0E }, // 6
		{ 0x1F,0x01,0x02,0x04,0x08,0x08,0x08 }, // 7
		{ 0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E }, // 8
		{ 0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C }, // 9
	};

	// Draws `text` (digits only) into an NV12 frame: a dark box (Y=16, UV neutral) with
	// white glyphs (Y=235), top-left at (x0,y0). yPlane/uvPlane are the locked NV12
	// planes; uvPlane = yPlane + pitch*height (contiguous NV12).
	void DrawDigitsNv12(BYTE* yPlane, BYTE* uvPlane, LONG pitch, UINT32 w, UINT32 h,
	                    const char* text, int x0, int y0, int scale)
	{
		const int cols = 5, rows = 7, sp = 1;
		int len = 0; for (const char* c = text; *c; ++c) ++len;
		const int boxW = len * (cols + sp) * scale + 2 * scale;
		const int boxH = rows * scale + 2 * scale;

		// Background box: Y = 16 (near-black), UV = 128 (neutral) so it's pure grayscale.
		for (int yy = 0; yy < boxH; ++yy)
		{
			int py = y0 + yy; if (py < 0 || py >= (int)h) continue;
			for (int xx = 0; xx < boxW; ++xx)
			{
				int px = x0 + xx; if (px < 0 || px >= (int)w) continue;
				yPlane[(LONG)py * pitch + px] = 16;
			}
		}
		for (int yy = 0; yy < boxH; yy += 2)
		{
			int py = y0 + yy; if (py < 0 || py >= (int)h) continue;
			int uvRow = py / 2;
			for (int xx = 0; xx < boxW; xx += 2)
			{
				int px = x0 + xx; if (px < 0 || px >= (int)w) continue;
				int uvx = (px / 2) * 2;
				uvPlane[(LONG)uvRow * pitch + uvx] = 128;
				uvPlane[(LONG)uvRow * pitch + uvx + 1] = 128;
			}
		}

		// Glyphs: Y = 235 (white).
		int cx = x0 + scale;
		for (const char* c = text; *c; ++c)
		{
			if (*c >= '0' && *c <= '9')
			{
				const uint8_t* g = kDigitFont[*c - '0'];
				for (int r = 0; r < rows; ++r)
				{
					uint8_t bits = g[r];
					for (int col = 0; col < cols; ++col)
					{
						if (!(bits & (1 << (cols - 1 - col)))) continue;
						for (int sy = 0; sy < scale; ++sy)
							for (int sx = 0; sx < scale; ++sx)
							{
								int px = cx + col * scale + sx;
								int py = y0 + scale + r * scale + sy;
								if (px >= 0 && px < (int)w && py >= 0 && py < (int)h)
									yPlane[(LONG)py * pitch + px] = 235;
							}
					}
				}
			}
			cx += (cols + sp) * scale;
		}
	}
}

// Burns the delivery counter into a real NV12 sample's Y plane (best-effort). The
// synthetic FrameGenerator path draws its own counter with Direct2D, so this only
// needs to handle the shared-memory frames.
void MediaStream::DrawOverlayCounter(IMFSample* sample, UINT64 value)
{
	if (!sample) return;
	wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
	if (FAILED(sample->GetBufferByIndex(0, &buffer)) || !buffer) return;

	wil::com_ptr_nothrow<IMF2DBuffer> buffer2D;
	if (FAILED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2D)))) return;

	BYTE* scan0 = nullptr; LONG pitch = 0;
	if (FAILED(buffer2D->Lock2D(&scan0, &pitch)) || !scan0) return;

	char text[32];
	snprintf(text, sizeof(text), "%llu", (unsigned long long)value);
	// UV plane immediately follows the Y plane in NV12 (same pitch).
	BYTE* uv = scan0 + (LONG)pitch * _videoHeight;
	const int scale = max(2u, _videoHeight / 90); // ~ readable regardless of resolution
	DrawDigitsNv12(scan0, uv, pitch, _videoWidth, _videoHeight, text, 16, 16, scale);

	buffer2D->Unlock2D();
}

// Pull the latest NV12 frame the app (FFmpeg) published into the frame shared
// memory (FrameChannelReader) and copy it into targetSample. S_FALSE when there is
// no fresh frame yet (producer not started, heartbeat stale, or geometry mismatch),
// so RequestSample falls back to the synthetic frame.
HRESULT MediaStream::CopyFrameChannelFrame(IMFSample* targetSample)
{
	RETURN_HR_IF_NULL(E_POINTER, targetSample);

	const uint8_t* slot = nullptr;
	uint32_t w = 0, h = 0, st = 0;
	uint64_t fseq = 0, fwritten = 0, hb = 0;
	_ffmpegFresh = false;

	if (!FrameChannelReader::Instance().AcquireLatest(&slot, &w, &h, &st, &fseq, &fwritten, &hb) || !slot)
		return S_FALSE;

	// Freshness: the producer stamps GetTickCount64() (system-wide, comparable across
	// processes) on every write; treat >2s without an update as "producer gone".
	const ULONGLONG now = GetTickCount64();
	const bool fresh = (hb != 0) && (now >= hb) && (now - hb <= 2000);
	_frameChannelRxFrames = fwritten;
	if (!fresh || w != _videoWidth || h != _videoHeight)
		return S_FALSE;

	wil::com_ptr_nothrow<IMFMediaBuffer> dstBuffer;
	RETURN_IF_FAILED(targetSample->GetBufferByIndex(0, &dstBuffer));
	RETURN_IF_FAILED(CopyNv12ToSample(dstBuffer.get(), slot, (LONG)st, w, h));

	_ffmpegFresh = true;
	// Duplicate detection for stats: same frameSeq as last delivery == a re-serve.
	if (_hasDeliveredFrame && fseq == _lastDeliveredFrameSeq)
		_declinedFrameCount++;
	_lastDeliveredFrameSeq = fseq;
	return S_OK;
}

HRESULT MediaStream::Start(IMFMediaType* type)
{
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_allocator);
	RETURN_HR_IF_NULL(E_POINTER, type);
	_currentType = type;

	WINTRACE(L"MediaStream::Start - using descriptor config: %s %ux%u @%u/%u",
		MFVideoFormatToString(_format).c_str(), _videoWidth, _videoHeight, _hintFpsNum, _hintFpsDen);

	if (!_frameGenerator.HasD3DManager())
	{
		LOG_IF_FAILED(_frameGenerator.EnsureRenderTarget(_videoWidth, _videoHeight));
	}

	constexpr DWORD kAllocatorSampleCount = 8;
	RETURN_IF_FAILED(_allocator->InitializeSampleAllocator(kAllocatorSampleCount, type));
	RETURN_IF_FAILED(_queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr));

	// Async delivery pacing: a periodic threadpool timer makes one frame due per frame
	// interval; DispatchSamples pairs that with a pending RequestSample. Start clean and
	// allow the first sample immediately so the stream doesn't stutter on startup.
	{
		winrt::slim_lock_guard lock(_lock);
		_requests.clear();
		_frameDue = true;
		if (!_deliveryTimer)
			_deliveryTimer = CreateThreadpoolTimer(&MediaStream::DeliveryTimerThunk, this, nullptr);
	}
	if (_deliveryTimer)
	{
		const DWORD periodMs = max(1u, (1000u * _hintFpsDen) / max(1u, _hintFpsNum));
		LARGE_INTEGER rel; rel.QuadPart = -(LONGLONG)periodMs * 10000LL; // relative, 100ns units
		FILETIME due{ rel.LowPart, (DWORD)rel.HighPart };
		SetThreadpoolTimer(_deliveryTimer, &due, periodMs, 0);
	}

	_state = MF_STREAM_STATE_RUNNING;
	return S_OK;
}

HRESULT MediaStream::Stop()
{
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_allocator);

	// Disarm the pacing timer and wait out any in-flight callback BEFORE taking _lock
	// (the callback takes _lock — waiting while holding it would deadlock).
	if (_deliveryTimer)
	{
		SetThreadpoolTimer(_deliveryTimer, nullptr, 0, 0);
		WaitForThreadpoolTimerCallbacks(_deliveryTimer, TRUE);
	}
	{
		winrt::slim_lock_guard lock(_lock);
		_requests.clear();
		_frameDue = false;
	}

	RETURN_IF_FAILED(_allocator->UninitializeSampleAllocator());
	RETURN_IF_FAILED(_queue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr));
	_state = MF_STREAM_STATE_STOPPED;
	return S_OK;
}

MFSampleAllocatorUsage MediaStream::GetAllocatorUsage()
{
	return MFSampleAllocatorUsage_UsesProvidedAllocator;
}

HRESULT MediaStream::SetAllocator(IUnknown* allocator)
{
	RETURN_HR_IF_NULL(E_POINTER, allocator);
	_allocator.reset();
	RETURN_HR(allocator->QueryInterface(&_allocator));
}

HRESULT MediaStream::SetD3DManager(IUnknown* manager)
{
	WINTRACE(L"MediaStream::SetD3DManager manager:%p", manager);
	RETURN_HR_IF_NULL(E_POINTER, manager);

	// The Frame Server hands us its D3D11 device manager for the output sample
	// allocator (it may allocate the consumer's samples as GPU textures). We no
	// longer keep it for a source-side GPU copy — frames now arrive as system-memory
	// NV12 from the app producer, so the copy is always CPU.
	_allocator->SetDirectXManager(manager);
	LOG_IF_FAILED(_frameGenerator.SetD3DManager(manager, _videoWidth, _videoHeight));
	return S_OK;
}

HRESULT MediaStream::SetVideoConfig(UINT32 width, UINT32 height, UINT32 fpsNum, UINT32 fpsDen, GUID format)
{
	if (width > 0) _videoWidth = width;
	if (height > 0) _videoHeight = height;
	if (fpsNum > 0) _hintFpsNum = fpsNum;
	if (fpsDen > 0) _hintFpsDen = fpsDen;
	if (format != GUID_NULL) _format = format;
	WINTRACE(L"MediaStream::SetVideoConfig - %ux%u @%u/%u format=%s",
		_videoWidth, _videoHeight, _hintFpsNum, _hintFpsDen,
		GUID_ToStringW(_format).c_str());
	return BuildDescriptor();
}

HRESULT MediaStream::SetRuntimeContext(const StreamRuntimeContext& context)
{
	_generation = context.config.generation;
	_overlayEnabled = context.config.overlay != 0;
	return SetVideoConfig(
		context.config.width,
		context.config.height,
		context.config.fpsNum,
		context.config.fpsDen,
		context.config.format);
}

void MediaStream::Shutdown()
{
	// Stop the pacing timer and wait out any in-flight callback before tearing down the
	// queue it delivers into. Not under _lock — the callback takes _lock.
	if (_deliveryTimer)
	{
		SetThreadpoolTimer(_deliveryTimer, nullptr, 0, 0);
		WaitForThreadpoolTimerCallbacks(_deliveryTimer, TRUE);
		CloseThreadpoolTimer(_deliveryTimer);
		_deliveryTimer = nullptr;
	}

	{
		winrt::slim_lock_guard lock(_lock);
		_requests.clear();
		_frameDue = false;
	}

	if (_queue)
	{
		LOG_IF_FAILED_MSG(_queue->Shutdown(), "Queue shutdown failed");
		_queue.reset();
	}

	_descriptor.reset();
	_source.reset();
	_attributes.reset();
}

// IMFMediaEventGenerator
STDMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState)
{
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->BeginGetEvent(pCallback, punkState));
	return S_OK;
}

STDMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent)
{
	RETURN_HR_IF_NULL(E_POINTER, ppEvent);
	*ppEvent = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->EndGetEvent(pResult, ppEvent));
	return S_OK;
}

STDMETHODIMP MediaStream::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent)
{
	WINTRACE(L"MediaStream::GetEvent");
	RETURN_HR_IF_NULL(E_POINTER, ppEvent);
	*ppEvent = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->GetEvent(dwFlags, ppEvent));
	return S_OK;
}

STDMETHODIMP MediaStream::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue)
{
	WINTRACE(L"MediaStream::QueueEvent");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue));
	return S_OK;
}

// IMFMediaStream
STDMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** ppMediaSource)
{
	WINTRACE(L"MediaStream::GetMediaSource");
	RETURN_HR_IF_NULL(E_POINTER, ppMediaSource);
	*ppMediaSource = nullptr;
	RETURN_HR_IF(MF_E_SHUTDOWN, !_source);

	RETURN_IF_FAILED(_source.copy_to(ppMediaSource));
	return S_OK;
}

STDMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** ppStreamDescriptor)
{
	WINTRACE(L"MediaStream::GetStreamDescriptor");
	RETURN_HR_IF_NULL(E_POINTER, ppStreamDescriptor);
	*ppStreamDescriptor = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_descriptor);

	RETURN_IF_FAILED(_descriptor.copy_to(ppStreamDescriptor));
	return S_OK;
}

// IMFMediaStream::RequestSample — the Frame Server's "give me a frame" call. Per the MS
// custom-media-source model we do NOT produce or block here: the token is queued and we
// return immediately, so a work-queue thread is never held (deadlock-safe). Actual
// delivery is paced by the threadpool timer (see DispatchSamples/OnDeliveryTick), which
// is what stops the fast NV12 copy path from free-running to hundreds of samples/sec.
STDMETHODIMP MediaStream::RequestSample(IUnknown* pToken)
{
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_allocator || !_queue);

	// Construction from the raw pointer AddRefs (and stores null when pToken is null).
	_requests.emplace_back(pToken);

	// Deliver right away if the timer already made a frame due; otherwise the request
	// waits in the queue until the next tick.
	DispatchSamples();
	return S_OK;
}

// Drains as many (frame-due AND pending-request) pairs as available, producing exactly one
// sample per due credit. _frameDue is a single-credit bool, so ticks that fire while no
// request is outstanding don't accumulate into a later burst. Runs under _lock (called
// from RequestSample and OnDeliveryTick, both already holding it).
void MediaStream::DispatchSamples()
{
	while (_frameDue && !_requests.empty())
	{
		IUnknown* token = _requests.front().get();
		HRESULT hr = ProduceAndQueue(token);
		if (hr == MF_E_SAMPLEALLOCATOR_EMPTY)
		{
			// All output samples are still in flight downstream. Keep the request and
			// the due credit; retry on the next tick.
			WINTRACE(L"MediaStream::DispatchSamples - allocator empty, retrying next tick");
			break;
		}
		// Delivered (or dropped on a hard error): consume the request and the credit.
		_requests.pop_front();
		_frameDue = false;
	}
}

// Allocates one output sample, fills it (FFmpeg frame-channel copy, else synthetic
// fallback), applies the diagnostic overlay, publishes stats, and queues the
// MEMediaSample event that completes one request. Runs under _lock.
HRESULT MediaStream::ProduceAndQueue(IUnknown* pToken)
{
	wil::com_ptr_nothrow<IMFSample> sample;
	HRESULT hr = _allocator->AllocateSample(&sample);
	if (hr == MF_E_SAMPLEALLOCATOR_EMPTY)
		return hr; // handled by the caller (retry next tick)
	RETURN_IF_FAILED(hr);

	RETURN_IF_FAILED(sample->SetSampleTime(MFGetSystemTime()));
	RETURN_IF_FAILED(sample->SetSampleDuration((10000000LL * _hintFpsDen) / max(1u, _hintFpsNum)));

	wil::com_ptr_nothrow<IMFSample> outSample;

	// The frame comes from the app (FFmpeg) via shared memory; the copy is always
	// CPU (system-memory NV12 slot). CopyFrameChannelFrame returns S_OK only when a
	// fresh frame is available — otherwise we fall through to the synthetic frame.
	{
		LARGE_INTEGER freq{}, t0{}, t1{};
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&t0);
		HRESULT hrCopy = CopyFrameChannelFrame(sample.get());
		QueryPerformanceCounter(&t1);
		if (hrCopy == S_OK)
		{
			outSample = sample;
			_renderedFrameCount++;
			_hasDeliveredFrame = true;
			_lastCopyMs = freq.QuadPart > 0
				? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart
				: 0.0;
			if (_overlayEnabled)
				DrawOverlayCounter(sample.get(), _overlayCounter);
		}
	}

	if (!outSample)
	{
		// Reachable before the first frame ever arrives or when the producer's
		// heartbeat is stale (app closed / not decoding yet). Show the synthetic frame
		// so a disconnected camera doesn't look like a silent stall. The synthetic path
		// draws the overlay counter itself (Direct2D).
		IMFSample* rawOut = nullptr;
		if (SUCCEEDED(_frameGenerator.Generate(sample.get(), _format, &rawOut, _overlayEnabled, _overlayCounter)) && rawOut)
			outSample.attach(rawOut);
		else
			outSample = sample;
	}

	if (_overlayEnabled)
		_overlayCounter++; // one increment per delivery → advances at the "render" rate

	PublishStats();

	if (pToken)
	{
		RETURN_IF_FAILED(outSample->SetUnknown(MFSampleExtension_Token, pToken));
	}
	RETURN_IF_FAILED(_queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, outSample.get()));
	return S_OK;
}

// Periodic threadpool-timer callback: makes one frame due and delivers it if a request
// is already waiting. Takes _lock (like RequestSample), so Stop()/Shutdown() must NOT
// hold _lock while draining callbacks — they don't.
void MediaStream::OnDeliveryTick()
{
	winrt::slim_lock_guard lock(_lock);
	if (_state != MF_STREAM_STATE_RUNNING || !_queue)
		return;
	_frameDue = true;
	DispatchSamples();
}

void CALLBACK MediaStream::DeliveryTimerThunk(PTP_CALLBACK_INSTANCE, void* ctx, PTP_TIMER)
{
	static_cast<MediaStream*>(ctx)->OnDeliveryTick();
}

// Assembles the current snapshot (whatever RequestSample just did) and hands it
// to StatsPublisher. Runs every RequestSample tick (~30x/sec, the Frame
// Server's natural cadence) so the app-side UI always has fresh numbers without
// needing a dedicated timer thread here.
void MediaStream::PublishStats()
{
	VCamFrameServerStats snapshot{};

	// The app (FFmpeg) is the producer: derive state from whether its heartbeat is
	// fresh, and rx from the producer's own frame counter. The Frame Server's own
	// copy is always CPU (system-memory NV12 slot), so the hw-accel flags — which
	// tracked the old in-process GPU zero-copy path — are always 0 here; the real
	// decode-on-GPU signal lives app-side (VCam_IsFfmpegProducerHardware).
	snapshot.sessionState = static_cast<uint32_t>(
		_ffmpegFresh ? VCamSessionStateWire::Running : VCamSessionStateWire::Starting);
	snapshot.rxFrames = _frameChannelRxFrames;
	snapshot.droppedFrames = 0;
	snapshot.renderedFrames = _renderedFrameCount;
	snapshot.declinedFrames = _declinedFrameCount;
	snapshot.driftMs = 0.0; // pacing/latency is managed app-side by the FFmpeg producer
	snapshot.lastCopyMs = _lastCopyMs;
	snapshot.width = _videoWidth;
	snapshot.height = _videoHeight;
	snapshot.fpsNum = _hintFpsNum;
	snapshot.fpsDen = _hintFpsDen;
	snapshot.hwAccelCapable = 0u;
	snapshot.hwAccelActive = 0u;

	StatsPublisher::Instance().Publish(snapshot);
}

// IMFMediaStream2
STDMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE value)
{
	WINTRACE(L"MediaStream::SetStreamState current:%u value:%u", _state, value);
	if (_state == value)
		return S_OK;
	switch (value)
	{
	case MF_STREAM_STATE_PAUSED:
		if (_state != MF_STREAM_STATE_RUNNING)
			RETURN_HR(MF_E_INVALID_STATE_TRANSITION);

		_state = value;
		break;

	case MF_STREAM_STATE_RUNNING:
		RETURN_IF_FAILED(Start(_currentType.get()));
		break;

	case MF_STREAM_STATE_STOPPED:
		RETURN_IF_FAILED(Stop());
		break;

	default:
		RETURN_HR(MF_E_INVALID_STATE_TRANSITION);
		break;
	}
	return S_OK;
}

STDMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* value)
{
	WINTRACE(L"MediaStream::GetStreamState state:%u", _state);
	RETURN_HR_IF_NULL(E_POINTER, value);
	*value = _state;
	return S_OK;
}

// IKsControl
STDMETHODIMP_(NTSTATUS) MediaStream::KsProperty(PKSPROPERTY property, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaStream::KsProperty len:%u data:%p dataLength:%u", length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, property);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaStream::KsProperty prop:%s", PKSIDENTIFIER_ToString(property, length).c_str());

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP_(NTSTATUS) MediaStream::KsMethod(PKSMETHOD method, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaStream::KsMethod len:%u data:%p dataLength:%u", length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, method);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaStream::KsMethod method:%s", PKSIDENTIFIER_ToString(method, length).c_str());

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP_(NTSTATUS) MediaStream::KsEvent(PKSEVENT evt, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaStream::KsEvent evt:%p len:%u data:%p dataLength:%u", evt, length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaStream::KsEvent event:%s", PKSIDENTIFIER_ToString(evt, length).c_str());
	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
