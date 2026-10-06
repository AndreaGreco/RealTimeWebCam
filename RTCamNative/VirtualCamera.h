#pragma once

#include <mfvirtualcamera.h>
#include <string>
#include "..\Shared\VCamConfig.h"

// CLSID della Virtual Camera (deve corrispondere a quello in VCamSampleSource)
// {3CAD447D-F283-4AF4-A3B2-6F5363309F52}
extern "C" {
	static const GUID CLSID_VCam = { 0x3cad447d, 0xf283, 0x4af4, {0xa3, 0xb2, 0x6f, 0x53, 0x63, 0x30, 0x9f, 0x52} };
}

// Helper function to convert GUID to string
inline std::wstring GUID_ToStringW(const GUID& guid)
{
	wchar_t buffer[64];
	swprintf_s(buffer, L"{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
		guid.Data1, guid.Data2, guid.Data3,
		guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
		guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
	return std::wstring(buffer);
}

class VirtualCamera
{
private:
IMFVirtualCamera* _vcam;
std::wstring _title;
VCamConfig _config;
bool _persistent;
bool _isRegistered;
bool _isStarted;

	static HRESULT CreateCamera(const std::wstring& title, bool persistent, IMFVirtualCamera** vcam);

public:
	VirtualCamera();
	~VirtualCamera();

	// Set the friendly name of the virtual camera
	void SetCameraName(const wchar_t* name);

	// Set the full configuration (URL, resolution, fps, format) to be sent to
	// the Frame Server. Must be called before Register()+Start().
	void SetConfig(const VCamConfig& config);

	// Persistent = MFVirtualCameraLifetime_System: the camera stays registered (and
	// enabled) after this process exits, until RemovePersistent() /
	// UnregisterVirtualCamera(). Default = MFVirtualCameraLifetime_Session (removed
	// when the app closes). Must be called before RegisterVirtualCamera().
	void SetPersistent(bool persistent) { _persistent = persistent; }
	bool IsPersistent() const { return _persistent; }

	// Register the virtual camera in the system
	HRESULT RegisterVirtualCamera();

	// Re-applies the config attributes and restarts a running camera so the Frame
	// Server re-reads them (Stop + Start). Used when re-attaching to a persistent
	// camera whose geometry changed.
	HRESULT RestartVirtualCamera();

	// Releases the IMFVirtualCamera WITHOUT stopping or removing it: a persistent
	// camera stays registered and enabled. No-op semantics for the system otherwise.
	void Detach();

	// Removes a persistent camera left registered by an earlier run (opened by
	// re-creating it with the system lifetime, then IMFVirtualCamera::Remove()).
	static HRESULT RemovePersistent(const wchar_t* title);

	// %LOCALAPPDATA%\RTVirtualCamera\offline-image.png for the calling user (empty on
	// failure). Sent to the Frame Server as MF_VCAM_OFFLINE_IMAGE.
	static std::wstring OfflineImagePath();

	// Start the virtual camera (makes it available to apps)
	HRESULT StartVirtualCamera();

	// Stop the virtual camera
	HRESULT StopVirtualCamera();

	// Unregister and remove the virtual camera from the system
	HRESULT UnregisterVirtualCamera();

	// Get the IMFMediaSource for this virtual camera
	HRESULT GetMediaSource(IMFMediaSource** ppMediaSource);

	// Check if camera is registered
	bool IsRegistered() const { return _isRegistered; }

	// Check if camera is started
	bool IsStarted() const { return _isStarted; }
};

