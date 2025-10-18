#include "pch.h"
#include "VirtualCamera.h"
#include "Logger.h"
#include <sstream>
#include "VideoPlayer.h"

// ============================================================================
// VirtualCamera Implementation
// ============================================================================

VirtualCamera::VirtualCamera(VideoPlayer *video)
	: _vcam(nullptr)
	, _title(L"RTSP Virtual Camera")
	, _isRegistered(false)
	, _isStarted(false)
{
	this->videoPlayer = video;
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

	// Test: Verifica se il CLSID è registrato
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

	// Start the virtual camera (no callback needed)
	HRESULT hr = _vcam->Start(nullptr);
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

	videoPlayer->sendFrameToVirtualCamera(_isStarted);

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

	videoPlayer->sendFrameToVirtualCamera(_isStarted);

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
	__declspec(dllexport) VirtualCamera* CreateVirtualCamera(VideoPlayer* video)
	{
		return new VirtualCamera(video);
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

