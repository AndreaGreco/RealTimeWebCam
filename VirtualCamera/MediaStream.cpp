#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "MediaStream.h"
#include "MediaSource.h"
#include "RtspReaderCallback.h"
#include <mfreadwrite.h>

#define NUM_IMAGE_COLS 1280
#define NUM_IMAGE_ROWS 960

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

	// Create media types - support RGB32 and NV12
	auto types = wil::make_unique_cotaskmem_array<wil::com_ptr_nothrow<IMFMediaType>>(2);

	wil::com_ptr_nothrow<IMFMediaType> rgbType;
	RETURN_IF_FAILED(MFCreateMediaType(&rgbType));
	rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	MFSetAttributeSize(rgbType.get(), MF_MT_FRAME_SIZE, NUM_IMAGE_COLS, NUM_IMAGE_ROWS);
	rgbType->SetUINT32(MF_MT_DEFAULT_STRIDE, NUM_IMAGE_COLS * 4);
	rgbType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	rgbType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	MFSetAttributeRatio(rgbType.get(), MF_MT_FRAME_RATE, 30, 1);
	auto bitrate = (uint32_t)(NUM_IMAGE_COLS * NUM_IMAGE_ROWS * 4 * 8 * 30);
	rgbType->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
	MFSetAttributeRatio(rgbType.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	types[0] = rgbType.detach();

	wil::com_ptr_nothrow<IMFMediaType> nv12Type;
	RETURN_IF_FAILED(MFCreateMediaType(&nv12Type));
	nv12Type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	nv12Type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	nv12Type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	nv12Type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	MFSetAttributeSize(nv12Type.get(), MF_MT_FRAME_SIZE, NUM_IMAGE_COLS, NUM_IMAGE_ROWS);
	nv12Type->SetUINT32(MF_MT_DEFAULT_STRIDE, (UINT)(NUM_IMAGE_COLS * 1.5));
	MFSetAttributeRatio(nv12Type.get(), MF_MT_FRAME_RATE, 30, 1);
	bitrate = (uint32_t)(NUM_IMAGE_COLS * 1.5 * NUM_IMAGE_ROWS * 8 * 30);
	nv12Type->SetUINT32(MF_MT_AVG_BITRATE, bitrate);
	MFSetAttributeRatio(nv12Type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	types[1] = nv12Type.detach();

	RETURN_IF_FAILED_MSG(MFCreateStreamDescriptor(_index, (DWORD)types.size(), types.get(), &_descriptor), "MFCreateStreamDescriptor failed");

	wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
	RETURN_IF_FAILED(_descriptor->GetMediaTypeHandler(&handler));
	TraceMFAttributes(handler.get(), L"MediaTypeHandler");
	RETURN_IF_FAILED(handler->SetCurrentMediaType(types[0]));

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

	// Reader attributes: async callback, video processing, low latency.
	// NOTE: do NOT set MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING at all (leave at default FALSE).
	// Setting it explicitly — even to FALSE — alongside MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING=TRUE
	// triggers validation in some Windows MF builds and can cause MFCreateSourceReaderFromURL to fail.
	wil::com_ptr_nothrow<IMFAttributes> readerAttrs;
	RETURN_IF_FAILED(MFCreateAttributes(&readerAttrs, 4));
	RETURN_IF_FAILED(readerAttrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
	RETURN_IF_FAILED(readerAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE));
	RETURN_IF_FAILED(readerAttrs->SetUINT32(MF_LOW_LATENCY, TRUE));
	RETURN_IF_FAILED(readerAttrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.get()));

	wil::com_ptr_nothrow<IMFSourceReader> reader;
	HRESULT hr = MFCreateSourceReaderFromURL(_rtspUrl.c_str(), readerAttrs.get(), &reader);
	if (FAILED(hr))
	{
		WINTRACE(L"MediaStream::InitializeRTSPReader - MFCreateSourceReaderFromURL failed: 0x%08X", hr);
		return hr;
	}

	// Get native resolution from the first available video type.
	wil::com_ptr_nothrow<IMFMediaType> nativeType;
	RETURN_IF_FAILED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType));
	MFGetAttributeSize(nativeType.get(), MF_MT_FRAME_SIZE, &_videoWidth, &_videoHeight);
	UINT32 nativeW = _videoWidth, nativeH = _videoHeight;
	WINTRACE(L"MediaStream::InitializeRTSPReader - Native: %ux%u", nativeW, nativeH);

	// Ask the source reader to decode to _format at the virtual camera's declared resolution.
	// MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING=TRUE makes MF insert a video processor that
	// scales + converts H.264 → _format @ 1280×960 automatically.
	// This is mandatory: the consumer has already negotiated 1280×960 from the stream
	// descriptor, so every sample we deliver MUST have that exact resolution.
	wil::com_ptr_nothrow<IMFMediaType> outputType;
	RETURN_IF_FAILED(MFCreateMediaType(&outputType));
	RETURN_IF_FAILED(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_IF_FAILED(outputType->SetGUID(MF_MT_SUBTYPE, _format == GUID_NULL ? MFVideoFormat_RGB32 : _format));
	MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, NUM_IMAGE_COLS, NUM_IMAGE_ROWS);
	RETURN_IF_FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.get()));

	// Read back actual negotiated output type to get true stride.
	wil::com_ptr_nothrow<IMFMediaType> actualType;
	RETURN_IF_FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType));
	{
		UINT32 w = 0, h = 0;
		MFGetAttributeSize(actualType.get(), MF_MT_FRAME_SIZE, &w, &h);
		_videoWidth  = (w > 0) ? w : NUM_IMAGE_COLS;
		_videoHeight = (h > 0) ? h : NUM_IMAGE_ROWS;

		// MFGetStrideForBitmapInfoHeader is the reliable way to get stride for any format.
		// MF_MT_DEFAULT_STRIDE is unreliable for planar formats (NV12) and may be absent.
		GUID subtype = GUID_NULL;
		actualType->GetGUID(MF_MT_SUBTYPE, &subtype);
		LONG computedStride = 0;
		if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(subtype.Data1, _videoWidth, &computedStride)) && computedStride > 0)
			_videoStride = (UINT32)computedStride;
		else
			_videoStride = _videoWidth * 4; // RGB32 safe fallback

		WINTRACE(L"MediaStream::InitializeRTSPReader - Output: %ux%u stride:%u (native was %ux%u)",
			_videoWidth, _videoHeight, _videoStride, nativeW, nativeH);
	}

	// Hand the reader to the callback and start the async chain.
	RETURN_IF_FAILED(callback->BeginRead(reader.get()));
	_rtspCallback = std::move(callback);

	WINTRACE(L"MediaStream::InitializeRTSPReader - async chain started");
	return S_OK;
}

// Copies a decoded RTSP frame (any format, native resolution) into an allocator sample.
// Source format is _format (set by InitializeRTSPReader to match what Zoom requested).
// Handles both RGB32 and NV12 destinations (system memory or D3D IMF2DBuffer textures).
HRESULT MediaStream::CopyRtspFrame(IMFSample* rtspSample, IMFSample* targetSample)
{
	RETURN_HR_IF_NULL(E_POINTER, rtspSample);
	RETURN_HR_IF_NULL(E_POINTER, targetSample);

	// Flatten RTSP sample to a contiguous system-memory buffer.
	wil::com_ptr_nothrow<IMFMediaBuffer> srcBuffer;
	RETURN_IF_FAILED(rtspSample->ConvertToContiguousBuffer(&srcBuffer));

	BYTE* srcData = nullptr;
	DWORD srcLen  = 0;
	RETURN_IF_FAILED(srcBuffer->Lock(&srcData, nullptr, &srcLen));

	// Destination: prefer IMF2DBuffer2 (D3D textures from allocator), fall back to flat lock.
	wil::com_ptr_nothrow<IMFMediaBuffer> dstBuffer;
	RETURN_IF_FAILED_MSG(targetSample->GetBufferByIndex(0, &dstBuffer),
		"CopyRtspFrame: GetBufferByIndex failed");

	wil::com_ptr_nothrow<IMF2DBuffer2> dst2D;
	dstBuffer->QueryInterface(&dst2D);

	HRESULT hrCopy = S_OK;
	if (dst2D)
	{
		BYTE* dstScan0 = nullptr;
		LONG  dstPitch = 0;
		BYTE* dstBuf   = nullptr;
		DWORD dstBufLen = 0;
		hrCopy = dst2D->Lock2DSize(MF2DBuffer_LockFlags_Write, &dstScan0, &dstPitch, &dstBuf, &dstBufLen);
		if (SUCCEEDED(hrCopy))
		{
			if (_format == MFVideoFormat_NV12)
			{
				// NV12: Y plane (full res) then UV plane (half height, interleaved).
				// Source stride for NV12 = _videoStride (= _videoWidth for packed NV12,
				// may be larger if the decoder aligns rows).
				// UV plane starts at srcData + _videoStride * _videoHeight (NOT _videoWidth * _videoHeight).
				MFCopyImage(dstScan0, dstPitch,
					srcData, (LONG)_videoStride,
					_videoWidth, _videoHeight);
				MFCopyImage(dstScan0 + dstPitch * (LONG)_videoHeight, dstPitch,
					srcData + _videoStride * _videoHeight, (LONG)_videoStride,
					_videoWidth, _videoHeight / 2);
			}
			else
			{
				// RGB32 / other: single plane.
				MFCopyImage(dstScan0, dstPitch, srcData, (LONG)_videoStride, _videoWidth * 4, _videoHeight);
			}
			dst2D->Unlock2D();
			// SetCurrentLength: use actual locked buffer size (covers all planes including UV).
			dstBuffer->SetCurrentLength(dstBufLen);
		}
	}
	else
	{
		// System-memory fallback (no D3D).
		BYTE* dstData   = nullptr;
		DWORD dstMaxLen = 0;
		hrCopy = dstBuffer->Lock(&dstData, &dstMaxLen, nullptr);
		if (SUCCEEDED(hrCopy))
		{
			DWORD copyBytes = min(srcLen, dstMaxLen);
			CopyMemory(dstData, srcData, copyBytes);
			dstBuffer->Unlock();
			dstBuffer->SetCurrentLength(copyBytes);
		}
	}

	srcBuffer->Unlock();

	if (FAILED(hrCopy))
	{
		WINTRACE(L"MediaStream::CopyRtspFrame - copy failed: 0x%08X", hrCopy);
		return hrCopy;
	}

	// Do NOT copy RTSP stream timestamp: the virtual camera pipeline expects
	// monotonically increasing presentation timestamps. When we repeat the same
	// decoded frame (RTSP fps < pipeline fps), the RTSP ts is constant → MF drops
	// or glitches. RequestSample already set MFGetSystemTime() on targetSample.
	LONGLONG ts = 0;
	rtspSample->GetSampleTime(&ts);
	WINTRACE(L"MediaStream::CopyRtspFrame - OK rtspTs:%lld %ux%u fmt:%s",
		ts, _videoWidth, _videoHeight, GUID_ToStringW(_format).c_str());
	return S_OK;
}

HRESULT MediaStream::Start(IMFMediaType* type)
{
	// ── Passo 5b del ciclo di vita ──────────────────────────────────────────
	// Chiamato da MediaSource::Start() quando un'app apre la virtual camera.
	// MediaSource::Start() chiama SetRTSPUrl() PRIMA di questa funzione,
	// MA solo se ha ricevuto l'URL in Initialize() — il che NON accade ancora
	// (BUG 3: AddProperty non mappa a IMFAttributes → _rtspUrl sempre vuoto).
	// Finché BUG 3 non è risolto, InitializeRTSPReader() restituisce S_FALSE
	// e vengono usati i frame sintetici FrameGenerator.
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_allocator);

	if (type)
	{
		RETURN_IF_FAILED(type->GetGUID(MF_MT_SUBTYPE, &_format));
		WINTRACE(L"MediaStream::Start format: %s", GUID_ToStringW(_format).c_str());
	}

	// Initialize RTSP async reader if URL is configured.
	HRESULT hr = InitializeRTSPReader();
	if (hr == S_FALSE)
	{
		WINTRACE(L"MediaStream::Start - No RTSP URL, using synthetic frames");
	}
	else if (FAILED(hr))
	{
		WINTRACE(L"MediaStream::Start - RTSP init failed: 0x%08X, using synthetic frames", hr);
	}

	// Allocator: always use the consumer-negotiated type (resolution fixed by stream descriptor).
	// We must deliver exactly what was negotiated — changing resolution requires MEStreamFormatChanged.
	// The source reader is configured to scale RTSP frames to 1280x960 automatically.

	// Fallback frame generator (synthetic frames, low cost to initialize).
	if (!_frameGenerator.HasD3DManager())
	{
		LOG_IF_FAILED(_frameGenerator.EnsureRenderTarget(NUM_IMAGE_COLS, NUM_IMAGE_ROWS));
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
	LOG_IF_FAILED(_frameGenerator.SetD3DManager(manager, NUM_IMAGE_COLS, NUM_IMAGE_ROWS));
	return S_OK;
}

void MediaStream::SetRTSPUrl(std::wstring url)
{
	_rtspUrl = url;
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
