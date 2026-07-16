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

	_state = MF_STREAM_STATE_RUNNING;
	return S_OK;
}

HRESULT MediaStream::Stop()
{
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_allocator);

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
	return SetVideoConfig(
		context.config.width,
		context.config.height,
		context.config.fpsNum,
		context.config.fpsDen,
		context.config.format);
}

void MediaStream::Shutdown()
{
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

STDMETHODIMP MediaStream::RequestSample(IUnknown* pToken)
{
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_allocator || !_queue);

	wil::com_ptr_nothrow<IMFSample> sample;
	HRESULT hr = _allocator->AllocateSample(&sample);
	if (hr == MF_E_SAMPLEALLOCATOR_EMPTY)
	{
		WINTRACE(L"MediaStream::RequestSample - sample allocator empty, outstanding requests still in flight");
		return hr;
	}
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
		}
	}

	if (!outSample)
	{
		// Reachable before the first frame ever arrives or when the producer's
		// heartbeat is stale (app closed / not decoding yet). Don't block — show the
		// synthetic frame so a disconnected camera doesn't look like a silent stall.
		IMFSample* rawOut = nullptr;
		if (SUCCEEDED(_frameGenerator.Generate(sample.get(), _format, &rawOut)) && rawOut)
			outSample.attach(rawOut);
		else
			outSample = sample;
	}

	PublishStats();

	if (pToken)
	{
		RETURN_IF_FAILED(outSample->SetUnknown(MFSampleExtension_Token, pToken));
	}
	RETURN_IF_FAILED(_queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, outSample.get()));
	return S_OK;
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
