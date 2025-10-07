#pragma once
#include <shlwapi.h>
#include <mfidl.h>

class CSourceOpenMonitor : public IMFSourceOpenMonitor
{
public:
    CSourceOpenMonitor();

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();
    STDMETHODIMP OnSourceEvent(IMFMediaEvent* pEvent);

private:
    long m_cRef;
};

