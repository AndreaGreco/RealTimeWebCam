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
// producer (FfmpegRtspSource) decodes/scales its frames to the geometry it was
// started with straight into a slot handed out by BeginWrite (WriteFrame is the
// copy-from-a-buffer variant). Both take that frame geometry and skip any frame
// that doesn't match the header (the Frame Server can re-stamp
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
	// yet (the Frame Server hasn't created it) — the caller retries. Cheap once
	// open: it only retries opening the frame-ready event, at most once a second.
	bool EnsureOpen();

	bool IsOpen() const { return _header != nullptr; }

	// Geometry the Frame Server published; valid only after EnsureOpen() succeeds.
	uint32_t Width()  const { return _header ? _header->width : 0; }
	uint32_t Height() const { return _header ? _header->height : 0; }
	uint32_t Stride() const { return _header ? _header->stride : 0; }

	// Publishes one width x height NV12 frame. srcY points at the Y plane (height
	// rows of srcStrideY bytes), srcUV at the interleaved UV plane (height/2 rows of
	// srcStrideUV bytes). The planes are copied into the next ring slot honoring the
	// destination stride, then the slot is published under the header seqlock and
	// the frame-ready event (if open) is signaled. No-op if not open, or if width/height don't match the header geometry (logged
	// once per mismatch episode, not per frame).
	void WriteFrame(const uint8_t* srcY, int srcStrideY,
	                const uint8_t* srcUV, int srcStrideUV,
	                uint32_t width, uint32_t height);

	// Two-phase write straight into the shared slot (no intermediate buffer), so the
	// producer can decode/scale directly into it. BeginWrite checks the geometry like
	// WriteFrame, picks the next ring slot and returns its Y/UV plane pointers and
	// stride; the caller fills them, then CommitWrite publishes under the seqlock (+
	// frame-ready event) or AbortWrite drops the slot unpublished. Returns false (and
	// no write is pending) if not open or the geometry doesn't match the header.
	bool BeginWrite(uint32_t width, uint32_t height, uint8_t* data[2], int linesize[2]);
	void CommitWrite();
	void AbortWrite();

	void Close();

private:
	void TryOpenFrameReadyEvent();

	HANDLE _mapping = nullptr;
	VCamFrameChannelHeader* _header = nullptr;
	// Frame-ready event (created by the Frame Server). Optional: without it the
	// writer works as before and the Frame Server falls back to its timer.
	HANDLE _frameReadyEvent = nullptr;
	ULONGLONG _lastEventOpenTick = 0; // throttles open retries to 1/s
	bool _geometryMismatch = false; // last WriteFrame skipped on geometry; only log transitions
	long _pendingSlot = -1;         // slot handed out by BeginWrite, not yet committed (-1 = none)
};
