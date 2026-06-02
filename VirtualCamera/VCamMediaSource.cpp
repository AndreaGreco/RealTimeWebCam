/*
 * ══════════════════════════════════════════════════════════════════════════════
 *  CICLO DI VITA DELLA VIRTUAL CAMERA — guida per sviluppatori
 * ══════════════════════════════════════════════════════════════════════════════
 *
 *  LATO APP (RTVirtualCamera.exe, processo utente):
 *  ─────────────────────────────────────────────────
 *  1. MFCreateVirtualCamera()
 *       Registra la camera nel sistema. La DLL NON viene ancora caricata.
 *
  *  2. IMFVirtualCamera::SetString/SetUINT32()   [in MFPipeline/VirtualCamera.cpp]
 *       Salva URL RTSP + width/height/fps come MF attributes sull'oggetto
 *       IMFVirtualCamera. Devono essere chiamati PRIMA di Start().
 *
 *  3. IMFVirtualCamera::Start()
 *       Trigger principale. Il Frame Server (svchost.exe):
 *         a) Carica VCamSampleSource.dll nel suo processo
 *         b) Crea il COM object tramite DllGetClassObject → IClassFactory
 *         c) Chiama MediaSource::Initialize(attributes)  ← qui sotto
 *         d) Invia MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_INITIALIZE
 *            al VcamStartCallback registrato dall'app
 *
 *  LATO FRAME SERVER (svchost.exe, processo di sistema):
 *  ──────────────────────────────────────────────────────
  *  4. Activator::ActivateObject() + VCamMediaSource::Initialize()  ← QUESTO FILE
 *       La config viene letta dall'activator e poi applicata alla source/agli stream.
 *
 *  5. Quando Zoom/Teams/Windows Camera apre la virtual camera:
 *       Frame Server chiama MediaSource::Start()
 *       → MediaStream::Start()
 *       → RtspSessionManager::Start(config)
 *
 *  6. RequestSample() — chiamato ~30x/sec dal Frame Server
 *       MediaStream legge l'ultimo frame dal manager RTSP condiviso,
 *       lo copia nel sample allocato e lo consegna al Frame Server.
 *
 *  7. Stop/Shutdown fermano stream e manager.
 * ══════════════════════════════════════════════════════════════════════════════
 */
#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "MediaStream.h"
#include "VCamMediaSource.h"
#include "..\Shared\VCamConfig.h"

HRESULT VCamMediaSource::SetupCameraSettings(IMFAttributes* attributes)
{
	RETURN_HR_IF_NULL(E_POINTER, attributes);

	const UINT64 nextGeneration = _camera_config.generation + 1;
	_camera_config = {};
	_camera_config.generation = nextGeneration;

	wil::unique_cotaskmem_string urlStr;
	UINT32 urlLen = 0;
	HRESULT hr = attributes->GetAllocatedString(MF_VCAM_RTSP_URL, &urlStr, &urlLen);
	if (FAILED(hr) || urlLen == 0)
	{
		WINTRACE(L"VCamMediaSource::SetupCameraSettings - MF_VCAM_RTSP_URL not found (hr=0x%08X), black frames", hr);
		return hr;
	}

	wcsncpy_s(_camera_config.rtspUrl, urlStr.get(), _TRUNCATE);
	if (FAILED(attributes->GetUINT32(MF_VCAM_WIDTH, &_camera_config.width))) _camera_config.width = 1920;
	if (FAILED(attributes->GetUINT32(MF_VCAM_HEIGHT, &_camera_config.height))) _camera_config.height = 1080;
	if (FAILED(attributes->GetUINT32(MF_VCAM_FPS_NUM, &_camera_config.fpsNum))) _camera_config.fpsNum = 30;
	if (FAILED(attributes->GetUINT32(MF_VCAM_FPS_DEN, &_camera_config.fpsDen))) _camera_config.fpsDen = 1;

	if (_camera_config.width == 0) _camera_config.width = 1920;
	if (_camera_config.height == 0) _camera_config.height = 1080;
	if (_camera_config.fpsNum == 0) _camera_config.fpsNum = 30;
	if (_camera_config.fpsDen == 0) _camera_config.fpsDen = 1;
	_camera_config.format = MFVideoFormat_NV12;
	_camera_config.valid = true;

	WINTRACE(L"VCamMediaSource::SetupCameraSettings - config received: url=%s %ux%u @%u/%u gen=%llu",
		_camera_config.rtspUrl,
		_camera_config.width,
		_camera_config.height,
		_camera_config.fpsNum,
		_camera_config.fpsDen,
		_camera_config.generation);

	LOG_IF_FAILED(attributes->CopyAllItems(this));
	return ApplyRuntimeContext();
}

HRESULT VCamMediaSource::ApplyRuntimeContext()
{
	StreamRuntimeContext context{};
	context.config = _camera_config;
	context.rtspManager = _rtspManager;
	WINTRACE(L"VCamMediaSource::ApplyRuntimeContext - config %u x %u @ %u / %u gen=%llu format=%ls",
		static_cast<unsigned>(context.config.width),
		static_cast<unsigned>(context.config.height),
		static_cast<unsigned>(context.config.fpsNum),
		static_cast<unsigned>(context.config.fpsDen),
		context.config.generation,
		GUID_ToStringW(context.config.format).c_str());

	for (auto& stream : _streams)
	{
		if (stream)
			RETURN_IF_FAILED(stream->SetRuntimeContext(context));
	}

	return S_OK;
}

HRESULT VCamMediaSource::Initialize(IMFAttributes* attributes)
{
	HRESULT hr;

	for (auto i = 0; i < _streams.size(); i++)
	{
		RETURN_IF_FAILED(_streams[i]->Initialize(this, i));
	}

	RETURN_IF_FAILED(ApplyRuntimeContext());

	wil::com_ptr_nothrow<IMFSensorProfileCollection> collection;
	RETURN_IF_FAILED(MFCreateSensorProfileCollection(&collection));

	DWORD streamId = 0;
	wil::com_ptr_nothrow<IMFSensorProfile> profile;
	hr = MFCreateSensorProfile(KSCAMERAPROFILE_Legacy, 0, nullptr, &profile);
	RETURN_IF_FAILED(hr);

	hr = profile->AddProfileFilter(streamId, L"((RES==;FRT<=30,1;SUT==))");
	RETURN_IF_FAILED(hr);

	hr = collection->AddProfile(profile.get());
	RETURN_IF_FAILED(hr);

	hr = MFCreateSensorProfile(KSCAMERAPROFILE_HighFrameRate, 0, nullptr, &profile);
	RETURN_IF_FAILED(hr);

	hr = profile->AddProfileFilter(streamId, L"((RES==;FRT>=60,1;SUT==))");
	RETURN_IF_FAILED(hr);

	hr = collection->AddProfile(profile.get());
	RETURN_IF_FAILED(hr);

	hr = SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, collection.get());
	RETURN_IF_FAILED(hr);

	auto streams = wil::make_unique_cotaskmem_array<IMFStreamDescriptor*>(_streams.size());
	RETURN_HR_IF(E_OUTOFMEMORY, !streams);
	for (uint32_t i = 0; i < streams.size(); i++)
	{
		wil::com_ptr_nothrow<IMFStreamDescriptor> desc;
		hr = _streams[i]->GetStreamDescriptor(&desc);
		RETURN_IF_FAILED(hr);

		streams[i] = desc.detach();
	}

	hr = MFCreatePresentationDescriptor((DWORD)streams.size(), streams.get(), &_descriptor);
	RETURN_IF_FAILED(hr);

	for (uint32_t i = 0; i < streams.size(); i++)
	{
		if (streams[i])
			streams[i]->Release();
	}

	RETURN_IF_FAILED(MFCreateEventQueue(&_queue));
	return S_OK;
}

int VCamMediaSource::GetStreamIndexById(DWORD id)
{
	for (uint32_t i = 0; i < _streams.size(); i++)
	{
		wil::com_ptr_nothrow<IMFStreamDescriptor> desc;
		if (FAILED(_streams[i]->GetStreamDescriptor(&desc)))
			return -1;

		DWORD sid = 0;
		if (FAILED(desc->GetStreamIdentifier(&sid)))
			return -1;

		if (sid == id)
			return i;
	}
	return -1;
}

// IMFMediaEventGenerator
STDMETHODIMP VCamMediaSource::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState)
{
	WINTRACE(L"VCamMediaSource::BeginGetEvent pCallback:%p punkState:%p", pCallback, punkState);
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->BeginGetEvent(pCallback, punkState));
	return S_OK;
}

STDMETHODIMP VCamMediaSource::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent)
{
	WINTRACE(L"VCamMediaSource::EndGetEvent");
	RETURN_HR_IF_NULL(E_POINTER, ppEvent);
	*ppEvent = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->EndGetEvent(pResult, ppEvent));
	return S_OK;
}

STDMETHODIMP VCamMediaSource::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent)
{
	WINTRACE(L"VCamMediaSource::GetEvent");
	RETURN_HR_IF_NULL(E_POINTER, ppEvent);
	*ppEvent = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->GetEvent(dwFlags, ppEvent));
	return S_OK;
}

STDMETHODIMP VCamMediaSource::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue)
{
	WINTRACE(L"VCamMediaSource::QueueEvent");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue));
	return S_OK;
}

// IMFMediaSource
STDMETHODIMP VCamMediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** ppPresentationDescriptor)
{
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"VCamMediaSource::CreatePresentationDescriptor");
	RETURN_HR_IF_NULL(E_POINTER, ppPresentationDescriptor);
	*ppPresentationDescriptor = nullptr;
	
	RETURN_HR_IF(MF_E_SHUTDOWN, !_descriptor);

	RETURN_IF_FAILED(_descriptor->Clone(ppPresentationDescriptor));
	return S_OK;
}

STDMETHODIMP VCamMediaSource::GetCharacteristics(DWORD* pdwCharacteristics)
{
	WINTRACE(L"VCamMediaSource::GetCharacteristics");
	RETURN_HR_IF_NULL(E_POINTER, pdwCharacteristics);

	*pdwCharacteristics = MFMEDIASOURCE_IS_LIVE;
	return S_OK;
}

STDMETHODIMP VCamMediaSource::Pause()
{
	WINTRACE(L"VCamMediaSource::Pause");
	RETURN_HR(MF_E_INVALID_STATE_TRANSITION);
}

STDMETHODIMP VCamMediaSource::Shutdown()
{
	WINTRACE(L"VCamMediaSource::Shutdown");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	LOG_IF_FAILED_MSG(_queue->Shutdown(), "Queue shutdown failed");
	_queue.reset();

	for (uint32_t i = 0; i < _streams.size(); i++)
	{
		_streams[i]->Shutdown();
	}

	if (_rtspManager)
		LOG_IF_FAILED(_rtspManager->Stop(_camera_config.generation));

	_descriptor.reset();
	_attributes.reset();
	return S_OK;
}

STDMETHODIMP VCamMediaSource::Start(IMFPresentationDescriptor* pPresentationDescriptor, const GUID* pguidTimeFormat, const PROPVARIANT* pvarStartPosition)
{
	WINTRACE(L"VCamMediaSource::Start pPresentationDescriptor:%p pguidTimeFormat:%p pvarStartPosition:%p", pPresentationDescriptor, pguidTimeFormat, pvarStartPosition);
	RETURN_HR_IF_NULL(E_POINTER, pPresentationDescriptor);
	RETURN_HR_IF_NULL(E_POINTER, pvarStartPosition);
	RETURN_HR_IF_MSG(E_INVALIDARG, pguidTimeFormat && *pguidTimeFormat != GUID_NULL, "Unsupported guid time format");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_descriptor);

	if (_camera_config.valid && _rtspManager)
	{
		LOG_IF_FAILED(_rtspManager->Start(_camera_config));
	}

	DWORD count;
	RETURN_IF_FAILED(pPresentationDescriptor->GetStreamDescriptorCount(&count));
	RETURN_HR_IF_MSG(E_INVALIDARG, count != (DWORD)_streams.size(), "Invalid number of descriptor streams");

	wil::unique_prop_variant time;
	RETURN_IF_FAILED(InitPropVariantFromInt64(MFGetSystemTime(), &time));

	for (DWORD i = 0; i < count; i++)
	{
		wil::com_ptr_nothrow<IMFStreamDescriptor> desc;
		BOOL selected = FALSE;
		RETURN_IF_FAILED(pPresentationDescriptor->GetStreamDescriptorByIndex(i, &selected, &desc));

		DWORD id = 0;
		RETURN_IF_FAILED(desc->GetStreamIdentifier(&id));

		auto index = GetStreamIndexById(id);
		RETURN_HR_IF(E_FAIL, index < 0);

		BOOL thisSelected = FALSE;
		wil::com_ptr_nothrow<IMFStreamDescriptor> thisDesc;
		RETURN_IF_FAILED(_descriptor->GetStreamDescriptorByIndex(index, &thisSelected, &thisDesc));

		MF_STREAM_STATE state;
		RETURN_IF_FAILED(_streams[index]->GetStreamState(&state));
		if (thisSelected && state == MF_STREAM_STATE_STOPPED)
		{
			thisSelected = FALSE;
		}
		else if (!thisSelected && state != MF_STREAM_STATE_STOPPED)
		{
			thisSelected = TRUE;
		}

		WINTRACE(L"VCamMediaSource::Start stream[%i] selected:%i thisSelected:%i", index, selected, thisSelected);
		if (selected != thisSelected)
		{
			if (selected)
			{
				RETURN_IF_FAILED(_descriptor->SelectStream(index));

				wil::com_ptr_nothrow<IUnknown> unk;
				RETURN_IF_FAILED(_streams[index].copy_to(&unk));
				RETURN_IF_FAILED(_queue->QueueEventParamUnk(MENewStream, GUID_NULL, S_OK, unk.get()));

				wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
				wil::com_ptr_nothrow<IMFMediaType> type;
				RETURN_IF_FAILED(desc->GetMediaTypeHandler(&handler));
				RETURN_IF_FAILED(handler->GetCurrentMediaType(&type));
				RETURN_IF_FAILED(_streams[index]->Start(type.get()));
			}
			else
			{
				RETURN_IF_FAILED(_descriptor->DeselectStream(index));
				RETURN_IF_FAILED(_streams[index]->Stop());
			}
		}
	}

	RETURN_IF_FAILED(_queue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &time));
	return S_OK;
}

STDMETHODIMP VCamMediaSource::Stop()
{
	WINTRACE(L"VCamMediaSource::Stop");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_descriptor);

	wil::unique_prop_variant time;
	RETURN_IF_FAILED(InitPropVariantFromInt64(MFGetSystemTime(), &time));

	for (DWORD i = 0; i < _streams.size(); i++)
	{
		RETURN_IF_FAILED(_streams[i]->Stop());
		RETURN_IF_FAILED(_descriptor->DeselectStream(i));
	}

	if (_rtspManager)
		LOG_IF_FAILED(_rtspManager->Stop(_camera_config.generation));

	RETURN_IF_FAILED(_queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, &time));
	return S_OK;
}

// IMFMediaSourceEx
STDMETHODIMP VCamMediaSource::GetSourceAttributes(IMFAttributes** ppAttributes)
{
	WINTRACE(L"VCamMediaSource::GetSourceAttributes");
	RETURN_HR_IF_NULL(E_POINTER, ppAttributes);
	winrt::slim_lock_guard lock(_lock);

	RETURN_IF_FAILED(QueryInterface(IID_PPV_ARGS(ppAttributes)));
	return S_OK;
}

STDMETHODIMP VCamMediaSource::GetStreamAttributes(DWORD dwStreamIdentifier, IMFAttributes** ppAttributes)
{
	WINTRACE(L"VCamMediaSource::GetStreamAttributes dwStreamIdentifier:%u", dwStreamIdentifier);
	RETURN_HR_IF_NULL(E_POINTER, ppAttributes);
	*ppAttributes = nullptr;
	winrt::slim_lock_guard lock(_lock);

	RETURN_HR_IF_MSG(E_FAIL, dwStreamIdentifier >= _streams.size(), "dwStreamIdentifier %u is invalid", dwStreamIdentifier);
	RETURN_IF_FAILED(_streams[dwStreamIdentifier].copy_to(ppAttributes));
	return S_OK;
}

STDMETHODIMP VCamMediaSource::SetD3DManager(IUnknown* pManager)
{
	WINTRACE(L"VCamMediaSource::SetD3DManager pManager:%p", pManager);
	RETURN_HR_IF_NULL(E_POINTER, pManager);
	winrt::slim_lock_guard lock(_lock);

	for (DWORD i = 0; i < _streams.size(); i++)
	{
		RETURN_IF_FAILED(_streams[i]->SetD3DManager(pManager));
	}
	return S_OK;
}

// IMFGetService
STDMETHODIMP VCamMediaSource::GetService(REFGUID siid, REFIID iid, LPVOID* ppvObject)
{
	if (iid == __uuidof(IMFDeviceController) || iid == __uuidof(IMFDeviceController2))
		return MF_E_UNSUPPORTED_SERVICE;

	WINTRACE(L"VCamMediaSource::GetService siid '%s' iid '%s' failed", GUID_ToStringW(siid).c_str(), GUID_ToStringW(iid).c_str());
	RETURN_HR(MF_E_UNSUPPORTED_SERVICE);
}

// IMFSampleAllocatorControl
STDMETHODIMP VCamMediaSource::SetDefaultAllocator(DWORD dwOutputStreamID, IUnknown* pAllocator)
{
	WINTRACE(L"VCamMediaSource::SetDefaultAllocator dwOutputStreamID:%u pAllocator:%p", dwOutputStreamID, pAllocator);
	RETURN_HR_IF_NULL(E_POINTER, pAllocator);
	winrt::slim_lock_guard lock(_lock);

	auto index = GetStreamIndexById(dwOutputStreamID);
	RETURN_HR_IF(E_FAIL, index < 0);

	RETURN_HR_IF_MSG(E_FAIL, index < 0 || (DWORD)index >= _streams.size(), "dwOutputStreamID %u is invalid, index:%i", dwOutputStreamID, index);
	RETURN_HR(_streams[index]->SetAllocator(pAllocator));
}

STDMETHODIMP VCamMediaSource::GetAllocatorUsage(DWORD dwOutputStreamID, DWORD* pdwInputStreamID, MFSampleAllocatorUsage* peUsage)
{
	WINTRACE(L"VCamMediaSource::GetAllocatorUsage dwOutputStreamID:%u pdwInputStreamID:%p peUsage:%p", dwOutputStreamID, pdwInputStreamID, peUsage);
	RETURN_HR_IF_NULL(E_POINTER, peUsage);
	RETURN_HR_IF_NULL(E_POINTER, pdwInputStreamID);
	winrt::slim_lock_guard lock(_lock);

	auto index = GetStreamIndexById(dwOutputStreamID);
	RETURN_HR_IF(E_FAIL, index < 0);

	RETURN_HR_IF_MSG(E_FAIL, index < 0 || (DWORD)index >= _streams.size(), "dwOutputStreamID %u is invalid, index:%i", dwOutputStreamID, index);
	*pdwInputStreamID = dwOutputStreamID;
	*peUsage = _streams[index]->GetAllocatorUsage();
	return S_OK;
}

// IKsControl
// Custom KsProperty set GUID for VCam runtime configuration.
// Property ID 1 = RTSP URL (wide string). Must match MFPipeline/VirtualCamera.cpp.
static const GUID KSPROPSETID_VCam_Config = { 0xc1d2e3f4, 0xa5b6, 0x47c8, { 0xd9, 0xea, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60 } };
static const ULONG KSPROPID_VCam_RtspUrl = 1;

STDMETHODIMP_(NTSTATUS) VCamMediaSource::KsProperty(PKSPROPERTY property, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	RETURN_HR_IF_NULL(E_POINTER, property);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"VCamMediaSource::KsProperty prop:%s", PKSIDENTIFIER_ToString(property, length).c_str());

	// Handle our custom property set: KSPROPSETID_VCam_Config / KSPROPID_VCam_RtspUrl
	if (property->Set == KSPROPSETID_VCam_Config &&
		property->Id == KSPROPID_VCam_RtspUrl &&
		(property->Flags & KSPROPERTY_TYPE_SET))
	{
		if (data == nullptr || dataLength < sizeof(wchar_t))
		{
			WINTRACE(L"VCamMediaSource::KsProperty - RTSP URL: invalid buffer");
			return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
		}

		// Read the wide-string URL from the payload
		const wchar_t* url = static_cast<const wchar_t*>(data);
		ULONG maxChars = dataLength / sizeof(wchar_t);

		// Ensure null-terminated within the buffer
		std::wstring newUrl(url, wcsnlen(url, maxChars));

		WINTRACE(L"VCamMediaSource::KsProperty - RTSP URL received: %s", newUrl.c_str());

		// Propagate to all streams (in case Start() was already called)
#if 0
		for (auto& stream : _streams)
		{
			if (stream)
				stream->SetRTSPUrl(_rtspUrl.c_str());
		}
#endif

		*bytesReturned = 0;
		return S_OK;
	}

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP_(NTSTATUS) VCamMediaSource::KsMethod(PKSMETHOD method, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	RETURN_HR_IF_NULL(E_POINTER, method);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP_(NTSTATUS) VCamMediaSource::KsEvent(PKSEVENT evt, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
