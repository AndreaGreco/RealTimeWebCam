#include "pch.h"
#include "StatsReader.h"

StatsReader& StatsReader::Instance()
{
	static StatsReader instance;
	return instance;
}

StatsReader::~StatsReader()
{
	if (_view)
		UnmapViewOfFile(const_cast<VCamFrameServerStats*>(_view));
	if (_mapping)
		CloseHandle(_mapping);
}

bool StatsReader::EnsureMapped()
{
	if (_view)
		return true;

	// FILE_MAP_READ only: this side never writes. The Frame Server (Local
	// Service) created the mapping with a DACL that allows Everyone GENERIC_READ
	// (see StatsPublisher::BuildStatsSecurityDescriptor) — without that, this
	// call would fail with ERROR_ACCESS_DENIED even though the name matches.
	HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, VCAM_STATS_MAPPING_NAME);
	if (!mapping)
		return false;

	const void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(VCamFrameServerStats));
	if (!view)
	{
		CloseHandle(mapping);
		return false;
	}

	_mapping = mapping;
	_view = static_cast<const VCamFrameServerStats*>(view);
	return true;
}

bool StatsReader::TryGetStats(VCamFrameServerStats& out)
{
	if (!EnsureMapped())
		return false;

	// Seqlock read (see Shared/VCamStats.h): retry while the writer is
	// mid-update (odd sequence) or the sequence changed during our read (the
	// writer raced us). Bounded so a pathological case can't spin forever —
	// the caller just polls again on its own timer if this returns false.
	for (int attempt = 0; attempt < 8; attempt++)
	{
		long seq1 = _view->sequence;
		if (seq1 & 1)
			continue; // writer in progress, try again

		VCamFrameServerStats snapshot = *_view; // plain struct copy (POD)

		long seq2 = _view->sequence;
		if (seq1 == seq2)
		{
			out = snapshot;
			return true;
		}
	}
	return false;
}

// ============================================================================
// C-style interface for C# interop
// ============================================================================

extern "C" {
	__declspec(dllexport) int VCam_GetFrameServerStats(VCamFrameServerStats* outStats)
	{
		if (!outStats)
			return -1;
		return StatsReader::Instance().TryGetStats(*outStats) ? 0 : -1;
	}
}
