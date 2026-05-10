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
 *  2. IMFVirtualCamera::AddProperty()   [in MFPipeline/VirtualCamera.cpp]
 *       Salva l'URL RTSP come device property (DEVPROPKEY) sull'oggetto
 *       IMFVirtualCamera. Deve essere chiamato PRIMA di Start().
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
 *  4. MediaSource::Initialize(attributes)          ← QUESTO FILE
 *       Unica occasione per leggere la configurazione dall'app.
 *       L'URL RTSP deve essere recuperato qui, altrimenti non arriva mai
 *       a InitializeRTSPReader(). Se il log mostra "No RTSP URL property
 *       found", l'URL non è nei attributes — vedere TraceMFAttributes sotto.
 *
 *  5. Quando Zoom/Teams/Windows Camera apre la virtual camera:
 *       Frame Server chiama MediaSource::Start()
 *       → Evento MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_START verso l'app
 *       → MediaStream::Start() → InitializeRTSPReader() → apre RTSP
 *
 *  6. RequestSample() — chiamato ~30x/sec dal Frame Server
 *       Legge un frame da _rtspReader e lo consegna al Frame Server,
 *       che lo distribuisce a Zoom, Teams, ecc.
 *       Se RTSP non disponibile → FrameGenerator genera "Camera IP non connessa"
 *
 *  7. Quando l'app consumer chiude la camera:
 *       Frame Server chiama MediaSource::Stop()
 *       → Evento MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_STOP
 *       → MediaStream::Stop() → _rtspReader.reset() (connessione RTSP chiusa)
 *
 *  8. Quando l'app chiama IMFVirtualCamera::Remove():
 *       Frame Server chiama MediaSource::Shutdown()
 *       → Evento MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_UNINITIALIZE
 *       → DLL scaricata dal processo Frame Server
 *
 *  Log visibili in TraceSpy (ETW GUID: 964d4572-adb9-4f3a-8170-fcbecec27467)
 * ══════════════════════════════════════════════════════════════════════════════
 */
#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "MediaStream.h"
#include "MediaSource.h"

// Custom MF attribute GUID used to carry the RTSP URL from the app process to the
// Frame Server. The app sets it via IMFVirtualCamera::SetString() (which works because
// IMFVirtualCamera inherits IMFAttributes); the Frame Server forwards all attributes
// to MediaSource::Initialize(). Must match the same GUID in MFPipeline/VirtualCamera.cpp.
static const GUID MF_VCAM_RTSP_URL = { 0xb8e0a5c1, 0x2d3f, 0x4a6b, { 0x9c, 0x8d, 0x1e, 0x2f, 0x3a, 0x4b, 0x5c, 0x6d } };

HRESULT MediaSource::Initialize(IMFAttributes* attributes)
{
	// ── Passo 4 del ciclo di vita ────────────────────────────────────────────
	// Chiamato dal Frame Server subito dopo aver caricato la DLL, in risposta
	// a IMFVirtualCamera::Start() dell'app. È l'UNICO momento in cui si può
	// leggere la configurazione passata da AddProperty(). Dopo questa chiamata
	// il parametro 'attributes' non è più accessibile.

	if (attributes)
	{
		// DIAGNOSTICA: stampa TUTTI gli attributi ricevuti dal Frame Server.
		// In TraceSpy (ETW) cerca "Initialize-attrs" per vedere la lista completa.
		// Se {b8e0a5c1-2d3f-4a6b-...} non appare, AddProperty() non ha prodotto
		// un IMFAttribute corrispondente e l'URL non potrà mai essere letto qui.
		WINTRACE(L"MediaSource::Initialize - attributi ricevuti dal Frame Server:");
		TraceMFAttributes(attributes, L"Initialize-attrs");

		RETURN_IF_FAILED(attributes->CopyAllItems(this));

		// Tenta di leggere l'URL RTSP dagli attributi.
		// La chiave MF_VCAM_RTSP_URL è il GUID {b8e0a5c1-2d3f-4a6b-...}.
		// In MFPipeline/VirtualCamera.cpp, AddProperty() usa un DEVPROPKEY
		// con lo stesso fmtid; il Frame Server dovrebbe mapparli qui.
		// Se GetStringLength fallisce, il GUID non è presente → vedere DIAGNOSTICA sopra.
		UINT32 urlLength = 0;
		HRESULT hr = attributes->GetStringLength(MF_VCAM_RTSP_URL, &urlLength);
		if (SUCCEEDED(hr) && urlLength > 0)
		{
			_rtspUrl.resize(urlLength + 1);
			hr = attributes->GetString(MF_VCAM_RTSP_URL, &_rtspUrl[0], urlLength + 1, &urlLength);
			if (SUCCEEDED(hr))
			{
				_rtspUrl.resize(urlLength);
				WINTRACE(L"MediaSource::Initialize - RTSP URL from property: %s", _rtspUrl.c_str());
			}
			else
			{
				_rtspUrl.clear();
				WINTRACE(L"MediaSource::Initialize - Failed to get RTSP URL string");
			}
		}
		else
		{
			// L'URL RTSP non è negli attributi ricevuti. Cause possibili:
			//   A) AddProperty() lato app è fallita — controllare DebugLog in VirtualCamera.cpp
			//   B) Il Frame Server non mappa DEVPROPKEY.fmtid → IMFAttributes per questo GUID
			//   C) SetRTSPUrl() è stata chiamata DOPO Start() (timing errato)
			// Vedere DIAGNOSTICA sopra (Initialize-attrs in TraceSpy) per la lista completa.
			WINTRACE(L"MediaSource::Initialize - No RTSP URL property found, will use black frames");
		}
	}

	wil::com_ptr_nothrow<IMFSensorProfileCollection> collection;
	RETURN_IF_FAILED(MFCreateSensorProfileCollection(&collection));

	DWORD streamId = 0;
	wil::com_ptr_nothrow<IMFSensorProfile> profile;
	RETURN_IF_FAILED(MFCreateSensorProfile(KSCAMERAPROFILE_Legacy, 0, nullptr, &profile));
	RETURN_IF_FAILED(profile->AddProfileFilter(streamId, L"((RES==;FRT<=30,1;SUT==))"));
	RETURN_IF_FAILED(collection->AddProfile(profile.get()));

	RETURN_IF_FAILED(MFCreateSensorProfile(KSCAMERAPROFILE_HighFrameRate, 0, nullptr, &profile));
	RETURN_IF_FAILED(profile->AddProfileFilter(streamId, L"((RES==;FRT>=60,1;SUT==))"));
	RETURN_IF_FAILED(collection->AddProfile(profile.get()));
	RETURN_IF_FAILED(SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, collection.get()));

	// AppInfo::Current() is only available for packaged (UWP/MSIX) applications.
	// For desktop apps, we skip this configuration.
	// The virtual camera will still work without the package family name.
	// Commented out to avoid exception when running from desktop applications.
#if 0
	try
	{
		auto appInfo = winrt::Windows::ApplicationModel::AppInfo::Current();
		if (appInfo)
		{
			RETURN_IF_FAILED(SetString(MF_VIRTUALCAMERA_CONFIGURATION_APP_PACKAGE_FAMILY_NAME, appInfo.PackageFamilyName().data()));
		}
	}
	catch (...)
	{
		WINTRACE(L"MediaSource::Initialize no AppX");
	}
#endif

	auto streams = wil::make_unique_cotaskmem_array<wil::com_ptr_nothrow<IMFStreamDescriptor>>(_streams.size());
	for (uint32_t i = 0; i < streams.size(); i++)
	{
		wil::com_ptr_nothrow<IMFStreamDescriptor> desc;
		RETURN_IF_FAILED(_streams[i]->GetStreamDescriptor(&desc));
		streams[i] = desc.detach();
	}
	RETURN_IF_FAILED(MFCreatePresentationDescriptor((DWORD)streams.size(), streams.get(), &_descriptor));
	RETURN_IF_FAILED(MFCreateEventQueue(&_queue));
	return S_OK;
}

int MediaSource::GetStreamIndexById(DWORD id)
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
STDMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* pCallback, IUnknown* punkState)
{
	WINTRACE(L"MediaSource::BeginGetEvent pCallback:%p punkState:%p", pCallback, punkState);
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->BeginGetEvent(pCallback, punkState));
	return S_OK;
}

STDMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent)
{
	WINTRACE(L"MediaSource::EndGetEvent");
	RETURN_HR_IF_NULL(E_POINTER, ppEvent);
	*ppEvent = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->EndGetEvent(pResult, ppEvent));
	return S_OK;
}

STDMETHODIMP MediaSource::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent)
{
	WINTRACE(L"MediaSource::GetEvent");
	RETURN_HR_IF_NULL(E_POINTER, ppEvent);
	*ppEvent = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->GetEvent(dwFlags, ppEvent));
	return S_OK;
}

STDMETHODIMP MediaSource::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue)
{
	WINTRACE(L"MediaSource::QueueEvent");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	RETURN_IF_FAILED(_queue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue));
	return S_OK;
}

// IMFMediaSource
STDMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** ppPresentationDescriptor)
{
	WINTRACE(L"MediaSource::CreatePresentationDescriptor");
	RETURN_HR_IF_NULL(E_POINTER, ppPresentationDescriptor);
	*ppPresentationDescriptor = nullptr;
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_descriptor);
	
	RETURN_IF_FAILED(_descriptor->Clone(ppPresentationDescriptor));
	return S_OK;
}

STDMETHODIMP MediaSource::GetCharacteristics(DWORD* pdwCharacteristics)
{
	WINTRACE(L"MediaSource::GetCharacteristics");
	RETURN_HR_IF_NULL(E_POINTER, pdwCharacteristics);

	*pdwCharacteristics = MFMEDIASOURCE_IS_LIVE;
	return S_OK;
}

STDMETHODIMP MediaSource::Pause()
{
	WINTRACE(L"MediaSource::Pause");
	RETURN_HR(MF_E_INVALID_STATE_TRANSITION);
}

STDMETHODIMP MediaSource::Shutdown()
{
	WINTRACE(L"MediaSource::Shutdown");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue);

	LOG_IF_FAILED_MSG(_queue->Shutdown(), "Queue shutdown failed");
	_queue.reset();

	for (uint32_t i = 0; i < _streams.size(); i++)
	{
		_streams[i]->Shutdown();
	}

	_descriptor.reset();
	_attributes.reset();
	return S_OK;
}

void MediaSource::SetRTSPUrl(std::wstring url)
{
	_rtspUrl = url;
}

std::wstring MediaSource::GetRTSPUrl()
{
	return _rtspUrl;
}

STDMETHODIMP MediaSource::Start(IMFPresentationDescriptor* pPresentationDescriptor, const GUID* pguidTimeFormat, const PROPVARIANT* pvarStartPosition)
{
	WINTRACE(L"MediaSource::Start pPresentationDescriptor:%p pguidTimeFormat:%p pvarStartPosition:%p", pPresentationDescriptor, pguidTimeFormat, pvarStartPosition);
	RETURN_HR_IF_NULL(E_POINTER, pPresentationDescriptor);
	RETURN_HR_IF_NULL(E_POINTER, pvarStartPosition);
	RETURN_HR_IF_MSG(E_INVALIDARG, pguidTimeFormat && *pguidTimeFormat != GUID_NULL, "Unsupported guid time format");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_descriptor);

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
		RETURN_IF_FAILED(_streams[i]->GetStreamState(&state));
		if (thisSelected && state == MF_STREAM_STATE_STOPPED )
		{
			thisSelected = FALSE;
		}
		else if (!thisSelected && state != MF_STREAM_STATE_STOPPED)
		{
			thisSelected = TRUE;
		}

		WINTRACE(L"MediaSource::Start stream[%i] selected:%i thisSelected:%i", index, selected, thisSelected);
		if (selected != thisSelected)
		{
			if (selected)
			{
			// ── Passo 5 del ciclo di vita ──────────────────────────────────
			// Un'app (Zoom, Teams, ecc.) ha aperto la virtual camera.
			// Passiamo l'URL RTSP allo stream PRIMA di chiamare Start():
			// MediaStream::Start() → InitializeRTSPReader() usa _rtspUrl.
			RETURN_IF_FAILED(_descriptor->SelectStream(index));

			wil::com_ptr_nothrow<IUnknown> unk;
			RETURN_IF_FAILED(_streams[index].copy_to(&unk));
			RETURN_IF_FAILED(_queue->QueueEventParamUnk(MENewStream, GUID_NULL, S_OK, unk.get()));

			wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
			wil::com_ptr_nothrow<IMFMediaType> type;
			RETURN_IF_FAILED(desc->GetMediaTypeHandler(&handler));
			RETURN_IF_FAILED(handler->GetCurrentMediaType(&type));

			if (!_rtspUrl.empty())
			{
				WINTRACE(L"MediaSource::Start - propagating RTSP URL to stream[%i]: %s", index, _rtspUrl.c_str());
				_streams[index]->SetRTSPUrl(_rtspUrl.c_str());
			}
			else
			{
				WINTRACE(L"MediaSource::Start - no RTSP URL, stream[%i] will show fallback frame", index);
				}

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

STDMETHODIMP MediaSource::Stop()
{
	WINTRACE(L"MediaSource::Stop");
	winrt::slim_lock_guard lock(_lock);
	RETURN_HR_IF(MF_E_SHUTDOWN, !_queue || !_descriptor);

	wil::unique_prop_variant time;
	RETURN_IF_FAILED(InitPropVariantFromInt64(MFGetSystemTime(), &time));

	for (DWORD i = 0; i < _streams.size(); i++)
	{
		RETURN_IF_FAILED(_streams[i]->Stop());
		RETURN_IF_FAILED(_descriptor->DeselectStream(i));
	}

	RETURN_IF_FAILED(_queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, &time));
	return S_OK;
}

// IMFMediaSourceEx
STDMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** ppAttributes)
{
	WINTRACE(L"MediaSource::GetSourceAttributes");
	RETURN_HR_IF_NULL(E_POINTER, ppAttributes);
	winrt::slim_lock_guard lock(_lock);

	RETURN_IF_FAILED(QueryInterface(IID_PPV_ARGS(ppAttributes)));
	return S_OK;
}

// IMFMediaSource2
STDMETHODIMP MediaSource::SetMediaType(DWORD dwStreamID, IMFMediaType* pMediaType)
{
	WINTRACE(L"MediaSource::SetMediaType dwStreamId:%u pMediaType:%p", dwStreamID, pMediaType);
	RETURN_HR_IF_NULL(E_POINTER, pMediaType);
	winrt::slim_lock_guard lock(_lock);

	TraceMFAttributes(pMediaType, L"MediaType");
	return S_OK;
}

STDMETHODIMP MediaSource::GetStreamAttributes(DWORD dwStreamIdentifier, IMFAttributes** ppAttributes)
{
	WINTRACE(L"MediaSource::GetStreamAttributes dwStreamIdentifier:%u", dwStreamIdentifier);
	RETURN_HR_IF_NULL(E_POINTER, ppAttributes);
	*ppAttributes = nullptr;
	winrt::slim_lock_guard lock(_lock);

	RETURN_HR_IF_MSG(E_FAIL, dwStreamIdentifier >= _streams.size(), "dwStreamIdentifier %u is invalid", dwStreamIdentifier);
	RETURN_IF_FAILED(_streams[dwStreamIdentifier].copy_to(ppAttributes));
	return S_OK;
}

STDMETHODIMP MediaSource::SetD3DManager(IUnknown* pManager)
{
	WINTRACE(L"MediaSource::SetD3DManager pManager:%p", pManager);
	RETURN_HR_IF_NULL(E_POINTER, pManager);
	winrt::slim_lock_guard lock(_lock);

	for (DWORD i = 0; i < _streams.size(); i++)
	{
		RETURN_IF_FAILED(_streams[i]->SetD3DManager(pManager));
	}
	return S_OK;
}

// IMFGetService
STDMETHODIMP MediaSource::GetService(REFGUID siid, REFIID iid, LPVOID* ppvObject)
{
	if (iid == __uuidof(IMFDeviceController) || iid == __uuidof(IMFDeviceController2))
		return MF_E_UNSUPPORTED_SERVICE;

	WINTRACE(L"MediaSource::GetService siid '%s' iid '%s' failed", GUID_ToStringW(siid).c_str(), GUID_ToStringW(iid).c_str());
	RETURN_HR(MF_E_UNSUPPORTED_SERVICE);
}

// IMFSampleAllocatorControl
STDMETHODIMP MediaSource::SetDefaultAllocator(DWORD dwOutputStreamID, IUnknown* pAllocator)
{
	WINTRACE(L"MediaSource::SetDefaultAllocator dwOutputStreamID:%u pAllocator:%p", dwOutputStreamID, pAllocator);
	RETURN_HR_IF_NULL(E_POINTER, pAllocator);
	winrt::slim_lock_guard lock(_lock);

	auto index = GetStreamIndexById(dwOutputStreamID);
	RETURN_HR_IF(E_FAIL, index < 0);

	RETURN_HR_IF_MSG(E_FAIL, index < 0 || (DWORD)index >= _streams.size(), "dwOutputStreamID %u is invalid, index:%i", dwOutputStreamID, index);
	RETURN_HR(_streams[index]->SetAllocator(pAllocator));
}

STDMETHODIMP MediaSource::GetAllocatorUsage(DWORD dwOutputStreamID, DWORD* pdwInputStreamID, MFSampleAllocatorUsage* peUsage)
{
	WINTRACE(L"MediaSource::GetAllocatorUsage dwOutputStreamID:%u pdwInputStreamID:%p peUsage:%p", dwOutputStreamID, pdwInputStreamID, peUsage);
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
static const ULONG KSPROPID_VCam_RtspUrl  = 1;

STDMETHODIMP_(NTSTATUS) MediaSource::KsProperty(PKSPROPERTY property, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaSource::KsProperty len:%u data:%p dataLength:%u", length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, property);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaSource::KsProperty prop:%s", PKSIDENTIFIER_ToString(property, length).c_str());

	// Handle our custom property set: KSPROPSETID_VCam_Config / KSPROPID_VCam_RtspUrl
	if (property->Set == KSPROPSETID_VCam_Config &&
		property->Id   == KSPROPID_VCam_RtspUrl  &&
		(property->Flags & KSPROPERTY_TYPE_SET))
	{
		if (data == nullptr || dataLength < sizeof(wchar_t))
		{
			WINTRACE(L"MediaSource::KsProperty - RTSP URL: invalid buffer");
			return HRESULT_FROM_WIN32(ERROR_INVALID_PARAMETER);
		}

		// Read the wide-string URL from the payload
		const wchar_t* url = static_cast<const wchar_t*>(data);
		ULONG maxChars = dataLength / sizeof(wchar_t);

		// Ensure null-terminated within the buffer
		std::wstring newUrl(url, wcsnlen(url, maxChars));

		WINTRACE(L"MediaSource::KsProperty - RTSP URL received: %s", newUrl.c_str());

		_rtspUrl = std::move(newUrl);

		// Propagate to all streams (in case Start() was already called)
		for (auto& stream : _streams)
		{
			if (stream)
				stream->SetRTSPUrl(_rtspUrl.c_str());
		}

		*bytesReturned = 0;
		return S_OK;
	}

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP_(NTSTATUS) MediaSource::KsMethod(PKSMETHOD method, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaSource::KsMethod len:%u data:%p dataLength:%u", length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, method);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaSource::KsMethod method:%s", PKSIDENTIFIER_ToString(method, length).c_str());

	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP_(NTSTATUS) MediaSource::KsEvent(PKSEVENT evt, ULONG length, LPVOID data, ULONG dataLength, ULONG* bytesReturned)
{
	WINTRACE(L"MediaSource::KsEvent evt:%p len:%u data:%p dataLength:%u", evt, length, data, dataLength);
	RETURN_HR_IF_NULL(E_POINTER, bytesReturned);
	winrt::slim_lock_guard lock(_lock);

	WINTRACE(L"MediaSource::KsEvent event:%s", PKSIDENTIFIER_ToString(evt, length).c_str());
	return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}
