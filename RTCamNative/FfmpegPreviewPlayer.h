#pragma once
#include <windows.h>
#include <string>
#include <atomic>
#include <vector>
#include "FfmpegRtspSource.h"

// Forward declaration so this header stays free of libav includes (the NV12->BGRA
// scaler is a libswscale object, used only inside the .cpp / native TU).
struct SwsContext;

// Stream geometry reported to the C# UI after probing the source. Mirrored
// byte-for-byte by RTVirtualCamera/VideoPlayerWrapper.cs::StreamInfo.
struct StreamInfo
{
	UINT32 width = 0;
	UINT32 height = 0;

	GUID subtype = GUID_NULL;

	UINT32 fpsNum = 0;
	UINT32 fpsDen = 0;

	UINT32 bitrate = 0;
};

// Human-readable connection characteristics of the current source, gathered once
// at initialize() from the FFmpeg probe and polled by the C# UI to fill the
// "Connessione" table. Mirrored byte-for-byte by
// RTVirtualCamera/VideoPlayerWrapper.cs::ConnectionInfo (CharSet.Ansi). The char[]
// fields are NUL-terminated ASCII/UTF-8 straight from libav (empty when unknown).
// int64 first so the following uint32s stay naturally aligned on both sides.
struct ConnectionInfo
{
	INT64  bitrate = 0;   // bits/s reported by libav (0 = unknown)
	UINT32 width = 0;
	UINT32 height = 0;
	UINT32 fpsNum = 0;
	UINT32 fpsDen = 0;
	INT32  valid = 0;     // 1 = the fields below are populated

	char container[64] = {};
	char transport[16] = {};
	char videoCodec[32] = {};
	char profile[48] = {};
	char pixelFormat[24] = {};
};

// Live diagnostics for the preview path, polled from the C# UI. Mirrored
// byte-for-byte by RTVirtualCamera/VideoPlayerWrapper.cs::PreviewStats.
struct PreviewStats
{
	unsigned long long receivedFrames; // frames delivered by the decoder
	unsigned long long renderedFrames; // frames actually blitted to the panel
	unsigned long long droppedFrames;  // frames skipped before render (0 here — the
	                                    // decode core already drops stale frames via
	                                    // its latency cap, so none reach the renderer)
	double driftMs;       // how far the last frame was behind live (FfmpegRtspSource
	                       // latency cap keeps this bounded, unlike the old MF path)
	double lastRenderMs;  // cost of the last NV12->BGRA convert + GDI blit (ms)
};

// FFmpeg-based preview player (replaces the old Media Foundation VideoPlayer/EVR).
// Owns an FfmpegRtspSource decode core; each decoded NV12 frame is converted to
// BGRA and blitted into the WinForms panel HWND with a double-buffered GDI path
// (StretchDIBits into a back buffer, then a single BitBlt — no flicker). Exposes
// the same C entry points the app already P/Invokes (see PreviewExports.cpp), so
// VideoPlayerWrapper.cs and MainForm are unchanged.
//
// All rendering happens on the decode thread; stop() joins that thread before the
// GDI back buffer is released, so there is never concurrent access to it.
class FfmpegPreviewPlayer
{
public:
	FfmpegPreviewPlayer() = default;
	~FfmpegPreviewPlayer();
	FfmpegPreviewPlayer(const FfmpegPreviewPlayer&) = delete;
	FfmpegPreviewPlayer& operator=(const FfmpegPreviewPlayer&) = delete;

	void setVideoPath(LPCWSTR path);
	void setWindowHandle(HWND hwnd);

	// Probes the source (open + find_stream_info) and caches its geometry. Returns 0
	// on success, negative if the source can't be opened / has no video stream.
	int initialize();

	// Starts the decode+render loop against the current path. Returns 0 on success.
	int play();
	int pause();
	int stop();

	int getVideoStreamInfo(StreamInfo* pInfo, UINT32 maxCount, UINT32* count);
	int getPreviewStats(PreviewStats* pStats);

	// Returns the connection characteristics cached by initialize(). 0 on success,
	// E_INVALIDARG if pInfo is null. When the source has not been probed yet the
	// struct is returned with valid=0.
	int getConnectionInfo(ConnectionInfo* pInfo);

	void updateVideoPosition() {} // GDI path re-reads the client rect each frame

	bool getIsPlaying() const { return _playing.load(); }
	bool getIsPaused()  const { return false; }

	// Which RTSP transport is actually carrying the preview's frames right now:
	// 0 = not connected, 1 = UDP, 2 = TCP (see FfmpegRtspSource::ActiveTransport).
	int getActiveTransport() const { return _source.ActiveTransport(); }

private:
	// Sink callback (decode thread): NV12 -> BGRA -> back buffer -> panel.
	void OnFrame(const uint8_t* y, int strideY, const uint8_t* uv, int strideUV,
	             uint32_t w, uint32_t h);
	void ReleaseBackBuffer();

	std::wstring _url;
	HWND _hwnd = nullptr;

	StreamInfo _streamInfo;
	bool _hasStreamInfo = false;

	ConnectionInfo _connInfo; // filled by initialize() from the FFmpeg probe

	FfmpegRtspSource _source;
	std::atomic<bool> _playing{ false };

	// --- Renderer state (decode thread only) --------------------------------
	SwsContext* _swsBgra = nullptr; // NV12 -> BGRA scaler (cached)
	std::vector<uint8_t> _bgra;     // BGRA frame buffer, w*4 stride
	HDC _memDC = nullptr;           // back-buffer DC
	HBITMAP _memBmp = nullptr;      // back-buffer bitmap
	HGDIOBJ _oldBmp = nullptr;
	int _bbW = 0;                   // back-buffer size
	int _bbH = 0;

	// --- Live stats (updated on decode thread, read on UI thread) -----------
	std::atomic<unsigned long long> _received{ 0 };
	std::atomic<unsigned long long> _rendered{ 0 };
	std::atomic<double> _lastRenderMs{ 0.0 };
};
