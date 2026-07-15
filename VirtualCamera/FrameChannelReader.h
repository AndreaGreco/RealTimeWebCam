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
	// Cheap once mapped: only re-stamps the header if the geometry changed. Returns
	// false (with a WINTRACE breadcrumb) if creation failed.
	bool EnsureMapped(uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen);

	bool IsMapped() const { return _header != nullptr; }

	// Points ppSlot at the latest fully-written NV12 slot (tightly packed, stride ==
	// width) and returns its identity/heartbeat, doing a bounded seqlock read. The
	// caller must copy the pixels out promptly; triple buffering guarantees the
	// producer won't overwrite this slot for ~2 frame intervals. Returns false if no
	// frame has been published yet or the read couldn't stabilize.
	bool AcquireLatest(const uint8_t** ppSlot, uint32_t* width, uint32_t* height, uint32_t* stride,
	                   uint64_t* frameSeq, uint64_t* framesWritten, uint64_t* heartbeatTickMs);

private:
	FrameChannelReader() = default;
	~FrameChannelReader();
	FrameChannelReader(const FrameChannelReader&) = delete;
	FrameChannelReader& operator=(const FrameChannelReader&) = delete;

	HANDLE _mapping = nullptr;
	VCamFrameChannelHeader* _header = nullptr;
};
