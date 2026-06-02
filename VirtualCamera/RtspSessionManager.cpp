#include "pch.h"
#include "RtspSessionManager.h"
#include "Tools.h"
#include "MFTools.h"
#include <mfreadwrite.h>
#include <mfapi.h>

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

HRESULT RtspSessionManager::Start(const CameraSessionConfig& config)
{
	if (!config.valid || config.rtspUrl[0] == L'\0')
	{
		winrt::slim_lock_guard lock(_lock);
		_state = RtspSessionState::Failed;
		return E_INVALIDARG;
	}

	winrt::slim_lock_guard lock(_lock);
	if (_running && _config.generation == config.generation)
		return S_OK;

	StopNoLock();
	_state = RtspSessionState::Starting;

	wil::com_ptr_nothrow<RtspReaderCallback> callback;
	callback.attach(new (std::nothrow) RtspReaderCallback());
	RETURN_HR_IF_NULL(E_OUTOFMEMORY, callback);

	wil::com_ptr_nothrow<IMFAttributes> attrs;
	RETURN_IF_FAILED(MFCreateAttributes(&attrs, 4));
	RETURN_IF_FAILED(attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, callback.get()));
	RETURN_IF_FAILED(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
	RETURN_IF_FAILED(attrs->SetUINT32(MF_LOW_LATENCY, TRUE));
	RETURN_IF_FAILED(attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE));

	wil::com_ptr_nothrow<IMFSourceReader> reader;
	HRESULT hr = MFCreateSourceReaderFromURL(config.rtspUrl, attrs.get(), &reader);
	if (FAILED(hr))
	{
		_state = RtspSessionState::Failed;
		return hr;
	}

	wil::com_ptr_nothrow<IMFMediaType> requestedType;
	RETURN_IF_FAILED(BuildRequestedType(config, &requestedType));
	hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, requestedType.get());
	if (FAILED(hr))
	{
		_state = RtspSessionState::Failed;
		return hr;
	}

	wil::com_ptr_nothrow<IMFMediaType> actualType;
	RETURN_IF_FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actualType));
	RETURN_IF_FAILED(actualType->GetGUID(MF_MT_SUBTYPE, &_actualSubtype));
	RETURN_IF_FAILED(MFGetAttributeSize(actualType.get(), MF_MT_FRAME_SIZE, &_actualWidth, &_actualHeight));
	if (_actualSubtype != MFVideoFormat_NV12 || _actualWidth != config.width || _actualHeight != config.height)
	{
		WINTRACE(L"RtspSessionManager::Start - negotiated type mismatch requested NV12 %ux%u but got %s %ux%u",
			config.width, config.height, GUID_ToStringW(_actualSubtype).c_str(), _actualWidth, _actualHeight);
		_state = RtspSessionState::Failed;
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

	hr = callback->BeginRead(reader.get());
	if (FAILED(hr))
	{
		_state = RtspSessionState::Failed;
		return hr;
	}

	_config = config;
	_config.format = MFVideoFormat_NV12;
	_callback = std::move(callback);
	_reader = std::move(reader);
	_running = true;
	_state = RtspSessionState::Running;

	WINTRACE(L"RtspSessionManager::Start - %s %ux%u stride=%u generation=%llu",
		GUID_ToStringW(_actualSubtype).c_str(), _actualWidth, _actualHeight, _actualStride, _config.generation);
	return S_OK;
}

HRESULT RtspSessionManager::Stop(UINT64 generation)
{
	winrt::slim_lock_guard lock(_lock);
	if (!_running)
		return S_OK;
	if (generation != 0 && generation != _config.generation)
		return S_FALSE;
	StopNoLock();
	return S_OK;
}

void RtspSessionManager::StopNoLock()
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
	ZeroMemory(&_config, sizeof(_config));
	_config.fpsNum = 30;
	_config.fpsDen = 1;
	_running = false;
	_state = RtspSessionState::Stopped;
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
	winrt::slim_lock_guard lock(_lock);
	frame = {};
	RETURN_HR_IF(MF_E_SHUTDOWN, !_running || !_callback);

	wil::com_ptr_nothrow<IMFSample> sample;
	HRESULT hr = _callback->TakeLatestSample(&sample);
	if (FAILED(hr) || !sample)
		return hr;

	wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
	RETURN_IF_FAILED(sample->ConvertToContiguousBuffer(&buffer));
	BYTE* src = nullptr;
	DWORD dataSize = 0;
	RETURN_IF_FAILED(buffer->Lock(&src, nullptr, &dataSize));
	auto bytes = std::make_shared<std::vector<BYTE>>(dataSize);
	if (!bytes)
	{
		buffer->Unlock();
		return E_OUTOFMEMORY;
	}
	CopyMemory(bytes->data(), src, dataSize);
	buffer->Unlock();

	frame.valid = true;
	frame.stride = _actualStride;
	frame.generation = _config.generation;
	frame.bytes = std::move(bytes);
	frame.dataSize = dataSize;
	sample->GetSampleTime(&frame.sampleTime);
	sample->GetSampleDuration(&frame.sampleDuration);
	return S_OK;
}
