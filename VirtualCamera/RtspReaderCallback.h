#pragma once
#include "pch.h"
#include <mfreadwrite.h>

// Async IMFSourceReaderCallback for RTSP streaming.
//
// Pattern (Option B):
//   InitializeRTSPReader() creates the source reader with this callback and calls BeginRead().
//   OnReadSample() fires on an MF work-queue thread when a decoded frame is ready:
//     - stores the sample (AddRef'd) in _latestSample
//     - immediately re-issues ReadSample to keep the pipeline full
//   MediaStream::RequestSample() calls TakeLatestSample() — never blocks.
//   If no frame is ready yet (pipeline warming up) → TakeLatestSample returns S_FALSE →
//   MediaStream falls back to FrameGenerator.
//
// Thread safety: _latestSample is protected by _lock (slim_mutex = SRWLOCK).
//   Shutdown() disarms the callback so no further samples are stored or requested.

class RtspReaderCallback : public IMFSourceReaderCallback
{
public:
    RtspReaderCallback()  : _cRef(1), _shutdown(false) {}
    virtual ~RtspReaderCallback() = default;

    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    STDMETHODIMP_(ULONG) AddRef()  override;
    STDMETHODIMP_(ULONG) Release() override;

    // IMFSourceReaderCallback
    STDMETHODIMP OnReadSample(HRESULT hrStatus,
                              DWORD   dwStreamIndex,
                              DWORD   dwStreamFlags,
                              LONGLONG llTimestamp,
                              IMFSample* pSample) override;
    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }
    STDMETHODIMP OnFlush(DWORD) override                 { return S_OK; }

    // Start the async read chain. Call once after creating the source reader.
    HRESULT BeginRead(IMFSourceReader* reader);

    // Reconfigures the source reader's output type. Called from MediaStream::Start()
    // when the consumer (Zoom) negotiates a format that differs from the initial NV12.
    HRESULT SetOutputMediaType(const GUID& subtype, UINT32 width, UINT32 height);

    // Returns the latest frame and clears it. Returns S_FALSE if no frame available yet.
    // Called by MediaStream::RequestSample() on the Frame Server thread.
    HRESULT TakeLatestSample(IMFSample** ppSample);

    // Stop the read chain and discard any buffered frame.
    void Shutdown();

private:
    long  _cRef;
    winrt::slim_mutex _lock;
    wil::com_ptr_nothrow<IMFSourceReader> _reader;   // held under _lock
    wil::com_ptr_nothrow<IMFSample>       _latestSample;
    bool _shutdown;
};
