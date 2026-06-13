#pragma once
#include "pch.h"
#include <mfreadwrite.h>
#include <functional>
#include <atomic>

// Async IMFSourceReaderCallback for RTSP streaming.
//
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
	RtspReaderCallback() : _cRef(1), _shutdown(false) {}
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
	STDMETHODIMP OnFlush(DWORD) override { return S_OK; }

	// Start the async read chain. Call once after creating the source reader.
	HRESULT BeginRead(IMFSourceReader* reader);

	// Sets the handler fired once when the stream breaks (read error or end of stream).
	// Must be called before BeginRead(); not changed afterward, so it is read without a lock.
	void SetBrokenHandler(std::function<void()> handler) { _onBroken = std::move(handler); }

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
	wil::com_ptr_nothrow<IMFSourceReader> _reader;
	wil::com_ptr_nothrow<IMFSample>       _latestSample;
	bool _shutdown;
	std::function<void()> _onBroken;        // fired once on disconnect; set before BeginRead
	std::atomic<bool> _brokenSignaled{ false };
};
