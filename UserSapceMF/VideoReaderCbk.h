#pragma once
#include <d3d9.h>
#include <Dxva2api.h>
#include <shlwapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <mfidl.h>
#include <evr.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Strmiids")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "Dxva2.lib")

// Forward declarations per le funzioni necessarie da MFUtils
template <class T> void SAFE_RELEASE(T** ppT);
template <class T> inline void SAFE_RELEASE(T*& pT);

class VideoReaderCall : public IMFSourceReaderCallback {

private:
    IMFVideoSampleAllocator* pVideoSampleAllocator;
    IMFStreamSink* m_pStreamSink = nullptr;
    IDirect3DDeviceManager9* pD3DManager;
    IMFSourceReader* pReader = nullptr;
    long m_cRef = 1;
    
    // Video format info
    UINT32 m_width;
    UINT32 m_height;
    GUID m_subtype;

public:
    VideoReaderCall(IMFStreamSink* pSink);
    ~VideoReaderCall();

    HRESULT AllocateInternalBuffer(IMFMediaType* SinkMediaType, IMFSourceReader* pReader);

    // IUnknown methods
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMFSourceReaderCallback methods
    STDMETHODIMP OnReadSample(HRESULT hrStatus,
        DWORD dwStreamIndex,
        DWORD dwStreamFlags,
        LONGLONG llTimestamp,
        IMFSample* pSample) override;

    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override;
    STDMETHODIMP OnFlush(DWORD) override;
};

