#pragma once
#include <guiddef.h>
#include <cstdint>
#include <cstddef>

// ─────────────────────────────────────────────────────────────────────────────
// Cross-process FRAME (pixel) channel for the FFmpeg receive engine.
//
// Unlike the Media Foundation engine — where the Frame Server opens the RTSP
// source itself and never shares pixels — the FFmpeg engine decodes in the app
// process (RTCamNative, user-space, libav) and streams NV12 frames to the Frame
// Server (VCamSampleSource.dll inside svchost.exe) through this shared-memory
// section. The Frame Server then copies the latest frame into the consumer's
// sample in MediaStream::RequestSample, exactly like the MF path does with a
// frame from RtspSessionManager. See CLAUDE.md "Two receive engines".
//
// Direction is REVERSED vs the stats channel (Shared/VCamStats.h): here the app
// is the writer and the Frame Server is the reader. But creating a "Global\"
// kernel object requires SeCreateGlobalPrivilege, which a service holds and a
// non-elevated interactive user does NOT. So the mapping is still CREATED by the
// Frame Server (FrameChannelReader, with an explicit DACL granting Interactive
// Users read+write) and merely OPENED for writing by the app (FrameChannelWriter,
// retrying until it exists). Same "service creates, app opens" shape as the
// stats channel — only the data direction differs.
//
// Layout: a header reserved to one page (VCAM_FRAMES_HEADER_BYTES) followed by
// VCAM_FRAMES_SLOT_COUNT contiguous NV12 slots (triple buffering), each rounded up
// to a whole number of pages so every slot starts page-aligned. The single writer fills the next slot, then publishes
// its index + a bumped frameSeq under the header seqlock (publishSeq bumped
// odd→even, same idiom as VCamStats.h). With 3 slots and one reader the writer
// has ~2 frames of headroom before it revisits a slot the reader might still be
// copying, so a plain memcpy read needs no lock on the pixels themselves — only
// the small header metadata is seqlock-protected. Exactly one writer makes this
// safe without a heavier primitive.
//
// Frame-ready event (VCAM_FRAMES_EVENT_NAME): an auto-reset Global\ event the
// writer signals after each publish, so the reader can deliver on arrival instead
// of polling on a timer. Like the mapping, it is CREATED only by the Frame Server
// (same privilege reason) and OPENED by the app with EVENT_MODIFY_STATE. It is an
// optimization: both sides must keep working without it.
// ─────────────────────────────────────────────────────────────────────────────

// v2: page-aligned header/slots (VCAM_FRAMES_HEADER_BYTES, VCamFrameChannel_SlotBytes).
#define VCAM_FRAMES_STRUCT_VERSION 2u

// Global\ namespace = visible across Terminal Server sessions. Suffixed with the
// fixed CLSID so a future multi-instance setup can't collide (same convention as
// VCAM_STATS_MAPPING_NAME).
#define VCAM_FRAMES_MAPPING_NAME L"Global\\RTVCam_Frames_3CAD447D-F283-4AF4-A3B2-6F5363309F52"

// Auto-reset event signaled by the writer after each publish (see above).
#define VCAM_FRAMES_EVENT_NAME L"Global\\RTVCam_FrameReady_3CAD447D-F283-4AF4-A3B2-6F5363309F52"

// Bytes reserved for the header: one page, so slot 0 (and, with page-rounded slot
// sizes, every slot) starts page-aligned.
#define VCAM_FRAMES_HEADER_BYTES 4096u
#define VCAM_FRAMES_PAGE_BYTES   4096u

// Triple buffering: enough headroom that the writer never overwrites the slot the
// single reader is mid-copy on at a normal 30–60 fps cadence.
#define VCAM_FRAMES_SLOT_COUNT 3u

// The mapping is always sized for this maximum geometry (regardless of the actual
// stream resolution) and the real width/height/bytesPerSlot are carried in the
// header. This keeps the mapping a fixed size across camera sessions — the section
// outlives any single session (like the stats one), and a fixed size avoids the
// "existing named mapping has the old size" problem when the resolution changes
// between sessions. ~37 MB for 3840x2160 NV12 * 3 slots, pagefile-backed.
#define VCAM_FRAMES_MAX_WIDTH  3840u
#define VCAM_FRAMES_MAX_HEIGHT 2160u

#pragma pack(push, 1)
struct VCamFrameChannelHeader
{
	uint32_t structVersion;          // VCAM_FRAMES_STRUCT_VERSION; reader checks before trusting the rest
	volatile long publishSeq;        // seqlock over the metadata below: odd while publishing, even + advanced once stable
	uint32_t width;                  // frame width  in pixels
	uint32_t height;                 // frame height in pixels
	uint32_t stride;                 // bytes per row of the Y plane inside a slot (writer packs tightly: stride == width)
	uint32_t fpsNum;                 // frame-rate numerator   (informational)
	uint32_t fpsDen;                 // frame-rate denominator (informational)
	GUID     format;                 // pixel subtype; always NV12 for now (MFVideoFormat_NV12)
	uint32_t slotCount;              // VCAM_FRAMES_SLOT_COUNT
	uint32_t bytesPerSlot;           // size of one slot = stride * height * 3 / 2 rounded up to a page (VCamFrameChannel_SlotBytes)
	volatile long latestSlot;        // index [0..slotCount) of the most recently fully-written slot; -1 = none yet
	uint64_t frameSeq;               // bumped once per genuinely new frame; reader tells fresh from re-serve (like RtspFrameSnapshot::frameSeq)
	uint64_t producerHeartbeatTickMs;// GetTickCount64() in the app process at the last write; reader treats a stale value as "producer gone" and shows the synthetic frame
	uint64_t framesWritten;          // cumulative frames the writer has published (feeds rxFrames-style stats)
	// NV12 slots start at VCAM_FRAMES_HEADER_BYTES: slotCount * bytesPerSlot bytes.
};
#pragma pack(pop)

static_assert(sizeof(VCamFrameChannelHeader) <= VCAM_FRAMES_HEADER_BYTES,
	"VCamFrameChannelHeader must fit in the reserved header page");

// NV12 byte size for a tightly-packed (stride == width) frame — the real payload.
inline uint32_t VCamFrameChannel_Nv12Bytes(uint32_t width, uint32_t height)
{
	return width * height * 3u / 2u;
}

// Slot size for the given geometry: the NV12 payload rounded up to whole pages, so
// consecutive slots stay page-aligned.
inline uint32_t VCamFrameChannel_SlotBytes(uint32_t width, uint32_t height)
{
	return (VCamFrameChannel_Nv12Bytes(width, height) + VCAM_FRAMES_PAGE_BYTES - 1u)
		/ VCAM_FRAMES_PAGE_BYTES * VCAM_FRAMES_PAGE_BYTES;
}

// Total mapping size for the given geometry: header page + all slots.
inline uint32_t VCamFrameChannel_MappingSize(uint32_t width, uint32_t height)
{
	return VCAM_FRAMES_HEADER_BYTES
		+ VCAM_FRAMES_SLOT_COUNT * VCamFrameChannel_SlotBytes(width, height);
}

// Fixed size the reader always creates the mapping with (see VCAM_FRAMES_MAX_* above).
inline uint32_t VCamFrameChannel_MaxMappingSize()
{
	return VCamFrameChannel_MappingSize(VCAM_FRAMES_MAX_WIDTH, VCAM_FRAMES_MAX_HEIGHT);
}

// Pointer to slot i within a mapped header. The caller guarantees i < slotCount
// and that the mapping is at least VCamFrameChannel_MappingSize bytes.
inline uint8_t* VCamFrameChannel_SlotPtr(VCamFrameChannelHeader* header, uint32_t i)
{
	uint8_t* base = reinterpret_cast<uint8_t*>(header) + VCAM_FRAMES_HEADER_BYTES;
	return base + (size_t)i * header->bytesPerSlot;
}
