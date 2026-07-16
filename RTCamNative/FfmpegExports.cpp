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

		// Sink: copy each decoded NV12 frame into the next shared-memory ring slot.
		auto sink = [](const uint8_t* y, int strideY, const uint8_t* uv, int strideUV,
		               uint32_t /*w*/, uint32_t /*h*/)
		{
			if (g_writer.EnsureOpen())
				g_writer.WriteFrame(y, strideY, uv, strideUV);
		};

		return g_producer->Start(std::wstring(url), width, height, fpsNum, fpsDen, sink) ? 0 : -1;
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

} // extern "C"
