#pragma once

// {A7B2C3D4-E5F6-47A8-B9C0-D1E2F3A4B5C6}
DEFINE_GUID(IID_IVCamConfiguration, 
    0xa7b2c3d4, 0xe5f6, 0x47a8, 0xb9, 0xc0, 0xd1, 0xe2, 0xf3, 0xa4, 0xb5, 0xc6);

// Interfaccia COM personalizzata per configurare la virtual camera
MIDL_INTERFACE("A7B2C3D4-E5F6-47A8-B9C0-D1E2F3A4B5C6")
IVCamConfiguration : public IUnknown
{
public:
    // Imposta l'URL RTSP da utilizzare
    virtual HRESULT STDMETHODCALLTYPE SetRTSPUrl(
        _In_ LPCWSTR rtspUrl) = 0;
    
    // Ottiene l'URL RTSP corrente
    virtual HRESULT STDMETHODCALLTYPE GetRTSPUrl(
        _Out_ LPWSTR rtspUrl,
        _In_ UINT32 bufferSize) = 0;
};
