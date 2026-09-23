// C exports for the FFmpeg receive engine's user-space producer, called from the
// C# app (P/Invoke, VirtualCameraWrapper). A single process-wide producer instance
// is enough: the app runs one virtual camera at a time. Compiled native (no /clr)
// because it pulls in the libav headers via FfmpegRtspSource.
#include <windows.h>
#include <cstdint>
#include <memory>
#include "FfmpegRtspSource.h"
#include "FrameChannelWriter.h"

namespace
{
	std::unique_ptr<FfmpegRtspSource> g_producer;
	// The writer that pushes decoded NV12 into the frame shared memory the Frame
	// Server reads. Owned here (not by FfmpegRtspSource) so the same decode core can
	// serve the preview with a different sink. The mapping is CREATED by the Frame
	// Server; EnsureOpen() just opens it for writing once it exists.
	FrameChannelWriter g_writer;
}

extern "C" {

	// Starts (or restarts) the user-space RTSP→NV12→shared-memory producer.
	// width/height must match the VCamConfig geometry sent to the Frame Server.
	// Returns 0 on success, -1 on failure.
	__declspec(dllexport) int VCam_StartFfmpegProducer(
		const wchar_t* url, uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen)
	{
		if (!url) return -1;
		if (g_producer)
			g_producer->Stop();
		else
			g_producer = std::make_unique<FfmpegRtspSource>();

		// Target: decode/scale each frame straight into the next shared-memory ring slot
		// (no intermediate NV12 buffer + memcpy), then publish it — or drop the slot if
		// the conversion failed.
		FfmpegRtspSource::FrameTarget target;
		target.acquire = [](uint32_t w, uint32_t h, uint8_t* data[2], int linesize[2])
		{
			return g_writer.EnsureOpen() && g_writer.BeginWrite(w, h, data, linesize);
		};
		target.release = [](bool publish)
		{
			if (publish)
				g_writer.CommitWrite();
			else
				g_writer.AbortWrite();
		};

		return g_producer->Start(std::wstring(url), width, height, fpsNum, fpsDen, std::move(target)) ? 0 : -1;
	}

	__declspec(dllexport) int VCam_StopFfmpegProducer()
	{
		if (g_producer)
			g_producer->Stop();
		g_writer.Close();
		return 0;
	}

	// Cumulative frames decoded+published by the producer since the last Start.
	// Returns 0 on success, -1 if no producer exists.
	__declspec(dllexport) int VCam_GetFfmpegProducerFrames(uint64_t* framesOut)
	{
		if (!framesOut) return -1;
		if (!g_producer) { *framesOut = 0; return -1; }
		*framesOut = g_producer->FramesDecoded();
		return 0;
	}

	// 1 if the producer's last frame was hardware-decoded (d3d11va), 0 if software
	// or no producer.
	__declspec(dllexport) int VCam_IsFfmpegProducerHardware()
	{
		return (g_producer && g_producer->IsHardware()) ? 1 : 0;
	}

	// Process-wide FFmpeg engine options, set from the app's settings dialog. They
	// configure the shared FfmpegRtspSource statics, so they apply to both the preview
	// and the producer, and take effect the next time a connection is opened.
	// mode: 0 = Auto (UDP preferred, TCP fallback), 1 = UDP only, 2 = TCP only.
	__declspec(dllexport) void VCam_SetRtspTransport(int mode)
	{
		RtspTransport t = RtspTransport::Auto;
		if (mode == (int)RtspTransport::Udp) t = RtspTransport::Udp;
		else if (mode == (int)RtspTransport::Tcp) t = RtspTransport::Tcp;
		FfmpegRtspSource::SetTransportPreference(t);
	}

	// enabled != 0 lets the decoder use d3d11va (GPU); 0 forces software decode.
	__declspec(dllexport) void VCam_SetHardwareDecode(int enabled)
	{
		FfmpegRtspSource::SetHardwareDecodeEnabled(enabled != 0);
	}

	// RTSP socket timeout (libav "timeout", µs on the wire), in milliseconds. Also the
	// engine's read-watchdog period (see FfmpegRtspSource::SetSocketTimeoutMs).
	__declspec(dllexport) void VCam_SetSocketTimeoutMs(int ms)
	{
		FfmpegRtspSource::SetSocketTimeoutMs(ms);
	}

	// RTP jitter/reorder buffer depth (libav "reorder_queue_size"), in packets.
	__declspec(dllexport) void VCam_SetReorderQueue(int packets)
	{
		FfmpegRtspSource::SetReorderQueueSize(packets);
	}

	// UDP receive buffer for the RTSP transport (libav "buffer_size"), in bytes.
	// 0 leaves libav's default. Absorbs high-bitrate packet bursts (anti green-bands).
	__declspec(dllexport) void VCam_SetUdpBufferSize(int bytes)
	{
		FfmpegRtspSource::SetUdpBufferSize(bytes);
	}

	// Demuxer max reorder delay (libav "max_delay"), in milliseconds. 0 = lowest latency.
	__declspec(dllexport) void VCam_SetMaxDelayMs(int ms)
	{
		FfmpegRtspSource::SetMaxDelayMs(ms);
	}

	// Latency cap: resync-to-live threshold in milliseconds.
	__declspec(dllexport) void VCam_SetLatencyCapMs(int ms)
	{
		FfmpegRtspSource::SetMaxLagMs(ms);
	}

	// Which RTSP transport is actually carrying the producer's frames right now:
	// 0 = not connected, 1 = UDP, 2 = TCP. Definitive even in Auto mode.
	__declspec(dllexport) int VCam_GetActiveTransport()
	{
		return g_producer ? g_producer->ActiveTransport() : 0;
	}

	// Measured received video bitrate in bits/s (0 if no producer / not yet measured).
	__declspec(dllexport) long long VCam_GetFfmpegProducerBitrate()
	{
		return g_producer ? (long long)g_producer->BitrateBps() : 0;
	}

	// Live connection state of the producer (FfmpegRtspSource::ConnectionState as
	// int: 0 idle, 1 connecting, 2 streaming, 3 reconnecting). Optional outputs:
	// attemptOut = number of the connection attempt in progress since frames last
	// flowed (0 while streaming); disconnectsOut = how many times an established
	// stream broke this session; lastErrorOut = last AVERROR code (0 = none). The UI
	// polls this to show "connection lost, retrying (attempt N)".
	__declspec(dllexport) int VCam_GetProducerConnectionState(
		uint32_t* attemptOut, uint32_t* disconnectsOut, int* lastErrorOut)
	{
		if (attemptOut)     *attemptOut     = g_producer ? g_producer->ConnectAttempt() : 0;
		if (disconnectsOut) *disconnectsOut = g_producer ? g_producer->Disconnects()    : 0;
		if (lastErrorOut)   *lastErrorOut   = g_producer ? g_producer->LastError()      : 0;
		return g_producer ? (int)g_producer->State() : (int)ConnectionState::Idle;
	}

} // extern "C"
