#include "pch.h"
#include "CSourceOpenMonitor.h"
#include "Logger.h"
#include <stdio.h>

CSourceOpenMonitor::CSourceOpenMonitor() : m_cRef(1)
{
}

STDMETHODIMP CSourceOpenMonitor::QueryInterface(REFIID riid, void** ppv)
{
    static const QITAB qit[] =
    {
        QITABENT(CSourceOpenMonitor, IMFSourceOpenMonitor),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

STDMETHODIMP_(ULONG) CSourceOpenMonitor::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) CSourceOpenMonitor::Release()
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
    {
        delete this;
    }
    // For thread safety, return a temporary variable.
    return cRef;
}

STDMETHODIMP CSourceOpenMonitor::OnSourceEvent(IMFMediaEvent* pEvent)
{
    MediaEventType eventType = MEUnknown;   // Event type
    HRESULT hrStatus = S_OK;                // Event status

    // Get the event type.
    HRESULT hr = pEvent->GetType(&eventType);

    // Get the event status. If the operation that triggered the event
    // did not succeed, the status is a failure code.
    if (SUCCEEDED(hr))
    {
        hr = pEvent->GetStatus(&hrStatus);
    }

    if (FAILED(hrStatus))
    {
        hr = hrStatus;
    }

    if (SUCCEEDED(hr))
    {
        // Switch on the event type.
        switch (eventType)
        {
        case MEConnectStart:
            // The application does something. (Not shown.)
            DebugLog("Connecting...\n");
            break;

        case MEConnectEnd:
            // The application does something. (Not shown.)
            DebugLog("Connect End.\n");
            break;
        case MEBufferingStarted:
            // The application does something. (Not shown.)
            DebugLog("MEBufferingStarted\n");
            break;
        case MEBufferingStopped:
            // The application does something. (Not shown.)
            DebugLog("MEBufferingStopped\n");
            break;
		default:
            char buff[256];
            snprintf(buff, 256, "Unknown event: %d\n", eventType);
			DebugLog(buff);
			break;
        }
    }
    else
    {
        // Event failed.
        // The application handled a failure. (Not shown.)
    }
    return S_OK;
}
