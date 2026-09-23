#pragma once
#include "..\Shared\VCamFrameChannel.h"

// Reader side of the FFmpeg frame channel (Shared/VCamFrameChannel.h), running in
// the Frame Server (VCamSampleSource.dll inside svchost.exe). It CREATES the
// Global\ mapping — a service holds SeCreateGlobalPrivilege and a non-elevated
// user does not, so even though the app is the writer, the reader must own the
// creation (same "service creates, app opens" shape as StatsPublisher). The
// mapping is created at a fixed maximum size (VCAM_FRAMES_MAX_*) and the actual
// stream geometry is stamped into the header, so the section can outlive a session
// and be reused across resolution changes without a resize.
//
// Singleton, same pattern as StatsPublisher / RtspSessionManager::Instance().
// Created lazily by VCamMediaSource when the FFmpeg engine is selected; never torn
// down explicitly. MediaStream::RequestSample reads the latest frame from it.
class FrameChannelReader
{
public:
	static FrameChannelReader& Instance();

	// Creates the mapping on first use and stamps the given geometry into the header.
	// Once mapped, always re-stamps the header (resets latestSlot/heartbeat/counters)
	// so a previous session's frame never looks fresh. Returns false (with a WINTRACE
	// breadcrumb) if creation failed.
	bool EnsureMapped(uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen);

	bool IsMapped() const { return _header != nullptr; }

	// Auto-reset Global\ event the app signals after each publish (VCAM_FRAMES_EVENT_NAME),
	// created alongside the mapping. nullptr if creation failed — it is an optimization,
	// callers must keep working without it.
	HANDLE FrameReadyEvent() const { return _frameReadyEvent; }

	// Points ppSlot at the latest fully-written NV12 slot (tightly packed, stride ==
	// width) and returns its identity/heartbeat, doing a bounded seqlock read. The
	// caller must copy the pixels out promptly; triple buffering guarantees the
	// producer won't overwrite this slot for ~2 frame intervals. Returns false if no
	// frame has been published yet, the header has a different structVersion, or the
	// read couldn't stabilize.
	bool AcquireLatest(const uint8_t** ppSlot, uint32_t* width, uint32_t* height, uint32_t* stride,
	                   uint64_t* frameSeq, uint64_t* framesWritten, uint64_t* heartbeatTickMs);

	// Call after copying the slot AcquireLatest returned (with its frameSeq): false if
	// the producer has since advanced far enough (>= slotCount-1 frames, or the header
	// was re-stamped) that it may have started overwriting that slot mid-copy.
	bool IsSlotStillValid(uint64_t acquiredFrameSeq) const;

private:
	FrameChannelReader() = default;
	~FrameChannelReader();
	FrameChannelReader(const FrameChannelReader&) = delete;
	FrameChannelReader& operator=(const FrameChannelReader&) = delete;

	void EnsureFrameReadyEvent();

	HANDLE _mapping = nullptr;
	VCamFrameChannelHeader* _header = nullptr;
	HANDLE _frameReadyEvent = nullptr;
};
