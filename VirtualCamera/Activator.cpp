#include "pch.h"
#include "Undocumented.h"
#include "Tools.h"
#include "EnumNames.h"
#include "MFTools.h"
#include "MediaStream.h"
#include "MediaSource.h"
#include "Activator.h"

HRESULT Activator::Initialize()
{
	_source = winrt::make_self<MediaSource>();
	RETURN_IF_FAILED(SetUINT32(MF_VIRTUALCAMERA_PROVIDE_ASSOCIATED_CAMERA_SOURCES, 1));
	RETURN_IF_FAILED(SetGUID(MFT_TRANSFORM_CLSID_Attribute, CLSID_VCam));
	RETURN_IF_FAILED(_source->Initialize(this));
	return S_OK;
}

// IMFActivate
STDMETHODIMP Activator::ActivateObject(REFIID riid, void** ppv)
{
	WINTRACE(L"Activator::ActivateObject '%s'", GUID_ToStringW(riid).c_str());
	RETURN_HR_IF_NULL(E_POINTER, ppv);
	*ppv = nullptr;

	// use undoc'd frame server property
	UINT32 pid = 0;
	if (SUCCEEDED(GetUINT32(MF_FRAMESERVER_CLIENTCONTEXT_CLIENTPID, &pid)) && pid)
	{
		auto name = GetProcessName(pid);
		if (!name.empty())
		{
			WINTRACE(L"Activator::ActivateObject client process '%s'", name.c_str());
		}
	}

	// At this point the Frame Server has finished writing all attributes on the
	// Activator (URL, width, height, fps). Pass them to the source now — this is
	// the correct moment, NOT inside Initialize() which is called too early.
	HRESULT hrCfg = _source->SetupCameraSettings(this);
	if (FAILED(hrCfg))
		WINTRACE(L"Activator::ActivateObject - SetupCameraSettings failed: 0x%08X (no RTSP, black frames)", hrCfg);

	RETURN_IF_FAILED_MSG(_source->QueryInterface(riid, ppv), "Activator::ActivateObject failed on IID %s", GUID_ToStringW(riid).c_str());
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
	_source = nullptr;
	return S_OK;
}
