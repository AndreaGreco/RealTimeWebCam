#pragma once
#include "..\Shared\VCamStats.h"

// Publishes live Frame-Server-side stats (RX/render/drop counters, drift,
// last copy cost, HW acceleration state) into the cross-session shared memory
// section defined in Shared/VCamStats.h, so the app process can display them
// without any RPC/COM round-trip into the Frame Server. See that header for
// the full rationale (Global\ namespace, DACL, seqlock).
//
// Singleton, same pattern as RtspSessionManager::Instance(). The mapping is
// created lazily on the first Publish() call and lives for the DLL's
// lifetime; there is no explicit teardown. A stale mapping from a finished
// session is harmless — a reader can tell it is stale because updatedTickMs
// stops advancing.
class StatsPublisher
{
public:
	static StatsPublisher& Instance();

	// Cheap: two InterlockedIncrement calls around a handful of field writes,
	// no allocation, no syscall on the steady-state path (after the mapping is
	// created). Safe to call from MediaStream::RequestSample (~30x/sec) on the
	// Frame Server's latency-critical frame path.
	void Publish(const VCamFrameServerStats& snapshot);

private:
	StatsPublisher() = default;
	~StatsPublisher();
	StatsPublisher(const StatsPublisher&) = delete;
	StatsPublisher& operator=(const StatsPublisher&) = delete;

	// Creates the mapping + view on first use. Returns false (and leaves a
	// WINTRACE breadcrumb) if it failed; does not retry on every call once it
	// has failed once, so a permanently broken environment doesn't cost
	// anything on the hot path.
	bool EnsureMapped();

	HANDLE _mapping = nullptr;
	VCamFrameServerStats* _view = nullptr;
	bool _initAttempted = false;
};
