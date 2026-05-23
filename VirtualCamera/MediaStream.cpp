#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "MediaStream.h"
#include "MediaSource.h"
#include "RtspReaderCallback.h"
#include <mfreadwrite.h>
#include <mfapi.h>

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

	// Build initial descriptor using the configured dimensions (defaults 1920x1080 until
	// SetVideoConfig is called from SetupCameraSettings with the probe-selected values).
	RETURN_IF_FAILED(BuildDescriptor());

	return S_OK;
}

// Builds or rebuilds _descriptor using the current _videoWidth/_videoHeight/_hintFpsNum/_hintFpsDen/_format.
HRESULT MediaStream::BuildDescriptor()
{
	auto types = wil::make_unique_cotaskmem_array<wil::com_ptr_nothrow<IMFMediaType>>(2);

	// NV12 preferred (hardware-native for H.264 decode).
	wil::com_ptr_nothrow<IMFMediaType> nv12Type;
	RETURN_IF_FAILED(MFCreateMediaType(&nv12Type));
	nv12Type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	nv12Type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	nv12Type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	nv12Type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	MFSetAttributeSize(nv12Type.get(), MF_MT_FRAME_SIZE, _videoWidth, _videoHeight);
	nv12Type->SetUINT32(MF_MT_DEFAULT_STRIDE, _videoWidth);
	MFSetAttributeRatio(nv12Type.get(), MF_MT_FRAME_RATE, _hintFpsNum, _hintFpsDen);
	nv12Type->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)(_videoWidth * _videoHeight * 12 * _hintFpsNum / _hintFpsDen / 8));
	MFSetAttributeRatio(nv12Type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	types[0] = nv12Type.detach();

	// RGB32 as fallback.
	wil::com_ptr_nothrow<IMFMediaType> rgbType;
	RETURN_IF_FAILED(MFCreateMediaType(&rgbType));
	rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	MFSetAttributeSize(rgbType.get(), MF_MT_FRAME_SIZE, _videoWidth, _videoHeight);
	rgbType->SetUINT32(MF_MT_DEFAULT_STRIDE, _videoWidth * 4);
	rgbType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	rgbType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	MFSetAttributeRatio(rgbType.get(), MF_MT_FRAME_RATE, _hintFpsNum, _hintFpsDen);
	rgbType->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)(_videoWidth * _videoHeight * 4 * 8 * _hintFpsNum / _hintFpsDen));
	MFSetAttributeRatio(rgbType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	types[1] = rgbType.detach();

	wil::com_ptr_nothrow<IMFStreamDescriptor> newDesc;
	RETURN_IF_FAILED_MSG(MFCreateStreamDescriptor(_index, (DWORD)types.size(), types.get(), &newDesc),
		"BuildDescriptor: MFCreateStreamDescriptor failed");

	wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
	RETURN_IF_FAILED(newDesc->GetMediaTypeHandler(&handler));
	RETURN_IF_FAILED(handler->SetCurrentMediaType(types[0]));

	_descriptor = std::move(newDesc);
	WINTRACE(L"MediaStream::BuildDescriptor - %ux%u @%u/%u", _videoWidth, _videoHeight, _hintFpsNum, _hintFpsDen);
	return S_OK;
}

HRESULT MediaStream::InitializeRTSPReader()
{
	if (_rtspUrl.empty())
	{
		WINTRACE(L"MediaStream::InitializeRTSPReader - No RTSP URL configured");
		return S_FALSE;
	}

	// Already running — don't open a second reader (called again from SetStreamState→Start).
	if (_rtspCallback)
	{
		WINTRACE(L"MediaStream::InitializeRTSPReader - already active, skipping");
		return S_OK;
	}

	WINTRACE(L"MediaStream::InitializeRTSPReader - Opening: %s", _rtspUrl.c_str());

	// Create callback first, then pass it to the source reader.
	wil::com_ptr_nothrow<RtspReaderCallback> callback;
	callback.attach(new (std::nothrow) RtspReaderCallback());
	RETURN_HR_IF_NULL(E_OUTOFMEMORY, callback);

	// Reader attributes: async callback, advanced video processing (handles H.264 decode +
	// format conversion + resolution scaling), low latency.
	// NOTE: use ADVANCED_VIDEO_PROCESSING only — combining it with basic VIDEO_PROCESSING
	// triggers validation failures on some Windows MF builds.
	wil::com_ptr_nothrow<IMFAttributes> readerAttrs;
	RETURN_IF_FAILED(MFCreateAttributes(&readerAttrs, 4));
	RETURN_IF_FAILED(readerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
	RETURN_IF_FAILED(readerAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE));
	RETURN_IF_FAILED(readerAttrs->SetUINT32(MF_LOW_LATENCY, TRUE));
	RETURN_IF_FAILED(readerAttrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.get()));

	wil::com_ptr_nothrow<IMFSourceReader> reader;
	HRESULT hr = MFCreateSourceReaderFromURL(_rtspUrl.c_str(), readerAttrs.get(), &reader);
	if (FAILED(hr))
	{
		WINTRACE(L"MediaStream::InitializeRTSPReader - MFCreateSourceReaderFromURL failed: 0x%08X", hr);
		return hr;
	}

	// Get native resolution from the first available video type (for logging only).
	// Do NOT overwrite _videoWidth/_videoHeight here: they are already set to the
	// consumer-negotiated dimensions (from Start()). The MF video processor will scale
	// the RTSP output to those dimensions automatically.
	wil::com_ptr_nothrow<IMFMediaType> nativeType;
	RETURN_IF_FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType));
	UINT32 nativeW = 0, nativeH = 0;
	MFGetAttributeSize(nativeType.get(), MF_MT_FRAME_SIZE, &nativeW, &nativeH);
	WINTRACE(L"MediaStream::InitializeRTSPReader - Native: %ux%u, output (consumer): %ux%u", nativeW, nativeH, _videoWidth, _videoHeight);

	// Configure source reader output: format + probe-selected resolution.
	// ADVANCED_VIDEO_PROCESSING inserts a full video processor (EVR-quality) that handles
	// H.264 decode + format conversion + scaling to _videoWidth×_videoHeight.
	// Output is packed (stride = width), so CopyRtspFrame uses _videoWidth directly.
	wil::com_ptr_nothrow<IMFMediaType> outputType;
	RETURN_IF_FAILED(MFCreateMediaType(&outputType));
	RETURN_IF_FAILED(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_IF_FAILED(outputType->SetGUID(MF_MT_SUBTYPE, _format == GUID_NULL ? MFVideoFormat_NV12 : _format));
	MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, _videoWidth, _videoHeight);
	RETURN_IF_FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.get()));

	WINTRACE(L"MediaStream::InitializeRTSPReader - output configured: %s %ux%u (native RTSP: %ux%u)",
		GUID_ToStringW(_format == GUID_NULL ? MFVideoFormat_NV12 : _format).c_str(),
		_videoWidth, _videoHeight, nativeW, nativeH);

	// Hand the reader to the callback and start the async chain.
	RETURN_IF_FAILED(callback->BeginRead(reader.get()));
	_rtspCallback = std::move(callback);

	WINTRACE(L"MediaStream::InitializeRTSPReader - async chain started");
	return S_OK;
}

// Copies a decoded RTSP frame
// The source reader is configured to produce exactly the consumer-negotiated format/size,
// so no scaling or stride calculation is needed — same pattern as VideoReaderCbk.
HRESULT MediaStream::CopyRtspFrame(IMFSample* rtspSample, IMFSample* targetSample)
{
	RETURN_HR_IF_NULL(E_POINTER, rtspSample);
	RETURN_HR_IF_NULL(E_POINTER, targetSample);

	// Source: flat contiguous buffer from the source reader (packed NV12 or RGB32).
	wil::com_ptr_nothrow<IMFMediaBuffer> srcBuffer;
	RETURN_IF_FAILED(rtspSample->ConvertToContiguousBuffer(&srcBuffer));
	BYTE* pbSrc = nullptr;
	DWORD cbSrc = 0;
	RETURN_IF_FAILED(srcBuffer->Lock(&pbSrc, nullptr, &cbSrc));

	// Verify the buffer size matches. InitializeRTSPReader already read back the actual
	// negotiated type and updated _videoWidth/_videoHeight, so mismatch here should not
	// happen. Log and skip the frame if it does (avoid overread / green stripes).
	DWORD cbExpected = (_format == MFVideoFormat_NV12)
		? (_videoWidth * _videoHeight * 3 / 2)
		: (_videoWidth * _videoHeight * 4);
	if (cbSrc < cbExpected)
	{
		WINTRACE(L"MediaStream::CopyRtspFrame - unexpected size mismatch: cbSrc=%u expected=%u "
				 L"(%ux%u %s) — skipping frame",
			cbSrc, cbExpected, _videoWidth, _videoHeight,
			(_format == MFVideoFormat_NV12) ? L"NV12" : L"RGB32");
		srcBuffer->Unlock();
		return S_FALSE; // caller will fall through to synthetic frame
	}

	// Destination: D3D texture via IMF2DBuffer (from allocator). Lock2D gives us the
	// real pitch of the GPU texture without guessing.
	wil::com_ptr_nothrow<IMFMediaBuffer> dstBuffer;
	RETURN_IF_FAILED_MSG(targetSample->GetBufferByIndex(0, &dstBuffer), "GetBufferByIndex");

	BYTE* pbScan0 = nullptr;
	LONG  lPitch  = 0;
	HRESULT hrCopy = S_OK;

	wil::com_ptr_nothrow<IMF2DBuffer> dst2D;
	if (SUCCEEDED(dstBuffer->QueryInterface(&dst2D)))
	{
		hrCopy = dst2D->Lock2D(&pbScan0, &lPitch);
		if (SUCCEEDED(hrCopy))
		{
			if (_format == MFVideoFormat_NV12)
				{
					WINTRACE("NV12 frame: src cb=%u, dst pitch=%d, %ux%u",
						cbSrc, lPitch, _videoWidth, _videoHeight);
					// Y plane: _videoHeight rows of _videoWidth bytes each.
					MFCopyImage(pbScan0, lPitch,
						pbSrc, (LONG)_videoWidth,
						_videoWidth, _videoHeight);
					// UV plane: half height. Src offset = Y plane size.
					MFCopyImage(pbScan0 + lPitch * (LONG)_videoHeight, lPitch,
						pbSrc  + _videoWidth * _videoHeight, (LONG)_videoWidth,
						_videoWidth, _videoHeight / 2);
				}
				else // RGB32
				{
					DWORD cbRow = _videoWidth * 4;
					MFCopyImage(pbScan0, lPitch, pbSrc, (LONG)cbRow, cbRow, _videoHeight);
					WINTRACE("RGB32 frame: src cb=%u, dst pitch=%d, %ux%u",
						cbSrc, lPitch, _videoWidth, _videoHeight);
				}
			dst2D->Unlock2D();
			// Always report the full allocated size (_videoHeight, not effectiveHeight):
			// the allocator owns the full texture; the consumer expects the declared size.
			DWORD cbFull = (_format == MFVideoFormat_NV12)
				? (_videoWidth * _videoHeight * 3 / 2)
				: (_videoWidth * _videoHeight * 4);
			dstBuffer->SetCurrentLength(cbFull);
		}
	}
	else
	{
		// System-memory fallback (no D3D — rare).
		WINTRACE("SYS memory frame: src cb=%u, %ux%u", cbSrc, _videoWidth, _videoHeight);
		BYTE* pbDst = nullptr; DWORD cbDstMax = 0;
		hrCopy = dstBuffer->Lock(&pbDst, &cbDstMax, nullptr);
		if (SUCCEEDED(hrCopy))
		{
			DWORD cb = min(cbSrc, cbDstMax);
			CopyMemory(pbDst, pbSrc, cb);
			dstBuffer->Unlock();
			dstBuffer->SetCurrentLength(cb);
		}
	}

	srcBuffer->Unlock();

	if (FAILED(hrCopy))
	{
		WINTRACE(L"MediaStream::CopyRtspFrame - copy failed: 0x%08X", hrCopy);
		return hrCopy;
	}

	LONGLONG ts = 0; rtspSample->GetSampleTime(&ts);
	WINTRACE(L"MediaStream::CopyRtspFrame - OK ts:%lld pitch:%d %ux%u", ts, lPitch, _videoWidth, _videoHeight);
	return S_OK;
}

HRESULT MediaStream::Start(IMFMediaType* type)
{
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_allocator);

	// _videoWidth/_videoHeight/_format are authoritative: set from the probe result
	// via SetVideoConfig() before Start() is called. We do not infer size from
	// the negotiated media type — the descriptor was already built at probe dimensions.
	if (_format == GUID_NULL) _format = MFVideoFormat_NV12;

	WINTRACE(L"MediaStream::Start - using probe config: %s %ux%u @%u/%u",
		GUID_ToStringW(_format).c_str(), _videoWidth, _videoHeight, _hintFpsNum, _hintFpsDen);

	if (_rtspCallback)
	{
		GUID targetFmt = (_format == GUID_NULL) ? MFVideoFormat_NV12 : _format;
		LOG_IF_FAILED(_rtspCallback->SetOutputMediaType(targetFmt, _videoWidth, _videoHeight));
		WINTRACE(L"MediaStream::Start - RTSP already connected, reconfigured output to %ux%u",
			_videoWidth, _videoHeight);
	}
	else if (!_rtspInitPending.exchange(true))
	{
		if (!_rtspUrl.empty())
		{
			AddRef();
			HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD
			{
				auto* stream = static_cast<MediaStream*>(param);
				HRESULT hr = stream->InitializeRTSPReader();
				if (FAILED(hr))
					WINTRACE(L"MediaStream - RTSP init failed: 0x%08X", hr);
				stream->_rtspInitPending = false;
				stream->Release();
				return 0;
			}, this, 0, nullptr);
			if (hThread) CloseHandle(hThread);
			else { _rtspInitPending = false; Release(); }
		}
		else
		{
			_rtspInitPending = false;
			WINTRACE(L"MediaStream::Start - No RTSP URL, synthetic frames only");
		}
	}
	else
	{
		WINTRACE(L"MediaStream::Start - RTSP init already in progress");
	}

	if (!_frameGenerator.HasD3DManager())
	{
		LOG_IF_FAILED(_frameGenerator.EnsureRenderTarget(_videoWidth, _videoHeight));
	}

	RETURN_IF_FAILED(_allocator->InitializeSampleAllocator(3, type)); // 3 = low latency (decode-hold-deliver)
	RETURN_IF_FAILED(_queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr));

	_state = MF_STREAM_STATE_RUNNING;
	return S_OK;
}

HRESULT MediaStream::Stop()
{
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_allocator);

	// Clean up RTSP async reader
	if (_rtspCallback)
	{
		_rtspCallback->Shutdown();
		_rtspCallback.reset();
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
	RETURN_HR_IF_NULL(E_POINTER, manager);

	_allocator->SetDirectXManager(manager);
	_dxgiManager = manager;  // Store for later use with color converter
	LOG_IF_FAILED(_frameGenerator.SetD3DManager(manager, _videoWidth, _videoHeight));
	return S_OK;
}

void MediaStream::SetRTSPUrl(std::wstring url)
{
	_rtspUrl = url;
	WINTRACE(L"MediaStream::SetRTSPUrl - URL stored: %s", url.c_str());
	// Thread is started from Start() once we know the consumer-negotiated type.
}

HRESULT MediaStream::SetVideoConfig(UINT32 width, UINT32 height, UINT32 fpsNum, UINT32 fpsDen, GUID format)
{
	if (width  > 0) _videoWidth  = width;
	if (height > 0) _videoHeight = height;
	if (fpsNum > 0) _hintFpsNum  = fpsNum;
	if (fpsDen > 0) _hintFpsDen  = fpsDen;
	if (format != GUID_NULL) _format = format;
	WINTRACE(L"MediaStream::SetVideoConfig - %ux%u @%u/%u format=%s",
		_videoWidth, _videoHeight, _hintFpsNum, _hintFpsDen,
		GUID_ToStringW(_format).c_str());
	return BuildDescriptor();
}

void MediaStream::Shutdown()
{
	if (_queue)
	{
		LOG_IF_FAILED_MSG(_queue->Shutdown(), "Queue shutdown failed");
		_queue.reset();
	}

	if (_rtspCallback)
	{
		_rtspCallback->Shutdown();
		_rtspCallback.reset();
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
	WINTRACE(L"MediaSource::GetMediaSource");
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
	RETURN_IF_FAILED(_allocator->AllocateSample(&sample));
	RETURN_IF_FAILED(sample->SetSampleTime(MFGetSystemTime()));
	RETURN_IF_FAILED(sample->SetSampleDuration(333333));

	wil::com_ptr_nothrow<IMFSample> outSample;

	// Try to deliver a real RTSP frame (non-blocking — callback stores latest decoded frame).
	if (_rtspCallback)
	{
		wil::com_ptr_nothrow<IMFSample> rtspSample;
		HRESULT hrTake = _rtspCallback->TakeLatestSample(&rtspSample);
		if (hrTake == S_OK && rtspSample)
		{
			HRESULT hrCopy = CopyRtspFrame(rtspSample.get(), sample.get());
			if (SUCCEEDED(hrCopy))
			{
				if(!sample) {
					WINTRACE(L"MediaStream::RequestSample - CopyRtspFrame failed, no output sample, using synthetic");
				} else {
					WINTRACE(L"MediaStream::RequestSample - Delivered RTSP frame");
					outSample = sample;
				}
			}
			else
			{
				WINTRACE(L"MediaStream::RequestSample - CopyRtspFrame failed, using synthetic");
			}
		}
		// hrTake == S_FALSE: pipeline warming up, fall through to synthetic
	}else {
		WINTRACE(L"MediaStream::RequestSample - No RTSP callback, using synthetic");
	}

	if (!outSample)
	{
		// No RTSP frame yet (or no RTSP at all) — deliver synthetic frame.
		IMFSample* rawOut = nullptr;
		if (SUCCEEDED(_frameGenerator.Generate(sample.get(), _format, &rawOut)) && rawOut)
			outSample.attach(rawOut);
		else
			outSample = sample;
	}

	if (pToken)
	{
		RETURN_IF_FAILED(outSample->SetUnknown(MFSampleExtension_Token, pToken));
	}
	RETURN_IF_FAILED(_queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, outSample.get()));
	return S_OK;
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
		RETURN_IF_FAILED(Start(nullptr));
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
