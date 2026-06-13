#include "pch.h"
#include "RtspSessionManager.h"
#include "Tools.h"
#include "MFTools.h"
#include <mfreadwrite.h>
#include <mfapi.h>
#include <chrono>

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

		HRESULT hr = _callback->TakeLatestSample(&sample);
		if (FAILED(hr) || !sample)
			return hr;

		stride = _actualStride;
		generation = _config.generation;
	}

	frame.valid = true;
	frame.stride = stride;
	frame.generation = generation;
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
