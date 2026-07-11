#include "pch.h"
#include "RtspSessionManager.h"
#include "Tools.h"
#include "MFTools.h"
#include <propsys.h>    // PSCreateMemoryPropertyStore (not in PCH)
#include <chrono>

#pragma comment(lib, "mfuuid.lib")   // MFNETSOURCE_* property keys
#pragma comment(lib, "propsys.lib")  // PSCreateMemoryPropertyStore

RtspSessionManager& RtspSessionManager::Instance()
{
	static RtspSessionManager instance;
	return instance;
}

HRESULT RtspSessionManager::BuildRequestedType(const CameraSessionConfig& config, IMFMediaType** ppType)
{
	RETURN_HR_IF_NULL(E_POINTER, ppType);
	*ppType = nullptr;

	wil::com_ptr_nothrow<IMFMediaType> type;
	RETURN_IF_FAILED(MFCreateMediaType(&type));
	RETURN_IF_FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_IF_FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
	RETURN_IF_FAILED(MFSetAttributeSize(type.get(), MF_MT_FRAME_SIZE, config.width, config.height));
	RETURN_IF_FAILED(MFSetAttributeRatio(type.get(), MF_MT_FRAME_RATE, config.fpsNum, config.fpsDen));
	return type.copy_to(ppType);
}

// Creates (or re-creates) the source reader for the given config and starts the
// Force any decoder MFT in the reader's video stream into low-latency mode.
// MF_LOW_LATENCY on the reader is NOT always honored by hardware (DXVA) H.264/H.265
// decoders, which keep a multi-frame reorder/DPB buffer -> the consumer sees delayed
// frames even though we only ever keep the latest one (no app-level queue exists).
// Setting CODECAPI_AVLowLatencyMode directly on the decoder collapses that buffer.
// The GUID is defined locally to avoid a link-time dependency on the codec API GUID lib.
static void ApplyDecoderLowLatency(IMFSourceReader* reader)
{
	// CODECAPI_AVLowLatencyMode {9C27891A-ED7A-40E1-88E8-B22727A024EE}
	static const GUID kAVLowLatencyMode =
		{ 0x9c27891a, 0xed7a, 0x40e1, { 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee } };

	wil::com_ptr_nothrow<IMFSourceReaderEx> readerEx;
	if (FAILED(reader->QueryInterface(__uuidof(IMFSourceReaderEx), readerEx.put_void())) || !readerEx)
	{
		WINTRACE(L"ApplyDecoderLowLatency - IMFSourceReaderEx unavailable, skipping");
		return;
	}

	// Walk the transform chain for the video stream; set the flag on every MFT that
	// exposes ICodecAPI (the H.264/H.265 decoder). WINTRACE the result so the actual
	// path can be confirmed by attaching to svchost.exe.
	for (DWORD i = 0; ; i++)
	{
		GUID category{};
		wil::com_ptr_nothrow<IMFTransform> mft;
		if (FAILED(readerEx->GetTransformForStream(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &category, mft.put())) || !mft)
			break;

		wil::com_ptr_nothrow<ICodecAPI> codec;
		if (FAILED(mft->QueryInterface(__uuidof(ICodecAPI), codec.put_void())) || !codec)
			continue;

		VARIANT v{};
		v.vt = VT_BOOL;
		v.boolVal = VARIANT_TRUE;
		HRESULT hr = codec->SetValue(&kAVLowLatencyMode, &v);
		WINTRACE(L"ApplyDecoderLowLatency - transform %u CODECAPI_AVLowLatencyMode SetValue: 0x%08X", i, hr);
	}
}

// async read chain. Assumes _lock is held. On success sets _reader/_callback/_config
// and _state = Running. Shared by Start() and the reconnect loop.
HRESULT RtspSessionManager::OpenReaderLocked(const CameraSessionConfig& config)
{
	ReleaseReaderLocked();

	wil::com_ptr_nothrow<RtspReaderCallback> callback;
	callback.attach(new (std::nothrow) RtspReaderCallback());
	RETURN_HR_IF_NULL(E_OUTOFMEMORY, callback);

	// Fire NotifyStreamBroken for THIS generation when the stream drops.
	const UINT64 gen = config.generation;
	callback->SetBrokenHandler([this, gen]() { NotifyStreamBroken(gen); });

	wil::com_ptr_nothrow<IMFAttributes> attrs;
	RETURN_IF_FAILED(MFCreateAttributes(&attrs, 5));
	RETURN_IF_FAILED(attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.get()));
	RETURN_IF_FAILED(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
	RETURN_IF_FAILED(attrs->SetUINT32(MF_LOW_LATENCY, TRUE));
	RETURN_IF_FAILED(attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE));

	// Share the Frame Server's D3D11 device so the decoder runs on the GPU (DXVA)
	// and emits DXGI textures we can copy GPU->GPU. Without it the reader produces
	// system-memory frames and MediaStream falls back to a single CPU copy.
	if (_dxgiManager)
	{
		RETURN_IF_FAILED(attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, _dxgiManager.get()));
		WINTRACE(L"RtspSessionManager::OpenReaderLocked - D3D manager set on reader (GPU decode path)");
	}
	else
	{
		WINTRACE(L"RtspSessionManager::OpenReaderLocked - no D3D manager, reader will use system memory");
	}

	// Minimize RTSP network buffering. Forwarded to the network source via a property
	// store. NOTE: these keys come from the Windows Media network source; the built-in
	// RTSP source may ignore some/all of them. Harmless if unsupported. Check the
	// WINTRACE below + the observed latency to tell whether they took effect.
	wil::com_ptr_nothrow<IPropertyStore> netConfig;
	if (SUCCEEDED(PSCreateMemoryPropertyStore(__uuidof(IPropertyStore), netConfig.put_void())) && netConfig)
	{
		PROPVARIANT pv;
		PROPERTYKEY pk;

		// Initial accelerated pre-buffer (default 5000 ms): 0 = none -> lower startup latency.
		// MFNETSOURCE_* are GUIDs; IPropertyStore expects PROPERTYKEY {fmtid, pid}.
		// The convention for MF network source properties is fmtid = the GUID, pid = 0.
		PropVariantInit(&pv);
		pv.vt = VT_UI4;
		pv.ulVal = 0;
		pk.fmtid = MFNETSOURCE_ACCELERATEDSTREAMINGDURATION;
		pk.pid = 0;
		netConfig->SetValue(pk, pv);
		PropVariantClear(&pv);

		// Steady-state buffering window in seconds: 0 = minimum -> lower ongoing delay
		// (at the cost of being more sensitive to network jitter).
		PropVariantInit(&pv);
		pv.vt = VT_UI4;
		pv.ulVal = 0;
		pk.fmtid = MFNETSOURCE_BUFFERINGTIME;
		pk.pid = 0;
		netConfig->SetValue(pk, pv);
		PropVariantClear(&pv);

		RETURN_IF_FAILED(attrs->SetUnknown(MF_SOURCE_READER_MEDIASOURCE_CONFIG, netConfig.get()));
		WINTRACE(L"RtspSessionManager::OpenReaderLocked - RTSP network buffering minimized (accelerated/buffering = 0)");
	}

	wil::com_ptr_nothrow<IMFSourceReader> reader;
	RETURN_IF_FAILED(MFCreateSourceReaderFromURL(config.rtspUrl, attrs.get(), &reader));

	wil::com_ptr_nothrow<IMFMediaType> requestedType;
	RETURN_IF_FAILED(BuildRequestedType(config, &requestedType));
	RETURN_IF_FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, requestedType.get()));

	wil::com_ptr_nothrow<IMFMediaType> actualType;
	RETURN_IF_FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType));
	RETURN_IF_FAILED(actualType->GetGUID(MF_MT_SUBTYPE, &_actualSubtype));
	RETURN_IF_FAILED(MFGetAttributeSize(actualType.get(), MF_MT_FRAME_SIZE, &_actualWidth, &_actualHeight));
	if (_actualSubtype != MFVideoFormat_NV12 || _actualWidth != config.width || _actualHeight != config.height)
	{
		WINTRACE(L"RtspSessionManager::OpenReaderLocked - negotiated type mismatch requested NV12 %ux%u but got %s %ux%u",
			config.width, config.height, GUID_ToStringW(_actualSubtype).c_str(), _actualWidth, _actualHeight);
		return MF_E_INVALIDMEDIATYPE;
	}

	UINT32 stride = 0;
	HRESULT hrStride = actualType->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride);
	if (FAILED(hrStride) || stride == 0)
	{
		LONG s = 0;
		if (SUCCEEDED(MFGetStrideForBitmapInfoHeader(_actualSubtype.Data1, _actualWidth, &s)))
			stride = static_cast<UINT32>(abs(s));
	}
	_actualStride = stride;

	// Collapse the decoder's internal frame buffer (esp. hardware DXVA decoders) so the
	// consumer always pulls the freshest frame. Must run after the decoder exists
	// (post media-type negotiation) and before reads start.
	ApplyDecoderLowLatency(reader.get());

	RETURN_IF_FAILED(callback->BeginRead(reader.get()));

	_config = config;
	_config.format = MFVideoFormat_NV12;
	_callback = std::move(callback);
	_reader = std::move(reader);
	_running = true;
	_state = RtspSessionState::Running;

	WINTRACE(L"RtspSessionManager::OpenReaderLocked - %s %ux%u stride=%u generation=%llu",
		GUID_ToStringW(_actualSubtype).c_str(), _actualWidth, _actualHeight, _actualStride, _config.generation);
	return S_OK;
}

HRESULT RtspSessionManager::Start(const CameraSessionConfig& config)
{
	if (!config.valid || config.rtspUrl[0] == L'\0')
	{
		winrt::slim_lock_guard lock(_lock);
		_state = RtspSessionState::Failed;
		return E_INVALIDARG;
	}

	// Stop any in-flight reconnect thread before re-arming the session (no _lock held).
	StopReconnectThread();

	winrt::slim_lock_guard lock(_lock);
	if (_running && _config.generation == config.generation)
		return S_OK;

	StopNoLock();
	_stopReconnect = false;
	_state = RtspSessionState::Starting;

	HRESULT hr = OpenReaderLocked(config);
	if (FAILED(hr))
	{
		_state = RtspSessionState::Failed;
		return hr;
	}
	return S_OK;
}

HRESULT RtspSessionManager::Stop(UINT64 generation)
{
	{
		winrt::slim_lock_guard lock(_lock);
		if (!_running)
			return S_OK;
		if (generation != 0 && generation != _config.generation)
			return S_FALSE;
	}

	// Join the reconnect thread outside the lock (it acquires _lock itself).
	StopReconnectThread();

	winrt::slim_lock_guard lock(_lock);
	StopNoLock();
	return S_OK;
}

// Tears down the reader + callback only. Leaves _config/_running/_state untouched so
// the reconnect loop can rebuild without losing the generation it is retrying.
void RtspSessionManager::ReleaseReaderLocked()
{
	if (_callback)
	{
		_callback->Shutdown();
		_callback.reset();
	}
	_reader.reset();
	_actualSubtype = MFVideoFormat_NV12;
	_actualWidth = 0;
	_actualHeight = 0;
	_actualStride = 0;
}

void RtspSessionManager::StopNoLock()
{
	ReleaseReaderLocked();
	ZeroMemory(&_config, sizeof(_config));
	_config.fpsNum = 30;
	_config.fpsDen = 1;
	_running = false;
	_state = RtspSessionState::Stopped;
}

// Signals the reconnect thread to stop and joins it. Must be called WITHOUT _lock held
// (the thread takes _lock during its retry attempts).
void RtspSessionManager::StopReconnectThread()
{
	std::thread toJoin;
	{
		winrt::slim_lock_guard lock(_lock);
		_stopReconnect = true;
		toJoin = std::move(_reconnectThread);
	}
	if (toJoin.joinable())
		toJoin.join();
}

// Called by the reader callback (on an MF work-queue thread) when the stream drops.
// Spawns the reconnect thread once per break. Does NOT tear down the dead reader here
// (we are called from within its callback) — the reconnect loop rebuilds it after the
// first interval, by which time the callback has returned.
void RtspSessionManager::NotifyStreamBroken(UINT64 generation)
{
	winrt::slim_lock_guard lock(_lock);
	if (!_running || generation != _config.generation)
		return; // session was stopped or replaced — stale notification
	if (_state == RtspSessionState::Reconnecting)
		return; // already reconnecting

	// A previous reconnect thread (if any) has finished by now since state != Reconnecting.
	if (_reconnectThread.joinable())
		_reconnectThread.join();

	_state = RtspSessionState::Reconnecting;
	_stopReconnect = false;
	WINTRACE(L"RtspSessionManager::NotifyStreamBroken - starting reconnect (generation=%llu)", generation);
	_reconnectThread = std::thread(&RtspSessionManager::ReconnectLoop, this, _config, generation);
}

// Retries opening the reader up to kMaxReconnectAttempts times, kReconnectIntervalMs apart.
// Runs on its own thread; shows synthetic frames meanwhile (state = Reconnecting).
void RtspSessionManager::ReconnectLoop(CameraSessionConfig config, UINT64 generation)
{
	for (int attempt = 1; attempt <= kMaxReconnectAttempts; ++attempt)
	{
		// Interruptible wait for the retry interval.
		for (int waited = 0; waited < kReconnectIntervalMs; waited += 100)
		{
			if (_stopReconnect.load())
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
		}

		HRESULT hr;
		{
			winrt::slim_lock_guard lock(_lock);
			if (_stopReconnect.load() || !_running || generation != _config.generation)
				return; // session stopped or replaced
			WINTRACE(L"RtspSessionManager::ReconnectLoop - attempt %d/%d", attempt, kMaxReconnectAttempts);
			hr = OpenReaderLocked(config); // sets _state = Running on success
		}
		if (SUCCEEDED(hr))
		{
			WINTRACE(L"RtspSessionManager::ReconnectLoop - reconnected on attempt %d", attempt);
			return;
		}
	}

	winrt::slim_lock_guard lock(_lock);
	if (generation == _config.generation && !_stopReconnect.load())
	{
		WINTRACE(L"RtspSessionManager::ReconnectLoop - giving up after %d attempts", kMaxReconnectAttempts);
		StopNoLock();
		_state = RtspSessionState::Failed;
	}
}

RtspSessionManager::~RtspSessionManager()
{
	// Best-effort cleanup at process teardown: avoid std::terminate on a joinable thread.
	_stopReconnect = true;
	if (_reconnectThread.joinable())
		_reconnectThread.detach();
}

bool RtspSessionManager::IsRunning() const
{
	winrt::slim_lock_guard lock(_lock);
	return _running;
}

UINT64 RtspSessionManager::GetGeneration() const
{
	winrt::slim_lock_guard lock(_lock);
	return _config.generation;
}

RtspSessionState RtspSessionManager::GetState() const
{
	winrt::slim_lock_guard lock(_lock);
	return _state;
}

HRESULT RtspSessionManager::TryGetLatestFrame(RtspFrameSnapshot& frame)
{
	wil::com_ptr_nothrow<IMFSample> sample;
	UINT32 stride = 0;
	UINT64 generation = 0;
	UINT64 frameSeq = 0;

	// Grab a reference (AddRef) to the latest decoded sample under the lock, then
	// release the lock immediately. No pixel copy or allocation happens here — the
	// sample carries its own buffer (GPU texture or system memory) and the actual
	// copy is done by MediaStream::CopyRtspFrame outside any session lock.
	{
		winrt::slim_lock_guard lock(_lock);
		frame = {};
		// Only serve frames while truly Running; during Reconnecting the consumer gets
		// synthetic frames (FrameGenerator) instead of a stale last image.
		RETURN_HR_IF(MF_E_SHUTDOWN, _state != RtspSessionState::Running || !_callback);

		HRESULT hr = _callback->TakeLatestSample(&sample, &frameSeq);
		if (FAILED(hr) || !sample)
			return hr;

		stride = _actualStride;
		generation = _config.generation;
	}

	frame.valid = true;
	frame.stride = stride;
	frame.generation = generation;
	frame.frameSeq = frameSeq;
	sample->GetSampleTime(&frame.sampleTime);
	sample->GetSampleDuration(&frame.sampleDuration);
	frame.sample = std::move(sample);
	return S_OK;
}

void RtspSessionManager::SetD3DManager(IUnknown* manager)
{
	winrt::slim_lock_guard lock(_lock);
	_dxgiManager.reset();
	if (manager)
		manager->QueryInterface(IID_PPV_ARGS(&_dxgiManager));
}

void RtspSessionManager::GetFrameCounters(UINT64& rxFrames, UINT64& droppedFrames) const
{
	winrt::slim_lock_guard lock(_lock);
	if (_callback)
		_callback->GetCounters(rxFrames, droppedFrames);
	else
		rxFrames = droppedFrames = 0;
}
