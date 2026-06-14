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

// Live diagnostics for the preview path, polled from the C# UI. Mirrored
// byte-for-byte by RTVirtualCamera/VideoPlayerWrapper.cs::PreviewStats.
struct PreviewStats
{
    unsigned long long receivedFrames; // frames delivered by the source reader
    unsigned long long renderedFrames; // frames actually sent to the EVR
    unsigned long long droppedFrames;  // frames skipped by the adaptive drop
    double driftMs;       // (wall elapsed - media elapsed) since the first frame:
                          //   ~flat  => latency is a fixed offset
                          //   rising => the pipeline is accumulating delay
    double lastRenderMs;  // cost of the last CPU->GPU copy + ProcessSample (ms)
};

class VideoReaderCall : public IMFSourceReaderCallback {

private:
    IMFVideoSampleAllocator* pVideoSampleAllocator = nullptr;
    IMFStreamSink* m_pStreamSink = nullptr;
    IDirect3DDeviceManager9* pD3DManager = nullptr;
    IMFSourceReader* pReader = nullptr;
    IMFPresentationClock* m_pClock = nullptr;
    long m_cRef = 1;
    CRITICAL_SECTION m_lock;
    long m_shutdown = 0;
    
    // Video format info
    UINT32 m_width;
    UINT32 m_height;
    GUID m_subtype;

    // Live stats, read via GetStats() by the UI thread (under m_lock).
    unsigned long long m_statReceived = 0;
    unsigned long long m_statRendered = 0;
    unsigned long long m_statDropped = 0;
    double m_driftMs = 0.0;
    bool m_driftBaseSet = false;
    ULONGLONG m_driftBaseTickMs = 0;
    LONGLONG m_driftBasePtsMs = 0;

    // Adaptive render pacing: wall-clock of the last rendered frame and how long
    // that render cost. Frames arriving within the budget are dropped (resync to
    // latest) instead of letting the source buffer a backlog.
    ULONGLONG m_lastRenderTick = 0;
    ULONGLONG m_lastRenderCostMs = 0;

    // Helper functions for format-specific buffer copying
    HRESULT CopyNV12Buffer(IMF2DBuffer* p2DBuffer, BYTE* pbSrcData, DWORD cbSrcLength);
    HRESULT CopyYUY2Buffer(IMF2DBuffer* p2DBuffer, BYTE* pbSrcData, DWORD cbSrcLength);
    HRESULT CopyRGB32Buffer(IMF2DBuffer* p2DBuffer, BYTE* pbSrcData, DWORD cbSrcLength);

public:
    VideoReaderCall(IMFStreamSink* pSink);
    ~VideoReaderCall();

    HRESULT AllocateInternalBuffer(IMFMediaType* SinkMediaType, IMFSourceReader* pReader);
    void SetPresentationClock(IMFPresentationClock* pClock);
    void GetStats(PreviewStats* pStats);
    void Shutdown();

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

