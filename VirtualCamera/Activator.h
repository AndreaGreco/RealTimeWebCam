#pragma once

struct Activator : winrt::implements<Activator, CBaseAttributes<IMFActivate>>
{
public:
	// IMFActivate
	STDMETHOD(ActivateObject(REFIID riid, void** ppv));
	STDMETHOD(ShutdownObject)();
	STDMETHOD(DetachObject)();

public:
	Activator()
	{
		SetBaseAttributesTraceName(L"ActivatorAtts");
	}

	HRESULT Initialize();

private:
	winrt::com_ptr<VCamMediaSource> vcam_media_source;
};

