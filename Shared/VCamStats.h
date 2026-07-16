#pragma once
#include <guiddef.h>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
// Cross-process live stats channel: Frame Server (VirtualCamera.dll, running
// inside svchost.exe as Local Service) -> app (RTVirtualCamera.exe, running in
// the interactive user session). See CLAUDE.md "Live stats (Frame Server ->
// app)" for the full picture.
//
// The two processes live in different Terminal Server sessions, so a plain
// named section is invisible between them: the mapping is created in the
// "Global\" kernel object namespace (VirtualCamera/StatsPublisher.cpp), with
// an explicit DACL granting read access outside Local Service — the default
// security descriptor for an object created by a service does not allow that.
//
// Written ~30x/sec by MediaStream::RequestSample (Frame Server thread, on the
// latency-critical frame path — see CopyRtspFrame notes in CLAUDE.md), read a
// couple of times a second by the app's UI timer. A named Mutex would work but
// costs a kernel call per frame on the write side; instead this struct uses a
// seqlock (same idea as the Linux kernel / Windows KUSER_SHARED_DATA): the
// writer bumps Sequence to odd before writing and back to even after; the
// reader retries if Sequence is odd, or changed, across the read. Pure
// user-mode on both sides, no blocking — safe because there is exactly one
// writer (VirtualCamera/StatsPublisher.cpp).
// ─────────────────────────────────────────────────────────────────────────────

#define VCAM_STATS_STRUCT_VERSION 2u

// Global\ namespace = visible across sessions. Suffixed with the fixed CLSID
// (Shared use only — do not confuse with CLSID_VCam's canonical definition in
// RTCamNative/VirtualCamera.h / the registry) so a future multi-instance setup
// can't collide with this name.
#define VCAM_STATS_MAPPING_NAME L"Global\\RTVCam_Stats_3CAD447D-F283-4AF4-A3B2-6F5363309F52"

// Session state on the wire. With the single FFmpeg receive path the Frame Server
// reports Running/Starting from the app producer's heartbeat freshness; the
// Reconnecting/Failed states are legacy (kept for wire stability, not emitted).
// Kept as a separate, explicitly-sized enum here because this header is shared
// with RTCamNative and the C# reader, neither of which needs the full C++ type.
enum class VCamSessionStateWire : uint32_t
{
	Stopped = 0,
	Starting = 1,
	Running = 2,
	Reconnecting = 3,
	Failed = 4,
};

#pragma pack(push, 1)
struct VCamFrameServerStats
{
	uint32_t structVersion;   // VCAM_STATS_STRUCT_VERSION; reader must check before trusting the rest
	volatile long sequence;   // seqlock: odd while the writer is updating, even + advanced once stable
	uint64_t updatedTickMs;   // GetTickCount64() at the last publish; a reader can tell a dead writer
	                          // apart from "no session yet" by checking whether this stops advancing
	uint32_t sessionState;    // VCamSessionStateWire
	uint64_t rxFrames;        // cumulative frames decoded from RTSP (received from the source)
	uint64_t renderedFrames;  // cumulative frames actually copied into a consumer sample (Zoom/Teams/...) —
	                          // every delivery, including a re-serve of the same underlying RTSP frame as
	                          // last time (see declinedFrames); MediaStream always delivers something on
	                          // every RequestSample once the first frame has arrived
	uint64_t droppedFrames;   // cumulative frames the RTSP source produced but were replaced before
	                          // ever being picked up by a consumer (source outrunning the consumer)
	uint64_t declinedFrames;  // of renderedFrames, how many were a re-serve of the same underlying RTSP
	                          // frame as the previous delivery (still copied and delivered — "declined" is
	                          // a name kept from an earlier attempt at actually skipping these via
	                          // MF_E_SAMPLEALLOCATOR_EMPTY, which turned out to destabilize the Frame
	                          // Server's own retry timing and was reverted; this is a count only). A high
	                          // declinedFrames rate vs rxFrames means the consumer (Zoom/Teams) is asking
	                          // faster than the camera delivers, which is normal, not a bug.
	double   driftMs;         // wall-clock elapsed - media elapsed since the first frame of the
	                          // current session; ~flat = fixed latency, rising = accumulating delay
	double   lastCopyMs;      // cost of the last CopyRtspFrame call (GPU or CPU path)
	uint32_t width;
	uint32_t height;
	uint32_t fpsNum;
	uint32_t fpsDen;
	uint32_t hwAccelCapable;  // 1 = the Frame Server handed a D3D11 device manager to MediaStream,
	                          // i.e. the GPU zero-copy path is wired up and can engage
	uint32_t hwAccelActive;   // 1 = the last frame copy actually took the GPU zero-copy path,
	                          // 0 = it fell back to the CPU path (see CopyRtspFrame in CLAUDE.md)
};
#pragma pack(pop)
