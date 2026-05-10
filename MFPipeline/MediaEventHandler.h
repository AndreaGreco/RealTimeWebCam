#pragma once

#include <mfplay.h>

class MediaEventHandler : IMFAsyncCallback {
public:
    HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult* pAsyncResult);
    HRESULT STDMETHODCALLTYPE GetParameters(DWORD* pdwFlags, DWORD* pdwQueue);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject);
    ULONG STDMETHODCALLTYPE AddRef(void);
    ULONG STDMETHODCALLTYPE Release(void);
};

