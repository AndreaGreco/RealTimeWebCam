#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "MediaStream.h"
#include "VCamMediaSource.h"
#include "Activator.h"

HRESULT Activator::Initialize()
{
	HRESULT hr;

	vcam_media_source = winrt::make_self<VCamMediaSource>();
	hr = SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1);
	RETURN_IF_FAILED(hr);

	hr = SetGUID(MFT_TRANSFORM_CLSID_Attribute, CLSID_VCam);
	RETURN_IF_FAILED(hr);

	// hr = vcam_media_source->Initialize(this);
	RETURN_IF_FAILED(hr);

	return S_OK;
}

// IMFActivate
STDMETHODIMP Activator::ActivateObject(REFIID riid, void** ppv)
{
	HRESULT hr;

	WINTRACE(L"Activator::ActivateObject '%s'", GUID_ToStringW(riid).c_str());
	RETURN_HR_IF_NULL(E_POINTER, ppv);
	*ppv = nullptr;

	// use undoc'd frame server property
	UINT32 pid = 0;
	hr = GetUINT32(MF_FRAMESERVER_CLIENTCONTEXT_CLIENTPID, &pid);
	if (SUCCEEDED(hr) && pid)
	{
		auto name = GetProcessName(pid);
		if (!name.empty())
		{
			WINTRACE(L"Activator::ActivateObject client process '%s'", name.c_str());
		}
	}

	LOG_IF_FAILED(vcam_media_source->SetupCameraSettings(this));
	hr = vcam_media_source->Initialize(this);
	if (FAILED(hr))
		WINTRACE(L"Activator::ActivateObject - Initialize failed: 0x%08X (no RTSP, black frames)", hr);

	hr = vcam_media_source->QueryInterface(riid, ppv);
	RETURN_IF_FAILED_MSG(hr, "Activator::ActivateObject failed on IID %s", GUID_ToStringW(riid).c_str());

	return S_OK;
}

STDMETHODIMP Activator::ShutdownObject()
{
	WINTRACE(L"Activator::ShutdownObject");
	return S_OK;
}

STDMETHODIMP Activator::DetachObject()
{
	WINTRACE(L"Activator::DetachObject");
	vcam_media_source = nullptr;
	return S_OK;
}
