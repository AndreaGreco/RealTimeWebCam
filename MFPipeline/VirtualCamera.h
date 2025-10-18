#pragma once

#include <mfvirtualcamera.h>
#include "VideoPlayer.h"
#include <string>

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
	VideoPlayer* videoPlayer;
	std::wstring _title;
	bool _isRegistered;
	bool _isStarted;
	

public:
	VirtualCamera(VideoPlayer *video);
	~VirtualCamera();

	// Set the friendly name of the virtual camera
	void SetCameraName(const wchar_t* name);

	// Register the virtual camera in the system
	HRESULT RegisterVirtualCamera();

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

