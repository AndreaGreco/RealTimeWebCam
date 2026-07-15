#pragma once
#include "pch.h"
#include "RtspReaderCallback.h"
#include "..\Shared\VCamConfig.h"
#include <memory>
#include <vector>
#include <thread>
#include <atomic>

struct CameraSessionConfig
{
	bool valid = false;
	wchar_t rtspUrl[512] = {};
	UINT32 width = 0;
	UINT32 height = 0;
	UINT32 fpsNum = 30;
	UINT32 fpsDen = 1;
	GUID format = MFVideoFormat_NV12;
	UINT64 generation = 0;
};

struct RtspFrameSnapshot
{
	bool valid = false;
	UINT32 stride = 0;
	LONGLONG sampleTime = 0;
	LONGLONG sampleDuration = 0;
	UINT64 generation = 0;
	// Identity of the underlying decoded frame (RtspReaderCallback::_latestFrameSeq).
	// Unchanged across calls that re-serve the same cached frame; MediaStream uses
	// this to tell a fresh RTSP frame from a duplicate re-serve for its stats.
	UINT64 frameSeq = 0;
	// Holds the decoded RTSP sample directly (GPU texture or system-memory buffer).
	// No pixel copy happens here — MediaStream::CopyRtspFrame copies straight from
	// this sample's buffer into the consumer's sample (GPU->GPU when possible).
	wil::com_ptr_nothrow<IMFSample> sample;
};

enum class RtspSessionState
{
	Stopped,
	Starting,
	Running,
	Reconnecting, // stream broke; reconnect thread is retrying (synthetic frames shown meanwhile)
	Failed,
};

struct IRtspSessionManager
{
	virtual ~IRtspSessionManager() = default;
	virtual HRESULT Start(const CameraSessionConfig& config) = 0;
	virtual HRESULT Stop(UINT64 generation) = 0;
	virtual bool IsRunning() const = 0;
	virtual UINT64 GetGeneration() const = 0;
	virtual RtspSessionState GetState() const = 0;
	virtual HRESULT TryGetLatestFrame(RtspFrameSnapshot& frame) = 0;
	// Shares the Frame Server's D3D11 device manager with the source reader so the
	// RTSP decoder produces GPU textures (DXVA). Must be set before Start() to take
	// effect; if unset, the reader falls back to system-memory output.
	virtual void SetD3DManager(IUnknown* manager) = 0;
	// Live diagnostics for StatsPublisher (see Shared/VCamStats.h): cumulative
	// frames received from RTSP and frames dropped (replaced before a consumer
	// ever picked them up). Both 0 if there is no active reader.
	virtual void GetFrameCounters(UINT64& rxFrames, UINT64& droppedFrames) const = 0;
};

struct StreamRuntimeContext
{
	CameraSessionConfig config;
	IRtspSessionManager* rtspManager = nullptr;
};

class RtspSessionManager final : public IRtspSessionManager
{
public:
	static RtspSessionManager& Instance();

	HRESULT Start(const CameraSessionConfig& config) override;
	HRESULT Stop(UINT64 generation) override;
	bool IsRunning() const override;
	UINT64 GetGeneration() const override;
	RtspSessionState GetState() const override;
	HRESULT TryGetLatestFrame(RtspFrameSnapshot& frame) override;
	void SetD3DManager(IUnknown* manager) override;
	void GetFrameCounters(UINT64& rxFrames, UINT64& droppedFrames) const override;

	// Reconnection policy: on a stream break we retry kMaxReconnectAttempts times,
	// kReconnectIntervalMs apart (60 attempts, one per second).
	static constexpr int kMaxReconnectAttempts = 60;
	static constexpr int kReconnectIntervalMs = 1000;

private:
	RtspSessionManager() = default;
	~RtspSessionManager();
	HRESULT BuildRequestedType(const CameraSessionConfig& config, IMFMediaType** ppType);
	HRESULT OpenReaderLocked(const CameraSessionConfig& config); // (re)creates the reader; _lock held
	void ReleaseReaderLocked();                                  // tears down reader/callback; _lock held
	void StopNoLock();                                           // ReleaseReaderLocked + reset session state
	void StopReconnectThread();                                  // signals + joins reconnect thread; no _lock held
	void NotifyStreamBroken(UINT64 generation);                  // called by the reader callback on disconnect
	void ReconnectLoop(CameraSessionConfig config, UINT64 generation);

	// Lazily creates _ownDevice/_ownDxgiManager the first time the reader needs a D3D
	// manager and the Frame Server hasn't supplied one via SetD3DManager (which in
	// practice never happens for a single-media-type software virtual camera - see
	// CLAUDE.md). Kept alive for the process lifetime once created, like _dxgiManager's
	// counterpart on the Frame Server side; _lock held.
	HRESULT EnsureOwnDeviceManagerLocked();
	// Downloads a DXGI-texture-backed NV12 sample (decoded on _ownDevice) into a plain
	// system-memory sample, so MediaStream::CopyRtspFrame keeps seeing exactly the shape
	// of sample it already handles on the no-D3D-manager path. _lock held.
	HRESULT DownloadGpuSampleLocked(IMFSample* gpuSample, wil::com_ptr_nothrow<IMFSample>& outSample);

	mutable winrt::slim_mutex _lock;
	CameraSessionConfig _config{};
	GUID _actualSubtype = MFVideoFormat_NV12;
	UINT32 _actualWidth = 0;
	UINT32 _actualHeight = 0;
	UINT32 _actualStride = 0;
	RtspSessionState _state = RtspSessionState::Stopped;
	bool _running = false;
	wil::com_ptr_nothrow<IMFSourceReader> _reader;
	wil::com_ptr_nothrow<RtspReaderCallback> _callback;
	wil::com_ptr_nothrow<IMFDXGIDeviceManager> _dxgiManager; // shared GPU device for DXVA decode, when the Frame Server provides one via SetD3DManager
	std::thread _reconnectThread;
	std::atomic<bool> _stopReconnect{ false };

	// Own D3D11 device + device manager, used for the reader whenever _dxgiManager above
	// is unset, so H.264 DXVA hardware decode happens regardless of Frame Server
	// cooperation. See EnsureOwnDeviceManagerLocked. Created once, kept for the process
	// lifetime (device creation is expensive; safe to reuse across reconnects/sessions).
	wil::com_ptr_nothrow<ID3D11Device> _ownDevice;
	wil::com_ptr_nothrow<IMFDXGIDeviceManager> _ownDxgiManager;
	HANDLE _ownDeviceHandle = nullptr;
	bool _usingOwnDeviceForReader = false; // which manager OpenReaderLocked actually attached to the reader, this session
	// Set by NotifyStreamBroken when the stream breaks while _usingOwnDeviceForReader was
	// true: the own D3D11 device negotiated a media type fine but failed during actual
	// decode (observed as MF_E_INVALIDMEDIATYPE from OnReadSample, not from
	// SetCurrentMediaType) - a sign the device isn't actually usable for DXVA decode in
	// this process (Frame Server / Local Service). Once set, OpenReaderLocked stops
	// offering the own device for the rest of the process lifetime (deliberately NOT
	// reset on Start()/Stop() - retrying on every camera (re)start would mean a repeated
	// ~1s reconnect glitch on a setup where this is never going to work), falling back to
	// the system-memory path that's known to work. A fresh DLL load (Frame Server
	// restart, reboot) gets one new attempt.
	bool _skipOwnDeviceThisGeneration = false;

	// Reusable CPU-readable staging texture for DownloadGpuSampleLocked; recreated when
	// the decoded frame size changes.
	wil::com_ptr_nothrow<ID3D11Texture2D> _stagingTex;
	UINT32 _stagingWidth = 0;
	UINT32 _stagingHeight = 0;

	// Caches the system-memory conversion of the latest GPU-decoded frame so repeated
	// TryGetLatestFrame polls (~30x/sec) that re-serve the same frameSeq don't redo the
	// GPU->CPU download every tick.
	wil::com_ptr_nothrow<IMFSample> _downloadedSample;
	UINT64 _downloadedFrameSeq = 0;
};
