#pragma once
#include <windows.h>
#include <cstdint>
#include "..\Shared\VCamFrameChannel.h"

// Writer side of the FFmpeg frame channel (Shared/VCamFrameChannel.h), running in
// the app process (RTCamNative, user-space). The mapping itself is CREATED by the
// Frame Server (FrameChannelReader) because creating a Global\ object needs
// SeCreateGlobalPrivilege, which the service has and a non-elevated user does not;
// this class only OPENS it for writing (retrying until the Frame Server has made
// it) and publishes NV12 frames into the triple-buffered ring.
//
// The Frame Server is the source of truth for geometry: this writer reads
// width/height/stride/slotCount from the header the reader stamped, and the
// producer (FfmpegRtspSource) scales its decoded frames to exactly that size
// before calling WriteFrame.
class FrameChannelWriter
{
public:
	FrameChannelWriter() = default;
	~FrameChannelWriter();
	FrameChannelWriter(const FrameChannelWriter&) = delete;
	FrameChannelWriter& operator=(const FrameChannelWriter&) = delete;

	// Opens the existing mapping for writing. Returns false if it does not exist
	// yet (the Frame Server hasn't created it) — the caller retries. Cheap no-op
	// once open.
	bool EnsureOpen();

	bool IsOpen() const { return _header != nullptr; }

	// Geometry the Frame Server published; valid only after EnsureOpen() succeeds.
	uint32_t Width()  const { return _header ? _header->width : 0; }
	uint32_t Height() const { return _header ? _header->height : 0; }
	uint32_t Stride() const { return _header ? _header->stride : 0; }

	// Publishes one NV12 frame. srcY points at the Y plane (height rows of srcStride
	// bytes), srcUV at the interleaved UV plane (height/2 rows of srcStride bytes).
	// The planes are copied into the next ring slot honoring the destination stride,
	// then the slot is published under the header seqlock. No-op if not open.
	void WriteFrame(const uint8_t* srcY, int srcStrideY,
	                const uint8_t* srcUV, int srcStrideUV);

	void Close();

private:
	HANDLE _mapping = nullptr;
	VCamFrameChannelHeader* _header = nullptr;
};
