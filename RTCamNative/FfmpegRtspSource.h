#pragma once
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include <functional>

// Geometry/codec probed from an RTSP source without starting a decode session.
// POD only — no libav types leak into this header, so managed (/clr) callers can
// include it if needed. Filled by FfmpegRtspSource::Probe (valid=false on failure).
struct FfmpegProbeInfo
{
	uint32_t width = 0;
	uint32_t height = 0;
	uint32_t fpsNum = 0;
	uint32_t fpsDen = 0;
	int      codecId = 0; // AVCodecID as int (0 = none/unknown)
	bool     valid = false;
};

// User-space RTSP receiver for the (now single) FFmpeg engine. Runs entirely in
// the app process (RTCamNative): opens the RTSP URL with libavformat, decodes
// H.264/H.265 with libavcodec (GPU via d3d11va when available, else software),
// scales/converts each frame to NV12 with libswscale, and hands it to a caller-
// supplied sink. Two consumers share this one class:
//   - the virtual-camera producer (FfmpegExports): sink writes NV12 into the
//     frame shared memory the Frame Server reads (Shared/VCamFrameChannel.h);
//   - the preview (FfmpegPreviewPlayer): sink renders NV12 into the WinForms panel.
// They never run at the same time (the preview stops when the camera starts), so a
// single RTSP connection is open at a time.
//
// One background thread owns the whole decode loop and reconnects on its own if
// the stream drops, until Stop(). Because it lives in the app process, the virtual
// camera only has live frames while the app is running — by design (see CLAUDE.md);
// the Frame Server falls back to the synthetic frame when the heartbeat goes stale.
class FfmpegRtspSource
{
public:
	// Called on the decode thread for each decoded+scaled NV12 frame. y points at
	// the Y plane (h rows of strideY bytes), uv at the interleaved UV plane (h/2
	// rows of strideUV bytes). w/h are the target size passed to Start().
	using FrameSink = std::function<void(const uint8_t* y, int strideY,
	                                     const uint8_t* uv, int strideUV,
	                                     uint32_t w, uint32_t h)>;

	FfmpegRtspSource() = default;
	~FfmpegRtspSource();
	FfmpegRtspSource(const FfmpegRtspSource&) = delete;
	FfmpegRtspSource& operator=(const FfmpegRtspSource&) = delete;

	// targetWidth/Height are the size every emitted NV12 frame is scaled to. For the
	// producer they must match the geometry the Frame Server used to size the frame
	// mapping; for the preview they are the source's native size. Starts the decode
	// thread; returns false if already running or the args are invalid.
	bool Start(const std::wstring& rtspUrl, uint32_t targetWidth, uint32_t targetHeight,
	           uint32_t fpsNum, uint32_t fpsDen, FrameSink sink);

	// Signals the decode thread (interrupting any blocking libav call) and joins it.
	void Stop();

	bool IsRunning() const { return _running.load(); }

	// Cumulative frames the decoder has published; for producer/preview diagnostics.
	uint64_t FramesDecoded() const { return _framesDecoded.load(); }

	// True if the last decoded frame came off the GPU (d3d11va), false if software.
	bool IsHardware() const { return _hwActive.load(); }

	// How far the last decoded frame was behind live (wall-clock minus stream PTS),
	// in ms; 0 when the stream carries no usable PTS. Used as the preview drift value.
	int64_t LastLagMs() const { return _lastLagMs.load(); }

	// Opens the URL just long enough to read geometry/codec, then closes it.
	// Blocking (bounded by the RTSP socket timeout); safe to call before Start().
	static FfmpegProbeInfo Probe(const std::wstring& rtspUrl);

private:
	void DecodeLoop(std::string url, uint32_t targetW, uint32_t targetH);
	// libav interrupt callback: returns non-zero to abort a blocking call when _stop is set.
	static int InterruptCb(void* opaque);

	std::thread _thread;
	std::atomic<bool> _stop{ false };
	std::atomic<bool> _running{ false };
	std::atomic<uint64_t> _framesDecoded{ 0 };
	std::atomic<bool> _hwActive{ false };
	std::atomic<int64_t> _lastLagMs{ 0 };
	FrameSink _sink;
};
