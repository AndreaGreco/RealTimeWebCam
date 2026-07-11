#pragma once
#include "..\Shared\VCamStats.h"

// Reads the live Frame-Server stats published by VirtualCamera/StatsPublisher.cpp
// into the shared-memory section described in Shared/VCamStats.h (Global\
// namespace, DACL, seqlock). This runs in the app process (RTVirtualCamera.exe),
// the reader side of that one-writer/one-reader channel.
//
// Exposed as a plain C function (VCam_GetFrameServerStats, see StatsReader.cpp)
// for P/Invoke from RTVirtualCamera/VirtualCameraWrapper.cs, mirroring the
// existing CreateVirtualCamera/RegisterVCam/... pattern in VirtualCamera.cpp.
class StatsReader
{
public:
	static StatsReader& Instance();

	// Reads the current snapshot with the seqlock retry loop described in
	// Shared/VCamStats.h. Returns false if the mapping doesn't exist yet (the
	// Frame Server never published — e.g. the virtual camera was never
	// started, or no consumer has opened it) or a read lost the retry race;
	// both are normal, recoverable states for the caller to just poll again.
	bool TryGetStats(VCamFrameServerStats& out);

private:
	StatsReader() = default;
	~StatsReader();
	StatsReader(const StatsReader&) = delete;
	StatsReader& operator=(const StatsReader&) = delete;

	// Opens the mapping on first use (and retries on every subsequent call
	// until it succeeds — unlike the writer side, "not published yet" is an
	// expected, temporary state here, not a permanent failure).
	bool EnsureMapped();

	HANDLE _mapping = nullptr;
	const VCamFrameServerStats* _view = nullptr;
};
