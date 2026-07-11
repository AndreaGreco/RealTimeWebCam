#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "MediaStream.h"
#include "VCamMediaSource.h"
#include "RtspReaderCallback.h"
#include "StatsPublisher.h"
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

// Copies the latest decoded RTSP frame into the consumer's sample.
// The source reader is configured to produce exactly the consumer-negotiated NV12
// size, so no scaling or stride math is needed. Two paths:
//   - GPU (fast): both buffers are DXGI textures on the shared device → CopySubresourceRegion.
//   - CPU (fallback): source is system memory → a single MFCopyImage into the dest buffer.
HRESULT MediaStream::CopyRtspFrame(const RtspFrameSnapshot& frame, IMFSample* targetSample)
{
	RETURN_HR_IF_NULL(E_POINTER, targetSample);
	RETURN_HR_IF(E_INVALIDARG, !frame.valid || !frame.sample || _videoWidth == 0 || _videoHeight == 0);

	wil::com_ptr_nothrow<IMFMediaBuffer> srcBuffer;
	RETURN_IF_FAILED(frame.sample->GetBufferByIndex(0, &srcBuffer));

	wil::com_ptr_nothrow<IMFMediaBuffer> dstBuffer;
	RETURN_IF_FAILED_MSG(targetSample->GetBufferByIndex(0, &dstBuffer), "GetBufferByIndex");

	// GPU zero-copy path: both sides are DXGI textures on the shared device.
	wil::com_ptr_nothrow<IMFDXGIBuffer> srcDxgi;
	wil::com_ptr_nothrow<IMFDXGIBuffer> dstDxgi;
	if (_deviceManager && _deviceHandle &&
		SUCCEEDED(srcBuffer->QueryInterface(IID_PPV_ARGS(&srcDxgi))) &&
		SUCCEEDED(dstBuffer->QueryInterface(IID_PPV_ARGS(&dstDxgi))))
	{
		HRESULT hrGpu = CopyRtspFrameGpu(srcDxgi.get(), dstDxgi.get());
		if (SUCCEEDED(hrGpu))
		{
			_lastCopyWasGpu = true;
			return S_OK;
		}
		WINTRACE(L"MediaStream::CopyRtspFrame - GPU copy failed 0x%08X, falling back to CPU", hrGpu);
	}

	_lastCopyWasGpu = false;
	return CopyRtspFrameCpu(srcBuffer.get(), dstBuffer.get());
}

// GPU->GPU copy of an NV12 texture. Both textures live on the device owned by the
// Frame Server's device manager, so a same-device CopySubresourceRegion suffices.
HRESULT MediaStream::CopyRtspFrameGpu(IMFDXGIBuffer* srcDxgi, IMFDXGIBuffer* dstDxgi)
{
	RETURN_HR_IF(E_NOT_VALID_STATE, !_deviceManager || !_deviceHandle);

	wil::com_ptr_nothrow<ID3D11Texture2D> srcTex;
	wil::com_ptr_nothrow<ID3D11Texture2D> dstTex;
	UINT srcSub = 0, dstSub = 0;
	RETURN_IF_FAILED(srcDxgi->GetResource(IID_PPV_ARGS(&srcTex)));
	RETURN_IF_FAILED(srcDxgi->GetSubresourceIndex(&srcSub));
	RETURN_IF_FAILED(dstDxgi->GetResource(IID_PPV_ARGS(&dstTex)));
	RETURN_IF_FAILED(dstDxgi->GetSubresourceIndex(&dstSub));

	// LockDevice serializes access to the immediate context shared with the decoder.
	wil::com_ptr_nothrow<ID3D11Device> device;
	HRESULT hr = _deviceManager->LockDevice(_deviceHandle, IID_PPV_ARGS(&device), TRUE);
	if (hr == MF_E_DXGI_NEW_VIDEO_DEVICE)
	{
		// The video device was reset/recreated — reopen the handle and retry once.
		_deviceManager->CloseDeviceHandle(_deviceHandle);
		_deviceHandle = nullptr;
		RETURN_IF_FAILED(_deviceManager->OpenDeviceHandle(&_deviceHandle));
		hr = _deviceManager->LockDevice(_deviceHandle, IID_PPV_ARGS(&device), TRUE);
	}
	RETURN_IF_FAILED(hr);

	// No early returns between LockDevice success and UnlockDevice.
	wil::com_ptr_nothrow<ID3D11DeviceContext> ctx;
	device->GetImmediateContext(&ctx);
	ctx->CopySubresourceRegion(dstTex.get(), dstSub, 0, 0, 0, srcTex.get(), srcSub, nullptr);
	ctx->Flush();

	_deviceManager->UnlockDevice(_deviceHandle, FALSE);
	return S_OK;
}

// Single-copy CPU fallback. Handles a padded source (IMF2DBuffer) without an
// intermediate contiguous buffer, copying straight into the dest with MFCopyImage.
HRESULT MediaStream::CopyRtspFrameCpu(IMFMediaBuffer* srcBuffer, IMFMediaBuffer* dstBuffer)
{
	const UINT32 width = _videoWidth;
	const UINT32 height = _videoHeight;
	const DWORD cbExpected = width * height * 3 / 2; // NV12

	// Source: prefer the 2D view to honor the decoder's real pitch; fall back to a flat lock.
	BYTE* pbSrc = nullptr;
	LONG srcPitch = (LONG)width;
	DWORD cbSrc = 0;
	wil::com_ptr_nothrow<IMF2DBuffer> src2D;
	bool srcLocked2D = false;
	if (SUCCEEDED(srcBuffer->QueryInterface(IID_PPV_ARGS(&src2D))) &&
		SUCCEEDED(src2D->Lock2D(&pbSrc, &srcPitch)))
	{
		srcLocked2D = true;
	}
	else
	{
		RETURN_IF_FAILED(srcBuffer->Lock(&pbSrc, nullptr, &cbSrc));
		if (cbSrc < cbExpected)
		{
			srcBuffer->Unlock();
			WINTRACE(L"MediaStream::CopyRtspFrameCpu - size mismatch cbSrc=%u expected=%u (%ux%u) - skipping",
				cbSrc, cbExpected, width, height);
			return S_FALSE; // caller falls through to synthetic frame
		}
	}

	auto unlockSrc = [&]() {
		if (srcLocked2D) src2D->Unlock2D();
		else srcBuffer->Unlock();
	};

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
			MFCopyImage(pbScan0, dstPitch, pbSrc, srcPitch, width, height);
			// UV plane: half height. Src UV follows the Y plane at srcPitch * height.
			MFCopyImage(pbScan0 + dstPitch * (LONG)height, dstPitch,
				pbSrc + srcPitch * (LONG)height, srcPitch,
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
			MFCopyImage(pbDst, (LONG)width, pbSrc, srcPitch, width, height);
			MFCopyImage(pbDst + width * height, (LONG)width,
				pbSrc + srcPitch * (LONG)height, srcPitch,
				width, height / 2);
			dstBuffer->Unlock();
			dstBuffer->SetCurrentLength(cb);
		}
	}

	unlockSrc();
	return hrCopy;
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

	_allocator->SetDirectXManager(manager);
	_dxgiManager = manager;  // Store for later use with color converter

	// Cache the device manager + an open device handle so RequestSample can do a
	// GPU->GPU CopySubresourceRegion without re-resolving the device every frame.
	if (_deviceManager && _deviceHandle)
	{
		_deviceManager->CloseDeviceHandle(_deviceHandle);
		_deviceHandle = nullptr;
	}
	_deviceManager.reset();
	if (SUCCEEDED(manager->QueryInterface(IID_PPV_ARGS(&_deviceManager))))
	{
		LOG_IF_FAILED(_deviceManager->OpenDeviceHandle(&_deviceHandle));
	}

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
	_rtspManager = context.rtspManager;
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

	if (_deviceManager && _deviceHandle)
	{
		LOG_IF_FAILED(_deviceManager->CloseDeviceHandle(_deviceHandle));
		_deviceHandle = nullptr;
	}
	_deviceManager.reset();

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
	RtspSessionState rtspState = _rtspManager ? _rtspManager->GetState() : RtspSessionState::Stopped;

	if (_rtspManager)
	{
		RtspFrameSnapshot frame;
		HRESULT hrFrame = _rtspManager->TryGetLatestFrame(frame);
		if (hrFrame == S_OK && frame.valid)
		{
			LARGE_INTEGER freq{}, t0{}, t1{};
			QueryPerformanceFrequency(&freq);
			QueryPerformanceCounter(&t0);
			HRESULT hrCopy = CopyRtspFrame(frame, sample.get());
			QueryPerformanceCounter(&t1);
			if (SUCCEEDED(hrCopy))
			{
				outSample = sample;
				_renderedFrameCount++;

				// TakeLatestSample (RtspReaderCallback) intentionally does NOT clear the
				// cached frame after handing it out — it keeps re-serving the last decoded
				// frame so the consumer always gets something even when RTSP runs slower
				// than the consumer asks. We tried declining instead of re-serving
				// (returning MF_E_SAMPLEALLOCATOR_EMPTY when frame.frameSeq matched the
				// last delivery) and it made the whole pipeline unstable in practice —
				// that HRESULT is tied to the allocator's own release-notification
				// mechanism, not a generic "ask me again" signal, so overloading it here
				// broke the Frame Server's retry timing. Back to always delivering
				// something; frame.frameSeq (bumped in OnReadSample only for a genuinely
				// new sample) is kept purely for stats, to tell a fresh frame from a
				// re-serve of the previous one.
				if (_hasDeliveredFrame && frame.frameSeq == _lastDeliveredFrameSeq)
					_declinedFrameCount++; // "declined" in the stats sense: not a new frame
				_lastDeliveredFrameSeq = frame.frameSeq;
				_hasDeliveredFrame = true;

				_lastCopyMs = freq.QuadPart > 0
					? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart
					: 0.0;

				// Drift: wall-clock elapsed vs media-timeline elapsed since the first
				// frame of this session (same formula as the preview path in
				// RTCamNative/VideoReaderCbk.cpp, so the two numbers mean the same
				// thing). Reset the baseline whenever the generation changes (new
				// Start()/reconnect) so a stale baseline from a previous session
				// doesn't leak into the new one.
				if (!_driftBaseSet || frame.generation != _driftGeneration)
				{
					_driftBaseSet = true;
					_driftGeneration = frame.generation;
					_driftBaseTickMs = GetTickCount64();
					_driftBasePtsMs = frame.sampleTime / 10000; // 100ns -> ms
					_driftMs = 0.0;
				}
				else
				{
					ULONGLONG nowTick = GetTickCount64();
					LONGLONG ptsMs = frame.sampleTime / 10000;
					double wallElapsed = (double)(nowTick - _driftBaseTickMs);
					double mediaElapsed = (double)(ptsMs - _driftBasePtsMs);
					_driftMs = wallElapsed - mediaElapsed;
				}
			}
		}
	}

	if (!outSample)
	{
		// Only reachable before the very first RTSP frame has ever arrived, or if
		// CopyRtspFrame itself failed. Same synthetic-frame fallback as
		// Reconnecting/Failed: don't block, just show something so a disconnected
		// camera doesn't look like a silent stall.
		if (rtspState == RtspSessionState::Failed)
			WINTRACE(L"MediaStream::RequestSample - RTSP manager failed, using synthetic frame");

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
	snapshot.sessionState = static_cast<uint32_t>(
		_rtspManager ? _rtspManager->GetState() : RtspSessionState::Stopped);

	UINT64 rx = 0, dropped = 0;
	if (_rtspManager)
		_rtspManager->GetFrameCounters(rx, dropped);
	snapshot.rxFrames = rx;
	snapshot.droppedFrames = dropped;
	snapshot.renderedFrames = _renderedFrameCount;
	snapshot.declinedFrames = _declinedFrameCount;
	snapshot.driftMs = _driftMs;
	snapshot.lastCopyMs = _lastCopyMs;
	snapshot.width = _videoWidth;
	snapshot.height = _videoHeight;
	snapshot.fpsNum = _hintFpsNum;
	snapshot.fpsDen = _hintFpsDen;
	// "Capable" = the Frame Server handed this stream a D3D11 device manager
	// (SetD3DManager), the precondition for the zero-copy path to ever engage.
	// "Active" = the last copy actually took that path (see CopyRtspFrame).
	snapshot.hwAccelCapable = (_deviceManager && _deviceHandle) ? 1u : 0u;
	snapshot.hwAccelActive = _lastCopyWasGpu ? 1u : 0u;

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
