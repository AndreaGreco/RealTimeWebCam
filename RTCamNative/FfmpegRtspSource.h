#pragma once
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include "FrameChannelWriter.h"

// User-space RTSP receiver for the FFmpeg engine. Runs entirely in the app
// process (RTCamNative): opens the RTSP URL with libavformat, decodes H.264/H.265
// with libavcodec (software), scales/converts each frame to NV12 with libswscale,
// and publishes it through FrameChannelWriter into the frame shared memory that
// the Frame Server reads (Shared/VCamFrameChannel.h).
//
// One background thread owns the whole decode loop and reconnects on its own if
// the stream drops, until Stop(). Because the producer lives in the app process,
// the virtual camera only has live frames while the app is running — by design
// (see CLAUDE.md "Two receive engines"); the Frame Server falls back to the
// synthetic frame when the heartbeat goes stale.
class FfmpegRtspSource
{
public:
	FfmpegRtspSource() = default;
	~FfmpegRtspSource();
	FfmpegRtspSource(const FfmpegRtspSource&) = delete;
	FfmpegRtspSource& operator=(const FfmpegRtspSource&) = delete;

	// targetWidth/Height must match the geometry the Frame Server used to size the
	// frame mapping (i.e. the same VCamConfig width/height the app sent). Starts the
	// decode thread; returns false if already running or the args are invalid.
	bool Start(const std::wstring& rtspUrl, uint32_t targetWidth, uint32_t targetHeight,
	           uint32_t fpsNum, uint32_t fpsDen);

	// Signals the decode thread (interrupting any blocking libav call) and joins it.
	void Stop();

	bool IsRunning() const { return _running.load(); }

	// Cumulative frames the decoder has published; for producer-side diagnostics.
	uint64_t FramesDecoded() const { return _framesDecoded.load(); }

private:
	void DecodeLoop(std::string url, uint32_t targetW, uint32_t targetH);
	// libav interrupt callback: returns non-zero to abort a blocking call when _stop is set.
	static int InterruptCb(void* opaque);

	std::thread _thread;
	std::atomic<bool> _stop{ false };
	std::atomic<bool> _running{ false };
	std::atomic<uint64_t> _framesDecoded{ 0 };
	FrameChannelWriter _writer;
};
