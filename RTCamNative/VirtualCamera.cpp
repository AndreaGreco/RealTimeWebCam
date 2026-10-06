#include "pch.h"
#include "VirtualCamera.h"
#include "Logger.h"
#include <sstream>
#include <shlobj.h>

#pragma comment(lib, "shell32")
#pragma comment(lib, "ole32")

// ============================================================================
// VirtualCamera Implementation
// ============================================================================

VirtualCamera::VirtualCamera()
	: _vcam(nullptr)
	, _title(L"RTSP Virtual Camera")
	, _config{}
	, _persistent(false)
	, _isRegistered(false)
	, _isStarted(false)
{
}

VirtualCamera::~VirtualCamera()
{
	// A persistent camera must outlive this object (and the process): just let go of it.
	if (_persistent)
		Detach();
	else
		UnregisterVirtualCamera();
}

HRESULT VirtualCamera::CreateCamera(const std::wstring& title, bool persistent, IMFVirtualCamera** vcam)
{
	// Convert CLSID to string for sourceId
	std::wstring clsid = GUID_ToStringW(CLSID_VCam);

	// Session lifetime: removed when the app closes. System lifetime: stays registered
	// (and, once started, enabled across app restarts and reboots) until Remove().
	// Either way access is CurrentUser, which needs no elevation (AllUsers would).
	// With the system lifetime, creating a camera with the same source id again opens
	// the existing one, which is how a later run re-attaches to it.
	return MFCreateVirtualCamera(
		MFVirtualCameraType_SoftwareCameraSource,
		persistent ? MFVirtualCameraLifetime_System : MFVirtualCameraLifetime_Session,
		MFVirtualCameraAccess_CurrentUser,        // Only current user can access
		title.c_str(),                            // Friendly name
		clsid.c_str(),                            // Source ID (CLSID of the registered COM server)
		nullptr,                                  // No categories
		0,                                        // Category count
		vcam
	);
}

void VirtualCamera::SetCameraName(const wchar_t* name)
{
	if (name != nullptr)
	{
		_title = name;
	}
}

void VirtualCamera::SetConfig(const VCamConfig& config)
{
	_config = config;
	DebugLog("VirtualCamera::SetConfig - config stored");
}

HRESULT VirtualCamera::RegisterVirtualCamera()
{
	if (_isRegistered)
	{
		DebugLog("VirtualCamera already registered");
		return S_OK;
	}

	DebugLog(_persistent ? "RegisterVirtualCamera - start (persistent)" : "RegisterVirtualCamera - start (session)");

	// Create the virtual camera
	HRESULT hr = CreateCamera(_title, _persistent, &_vcam);

	if (FAILED(hr))
	{
		std::ostringstream oss;
		oss << "MFCreateVirtualCamera failed with HRESULT: 0x" << std::hex << hr;
		DebugLog(oss.str().c_str());
		return hr;
	}

	_isRegistered = true;

	// Set individual attributes on the IMFVirtualCamera store BEFORE Start().
	// IMFVirtualCamera::SetString/SetUINT32 are forwarded by the Frame Server
	// to MediaSource::Initialize(). SetBlob is NOT reliably forwarded.
	auto setAttr = [&](HRESULT h, const char* name) {
		if (FAILED(h)) {
			std::ostringstream oss;
			oss << "VirtualCamera::RegisterVirtualCamera - " << name << " failed: 0x" << std::hex << h;
			DebugLog(oss.str().c_str());
		}
	};

	setAttr(_vcam->SetString(MF_VCAM_RTSP_URL, _config.rtspUrl), "SetString(RTSP_URL)");
	setAttr(_vcam->SetUINT32(MF_VCAM_WIDTH,    _config.width),   "SetUINT32(WIDTH)");
	setAttr(_vcam->SetUINT32(MF_VCAM_HEIGHT,   _config.height),  "SetUINT32(HEIGHT)");
	setAttr(_vcam->SetUINT32(MF_VCAM_FPS_NUM,  _config.fpsNum),  "SetUINT32(FPS_NUM)");
	setAttr(_vcam->SetUINT32(MF_VCAM_FPS_DEN,  _config.fpsDen),  "SetUINT32(FPS_DEN)");
	setAttr(_vcam->SetUINT32(MF_VCAM_OVERLAY,  _config.overlay), "SetUINT32(OVERLAY)");

	// Per-user offline image: always sent (fixed path), the Frame Server polls the file.
	std::wstring offlineImage = OfflineImagePath();
	if (!offlineImage.empty())
		setAttr(_vcam->SetString(MF_VCAM_OFFLINE_IMAGE, offlineImage.c_str()), "SetString(OFFLINE_IMAGE)");

	DebugLog("VirtualCamera::RegisterVirtualCamera - config attributes stored on IMFVirtualCamera");

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

	// Config attributes already set in RegisterVirtualCamera() via SetString/SetUINT32.
	// Start with no callback: the Frame Server will forward them to MediaSource::Initialize().
	HRESULT hr = _vcam->Start(nullptr);
	if (FAILED(hr) && _persistent)
	{
		// Re-attaching to a persistent camera that is still enabled from an earlier run:
		// stop it and start again (which also makes the Frame Server re-read the attrs).
		std::ostringstream oss;
		oss << "IMFVirtualCamera::Start (persistent) failed: 0x" << std::hex << hr << ", retrying after Stop";
		DebugLog(oss.str().c_str());
		_vcam->Stop();
		hr = _vcam->Start(nullptr);
	}
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

HRESULT VirtualCamera::RestartVirtualCamera()
{
	if (!_isRegistered || _vcam == nullptr)
		return E_NOT_VALID_STATE;

	DebugLog("RestartVirtualCamera - Stop + Start to apply new config attributes");
	_vcam->Stop();
	_isStarted = false;
	return StartVirtualCamera();
}

void VirtualCamera::Detach()
{
	if (_vcam)
	{
		DebugLog("VirtualCamera::Detach - releasing the camera, left registered");
		_vcam->Release();
		_vcam = nullptr;
	}
	_isRegistered = false;
	_isStarted = false;
}

std::wstring VirtualCamera::OfflineImagePath()
{
	PWSTR localAppData = nullptr;
	if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppData)))
		return std::wstring();

	std::wstring path(localAppData);
	CoTaskMemFree(localAppData);
	path += L"\\" VCAM_OFFLINE_IMAGE_DIR L"\\" VCAM_OFFLINE_IMAGE_FILE;
	return path;
}

HRESULT VirtualCamera::RemovePersistent(const wchar_t* title)
{
	IMFVirtualCamera* vcam = nullptr;
	HRESULT hr = CreateCamera(title ? title : L"RTSP Virtual Camera", true, &vcam);
	if (SUCCEEDED(hr))
	{
		hr = vcam->Remove();
		vcam->Release();
	}

	std::ostringstream oss;
	oss << "VirtualCamera::RemovePersistent: 0x" << std::hex << hr;
	DebugLog(oss.str().c_str());
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

	__declspec(dllexport) void SetVirtualCameraConfig(VirtualCamera* vcam, const VCamConfig* config)
	{
		if (vcam && config)
		{
			vcam->SetConfig(*config);
		}
	}

	__declspec(dllexport) void SetVCamPersistent(VirtualCamera* vcam, int persistent)
	{
		if (vcam)
		{
			vcam->SetPersistent(persistent != 0);
		}
	}

	__declspec(dllexport) int RestartVCam(VirtualCamera* vcam)
	{
		if (vcam)
		{
			HRESULT hr = vcam->RestartVirtualCamera();
			return SUCCEEDED(hr) ? 0 : -1;
		}
		return -1;
	}

	__declspec(dllexport) void DetachVCam(VirtualCamera* vcam)
	{
		if (vcam)
		{
			vcam->Detach();
		}
	}

	// Removes the persistent camera left by an earlier run. Returns 0 on success.
	__declspec(dllexport) int RemovePersistentVCam(LPCWSTR name)
	{
		HRESULT hr = VirtualCamera::RemovePersistent(name);
		return SUCCEEDED(hr) ? 0 : (int)hr;
	}

	// Full path of this user's offline image (see Shared/VCamConfig.h). Returns the
	// length written, or 0 if the buffer is too small / LocalAppData can't be resolved.
	__declspec(dllexport) int VCam_GetOfflineImagePath(LPWSTR buffer, int cch)
	{
		if (!buffer || cch <= 0)
			return 0;

		std::wstring path = VirtualCamera::OfflineImagePath();
		if (path.empty() || (int)path.size() >= cch)
			return 0;

		wcscpy_s(buffer, cch, path.c_str());
		return (int)path.size();
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

