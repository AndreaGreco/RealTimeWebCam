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
// producer (FfmpegRtspSource) scales its decoded frames to the geometry it was
// started with before calling WriteFrame. WriteFrame takes that source geometry
// and skips any frame that doesn't match the header (the Frame Server can re-stamp
// it with a different config while the app is running), so a stale producer can
// never read past its source buffer or write past the section.
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

	// Publishes one width x height NV12 frame. srcY points at the Y plane (height
	// rows of srcStrideY bytes), srcUV at the interleaved UV plane (height/2 rows of
	// srcStrideUV bytes). The planes are copied into the next ring slot honoring the
	// destination stride, then the slot is published under the header seqlock.
	// No-op if not open, or if width/height don't match the header geometry (logged
	// once per mismatch episode, not per frame).
	void WriteFrame(const uint8_t* srcY, int srcStrideY,
	                const uint8_t* srcUV, int srcStrideUV,
	                uint32_t width, uint32_t height);

	void Close();

private:
	HANDLE _mapping = nullptr;
	VCamFrameChannelHeader* _header = nullptr;
	bool _geometryMismatch = false; // last WriteFrame skipped on geometry; only log transitions
};
