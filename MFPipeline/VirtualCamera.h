#pragma once
ref class VirtualCamera
{
	/*
	HRESULT RegisterVirtualCamera()
	{
		auto clsid = GUID_ToStringW(CLSID_VCam);
		RETURN_IF_FAILED_MSG(MFCreateVirtualCamera(
			MFVirtualCameraType_SoftwareCameraSource,
			MFVirtualCameraLifetime_Session,
			MFVirtualCameraAccess_CurrentUser,
			_title,
			clsid.c_str(),
			nullptr,
			0,
			&_vcam),
			"Failed to create virtual camera");

		WINTRACE(L"RegisterVirtualCamera '%s' ok", clsid.c_str());
		RETURN_IF_FAILED_MSG(_vcam->Start(nullptr), "Cannot start VCam");
		WINTRACE(L"VCam was started");
		return S_OK;
	}

	HRESULT UnregisterVirtualCamera()
	{
		if (!_vcam)
			return S_OK;

		// NOTE: we don't call Shutdown or this will cause 2 Shutdown calls to the media source and will prevent proper removing
		//auto hr = _vcam->Shutdown();
		//WINTRACE(L"Shutdown VCam hr:0x%08X", hr);

		auto hr = _vcam->Remove();
		WINTRACE(L"Remove VCam hr:0x%08X", hr);
		return S_OK;
	}
	*/
};

