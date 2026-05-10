#include "pch.h"
#include "VirtualCamera.h"
#include "Logger.h"
#include <sstream>
#include <ks.h>

// ============================================================================
// VirtualCamera Implementation
// ============================================================================

// KsProperty set GUID used by SendCameraProperty (app→FrameServer) and received
// in MediaSource::KsProperty() inside VCamSampleSource.dll.
// Property ID 1 = RTSP URL (wide string payload).
// Must match the same GUID in VirtualCamera/MediaSource.cpp.
static const GUID KSPROPSETID_VCam_Config = { 0xc1d2e3f4, 0xa5b6, 0x47c8, { 0xd9, 0xea, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x60 } };
static const ULONG KSPROPID_VCam_RtspUrl  = 1;

class VirtualCamera; // forward

class VcamStartCallback : public IMFAsyncCallback
{
private:
	LONG m_refCount;
	VirtualCamera* _owner; // non-owning: callback lifetime < VirtualCamera lifetime


public:
	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv)
	{
		if (riid == IID_IUnknown || riid == __uuidof(IMFAsyncCallback))
		{
			*ppv = static_cast<IMFAsyncCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef()
	{
		return InterlockedIncrement(&m_refCount);
	}
	STDMETHODIMP_(ULONG) Release()
	{
		ULONG count = InterlockedDecrement(&m_refCount);
		if (count == 0)
		{
			delete this;
		}
		return count;
	}
	// IMFAsyncCallback methods
	STDMETHODIMP GetParameters(DWORD* pdwFlags, DWORD* pdwQueue)
	{
		return E_NOTIMPL;
	}
	STDMETHODIMP Invoke(IMFAsyncResult* pAsyncResult)
	{
		if (!pAsyncResult)
		{
			DebugLog("VcamStartCallback::Invoke - pAsyncResult is null");
			return E_POINTER;
		}

		// Get the event from the async result
		IMFMediaEvent* pEvent = nullptr;
		HRESULT hr = pAsyncResult->GetObject((IUnknown**)&pEvent);
		if (FAILED(hr) || !pEvent)
		{
			DebugLog("VcamStartCallback::Invoke - Failed to get event from async result");
			return hr;
		}

		// Get the event type
		MediaEventType eventType;
		hr = pEvent->GetType(&eventType);
		if (FAILED(hr))
		{
			pEvent->Release();
			DebugLog("VcamStartCallback::Invoke - Failed to get event type");
			return hr;
		}

		// Handle the event based on type
		switch (eventType)
		{
		case MEExtendedType:
		{
			// Get the extended type GUID
			GUID extendedType;
			hr = pEvent->GetExtendedType(&extendedType);
			if (SUCCEEDED(hr))
			{
				// Log the extended event types
				if (extendedType == MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_INITIALIZE)
				{
					DebugLog("VcamEvent: MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_INITIALIZE - Custom media source initialized");
					// Frame Server has loaded VCamSampleSource.dll and called Initialize().
					// NOW we can push the RTSP URL cross-process via KsProperty.
					if (_owner)
					{
						_owner->SendRTSPUrl();
					}
				}
				else if (extendedType == MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_START)
				{
					DebugLog("VcamEvent: MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_START - Stream(s) started by application");
				}
				else if (extendedType == MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_STOP)
				{
					DebugLog("VcamEvent: MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_STOP - All streams stopped by application");
				}
				else if (extendedType == MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_UNINITIALIZE)
				{
					DebugLog("VcamEvent: MF_FRAMESERVER_VCAMEVENT_EXTENDED_SOURCE_UNINITIALIZE - Custom media source uninitialized");
				}
				else if (extendedType == MF_FRAMESERVER_VCAMEVENT_EXTENDED_PIPELINE_SHUTDOWN)
				{
					DebugLog("VcamEvent: MF_FRAMESERVER_VCAMEVENT_EXTENDED_PIPELINE_SHUTDOWN - Virtual camera pipeline shutdown");
				}
				else if (extendedType == MF_FRAMESERVER_VCAMEVENT_EXTENDED_CUSTOM_EVENT)
				{
					DebugLog("VcamEvent: MF_FRAMESERVER_VCAMEVENT_EXTENDED_CUSTOM_EVENT - Custom event from media source");
				}
				else
				{
					// Log unknown extended type
					char logMsg[256];
					snprintf(logMsg, sizeof(logMsg),
						"VcamEvent: Unknown extended type {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
						extendedType.Data1, extendedType.Data2, extendedType.Data3,
						extendedType.Data4[0], extendedType.Data4[1], extendedType.Data4[2], extendedType.Data4[3],
						extendedType.Data4[4], extendedType.Data4[5], extendedType.Data4[6], extendedType.Data4[7]);
					DebugLog(logMsg);
				}
			}
			break;
		}
		case MEError:
		{
			// Get the error status
			HRESULT hrStatus;
			hr = pEvent->GetStatus(&hrStatus);
			if (SUCCEEDED(hr))
			{
				std::ostringstream oss;
				oss << "VcamEvent: MEError - Error occurred with HRESULT: 0x" << std::hex << hrStatus;
				DebugLog(oss.str().c_str());
			}
			else
			{
				DebugLog("VcamEvent: MEError - Error occurred (failed to get status)");
			}
			break;
		}
		default:
		{
			// Log other event types
			char logMsg[128];
			snprintf(logMsg, sizeof(logMsg), "VcamEvent: Unhandled event type: %d", eventType);
			DebugLog(logMsg);
			break;
		}
		}

		pEvent->Release();
		return S_OK;
	}
	VcamStartCallback(VirtualCamera* owner) : m_refCount(1), _owner(owner) {}
};

VirtualCamera::VirtualCamera()
	: _vcam(nullptr)
	, _title(L"RTSP Virtual Camera")
	, _isRegistered(false)
	, _isStarted(false)
{
}

VirtualCamera::~VirtualCamera()
{
	UnregisterVirtualCamera();
}

void VirtualCamera::SetCameraName(const wchar_t* name)
{
	if (name != nullptr)
	{
		_title = name;
	}
}

void VirtualCamera::SetRTSPUrl(std::wstring url)
{
	_rtspUrl = url;
	DebugLog("VirtualCamera::SetRTSPUrl - URL stored");
}

void VirtualCamera::SendRTSPUrl()
{
	// Called from VcamStartCallback after SOURCE_INITIALIZE: the Frame Server has loaded
	// VCamSampleSource.dll and is ready to receive KsProperty calls.
	// SendCameraProperty routes to MediaSource::KsProperty() cross-process.
	if (_vcam == nullptr || _rtspUrl.empty())
	{
		DebugLog("VirtualCamera::SendRTSPUrl - skipped (no vcam or empty URL)");
		return;
	}

	const wchar_t* url   = _rtspUrl.c_str();
	ULONG          bytes = (ULONG)((wcslen(url) + 1) * sizeof(wchar_t));
	ULONG          written = 0;

	HRESULT hr = _vcam->SendCameraProperty(
		KSPROPSETID_VCam_Config,      // property set GUID
		KSPROPID_VCam_RtspUrl,        // property ID = 1
		KSPROPERTY_TYPE_SET,          // SET operation
		nullptr, 0,                   // no extra payload header
		(void*)url, bytes,            // the URL as wchar_t buffer
		&written
	);

	if (SUCCEEDED(hr))
	{
		DebugLog("VirtualCamera::SendRTSPUrl - URL sent successfully via SendCameraProperty");
	}
	else
	{
		std::ostringstream oss;
		oss << "VirtualCamera::SendRTSPUrl - SendCameraProperty failed: 0x" << std::hex << hr;
		DebugLog(oss.str().c_str());
	}
}

HRESULT VirtualCamera::RegisterVirtualCamera()
{
	if (_isRegistered)
	{
		DebugLog("VirtualCamera already registered");
		return S_OK;
	}

	DebugLog("RegisterVirtualCamera - start");

	// Convert CLSID to string for sourceId
	std::wstring clsid = GUID_ToStringW(CLSID_VCam);

	// Create the virtual camera
	HRESULT hr = MFCreateVirtualCamera(
		MFVirtualCameraType_SoftwareCameraSource,
		MFVirtualCameraLifetime_Session,          // Session lifetime (removed when app closes)
		MFVirtualCameraAccess_CurrentUser,        // Only current user can access
		_title.c_str(),                            // Friendly name
		clsid.c_str(),                            // Source ID (CLSID of the registered COM server)
		nullptr,                                  // No categories
		0,                                        // Category count
		&_vcam
	);

	if (FAILED(hr))
	{
		std::ostringstream oss;
		oss << "MFCreateVirtualCamera failed with HRESULT: 0x" << std::hex << hr;
		DebugLog(oss.str().c_str());
		return hr;
	}

	_isRegistered = true;

	// Log success with CLSID
	char clsidStr[128];
	snprintf(clsidStr, sizeof(clsidStr),
		"VirtualCamera registered with CLSID: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		CLSID_VCam.Data1, CLSID_VCam.Data2, CLSID_VCam.Data3,
		CLSID_VCam.Data4[0], CLSID_VCam.Data4[1], CLSID_VCam.Data4[2], CLSID_VCam.Data4[3],
		CLSID_VCam.Data4[4], CLSID_VCam.Data4[5], CLSID_VCam.Data4[6], CLSID_VCam.Data4[7]);
	DebugLog(clsidStr);

	return S_OK;
}

HRESULT VirtualCamera::StartVirtualCamera()
{
	if (!_isRegistered || _vcam == nullptr)
	{
		DebugLog("VirtualCamera not registered, cannot start");
		return E_NOT_VALID_STATE;
	}

	if (_isStarted)
	{
		DebugLog("VirtualCamera already started");
		return S_OK;
	}

	DebugLog("StartVirtualCamera - start");

	// Test: Verifica se il CLSID � registrato
	IUnknown* pTest = nullptr;
	HRESULT hrTest = CoCreateInstance(CLSID_VCam, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&pTest);
	if (FAILED(hrTest))
	{
		std::ostringstream oss;
		oss << "CoCreateInstance test failed with HRESULT: 0x" << std::hex << hrTest;
		DebugLog(oss.str().c_str());
		
		if (hrTest == 0x80040154) // REGDB_E_CLASSNOTREG
		{
			DebugLog("ERROR: VCamSampleSource.dll is NOT registered!");
			DebugLog("Run: regsvr32 VCamSampleSource.dll");
		}
		else if (hrTest == E_ACCESSDENIED)
		{
			DebugLog("ERROR: Access denied when creating COM object");
			DebugLog("Try running as Administrator");
		}
	}
	else
	{
		DebugLog("CoCreateInstance test succeeded - DLL is registered");
		pTest->Release();
	}

	// Start the virtual camera with callback to push RTSP URL after SOURCE_INITIALIZE
	VcamStartCallback* startCallback = new VcamStartCallback(this);
	HRESULT hr = _vcam->Start(startCallback);
	if (FAILED(hr))
	{
		std::ostringstream oss;
		oss << "IMFVirtualCamera::Start failed with HRESULT: 0x" << std::hex << hr;
		DebugLog(oss.str().c_str());
		
		if (hr == E_ACCESSDENIED)
		{
			DebugLog("E_ACCESSDENIED: Likely DLL not registered or permission issue");
		}
		
		return hr;
	}

	_isStarted = true;
	DebugLog("VirtualCamera started successfully");

	return S_OK;
}

HRESULT VirtualCamera::StopVirtualCamera()
{
	if (!_isStarted || _vcam == nullptr)
	{
		DebugLog("VirtualCamera not started, nothing to stop");
		return S_OK;
	}

	DebugLog("StopVirtualCamera - start");

	HRESULT hr = _vcam->Stop();
	if (FAILED(hr))
	{
		std::ostringstream oss;
		oss << "IMFVirtualCamera::Stop failed with HRESULT: 0x" << std::hex << hr;
		DebugLog(oss.str().c_str());
		// Continue anyway
	}

	_isStarted = false;
	DebugLog("VirtualCamera stopped");

	return S_OK;
}

HRESULT VirtualCamera::UnregisterVirtualCamera()
{
	if (!_isRegistered || _vcam == nullptr)
	{
		return S_OK;
	}

	DebugLog("UnregisterVirtualCamera - start");

	// Stop first if started
	if (_isStarted)
	{
		StopVirtualCamera();
	}

	// NOTE: We don't call Shutdown because it will cause 2 Shutdown calls 
	// to the media source and will prevent proper removal
	// Remove the virtual camera from the system
	HRESULT hr = _vcam->Remove();
	if (FAILED(hr))
	{
		std::ostringstream oss;
		oss << "IMFVirtualCamera::Remove failed with HRESULT: 0x" << std::hex << hr;
		DebugLog(oss.str().c_str());
	}
	else
	{
		DebugLog("VirtualCamera removed successfully");
	}

	// Release the interface
	if (_vcam)
	{
		_vcam->Release();
		_vcam = nullptr;
	}

	_isRegistered = false;
	_isStarted = false;

	return hr;
}

HRESULT VirtualCamera::GetMediaSource(IMFMediaSource** ppMediaSource)
{
	if (!_isRegistered || _vcam == nullptr)
	{
		DebugLog("VirtualCamera not registered, cannot get media source");
		return E_NOT_VALID_STATE;
	}

	if (ppMediaSource == nullptr)
	{
		return E_POINTER;
	}

	HRESULT hr = _vcam->GetMediaSource(ppMediaSource);
	if (FAILED(hr))
	{
		std::ostringstream oss;
		oss << "IMFVirtualCamera::GetMediaSource failed with HRESULT: 0x" << std::hex << hr;
		DebugLog(oss.str().c_str());
	}

	return hr;
}

// ============================================================================
// C-style interface for C# interop
// ============================================================================

extern "C" {
	__declspec(dllexport) VirtualCamera* CreateVirtualCamera()
	{
		return new VirtualCamera();
	}

	__declspec(dllexport) void DestroyVirtualCamera(VirtualCamera* vcam)
	{
		if (vcam)
		{
			delete vcam;
		}
	}

	__declspec(dllexport) void SetVirtualCameraName(VirtualCamera* vcam, LPWSTR name)
	{
		if (vcam && name)
		{
			vcam->SetCameraName(name);
		}
	}

	__declspec(dllexport) void SetVirtualCameraRTSPUrl(VirtualCamera* vcam, LPWSTR url)
	{
		if (vcam && url)
		{
			vcam->SetRTSPUrl(url);
		}
	}

	__declspec(dllexport) int RegisterVCam(VirtualCamera* vcam)
	{
		if (vcam)
		{
			HRESULT hr = vcam->RegisterVirtualCamera();
			return SUCCEEDED(hr) ? 0 : -1;
		}
		return -1;
	}

	__declspec(dllexport) int StartVCam(VirtualCamera* vcam)
	{
		if (vcam)
		{
			HRESULT hr = vcam->StartVirtualCamera();
			return SUCCEEDED(hr) ? 0 : -1;
		}
		return -1;
	}

	__declspec(dllexport) int StopVCam(VirtualCamera* vcam)
	{
		if (vcam)
		{
			HRESULT hr = vcam->StopVirtualCamera();
			return SUCCEEDED(hr) ? 0 : -1;
		}
		return -1;
	}

	__declspec(dllexport) int UnregisterVCam(VirtualCamera* vcam)
	{
		if (vcam)
		{
			HRESULT hr = vcam->UnregisterVirtualCamera();
			return SUCCEEDED(hr) ? 0 : -1;
		}
		return -1;
	}

	__declspec(dllexport) bool IsVCamRegistered(VirtualCamera* vcam)
	{
		if (vcam)
		{
			return vcam->IsRegistered();
		}
		return false;
	}

	__declspec(dllexport) bool IsVCamStarted(VirtualCamera* vcam)
	{
		if (vcam)
		{
			return vcam->IsStarted();
		}
		return false;
	}
}

