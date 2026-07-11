#include "pch.h"
#include "StatsPublisher.h"
#include "WinTrace.h"
#include <sddl.h>

#pragma comment(lib, "advapi32.lib") // ConvertStringSecurityDescriptorToSecurityDescriptorW

StatsPublisher& StatsPublisher::Instance()
{
	static StatsPublisher instance;
	return instance;
}

StatsPublisher::~StatsPublisher()
{
	if (_view)
		UnmapViewOfFile(_view);
	if (_mapping)
		CloseHandle(_mapping);
}

// Local Service (the Frame Server's identity) gets full control; everyone else
// gets read-only. Without this explicit DACL, the default security descriptor
// for an object created by a service does not grant the interactive user's
// process (RTVirtualCamera.exe) access, and OpenFileMappingW on the app side
// fails with ERROR_ACCESS_DENIED.
static PSECURITY_DESCRIPTOR BuildStatsSecurityDescriptor()
{
	PSECURITY_DESCRIPTOR sd = nullptr;
	// D: DACL. (A;;GA;;;LS) = allow Generic-All to LOCAL SERVICE.
	//           (A;;GR;;;WD) = allow Generic-Read to Everyone.
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		L"D:(A;;GA;;;LS)(A;;GR;;;WD)", SDDL_REVISION_1, &sd, nullptr))
	{
		WINTRACE(L"StatsPublisher::BuildStatsSecurityDescriptor - ConvertStringSecurityDescriptorToSecurityDescriptorW failed, error=%u", GetLastError());
		return nullptr;
	}
	return sd;
}

bool StatsPublisher::EnsureMapped()
{
	if (_view)
		return true;
	if (_initAttempted)
		return false; // already tried and failed once; don't retry every frame
	_initAttempted = true;

	PSECURITY_DESCRIPTOR sd = BuildStatsSecurityDescriptor();
	SECURITY_ATTRIBUTES sa{ sizeof(sa), sd, FALSE };

	HANDLE mapping = CreateFileMappingW(
		INVALID_HANDLE_VALUE, sd ? &sa : nullptr, PAGE_READWRITE,
		0, sizeof(VCamFrameServerStats), VCAM_STATS_MAPPING_NAME);
	DWORD createErr = GetLastError();

	if (sd)
		LocalFree(sd);

	if (!mapping)
	{
		WINTRACE(L"StatsPublisher::EnsureMapped - CreateFileMappingW failed, error=%u", createErr);
		return false;
	}

	void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(VCamFrameServerStats));
	if (!view)
	{
		WINTRACE(L"StatsPublisher::EnsureMapped - MapViewOfFile failed, error=%u", GetLastError());
		CloseHandle(mapping);
		return false;
	}

	// ERROR_ALREADY_EXISTS just means a previous camera session's mapping is
	// still alive (some handle to it survived); we reuse it and stamp our own
	// version/zeroed counters into it below.
	ZeroMemory(view, sizeof(VCamFrameServerStats));
	static_cast<VCamFrameServerStats*>(view)->structVersion = VCAM_STATS_STRUCT_VERSION;

	_mapping = mapping;
	_view = static_cast<VCamFrameServerStats*>(view);
	WINTRACE(L"StatsPublisher::EnsureMapped - mapping ready (reused existing=%d)", createErr == ERROR_ALREADY_EXISTS);
	return true;
}

void StatsPublisher::Publish(const VCamFrameServerStats& snapshot)
{
	if (!EnsureMapped())
		return;

	// Seqlock write: odd while in progress, even + advanced once done. This is
	// the only synchronization primitive needed because there is exactly one
	// writer — MediaStream::RequestSample, itself already serialized by
	// MediaStream::_lock upstream of the Publish() call.
	InterlockedIncrement(&_view->sequence); // -> odd
	_view->structVersion = VCAM_STATS_STRUCT_VERSION;
	_view->updatedTickMs = GetTickCount64();
	_view->sessionState = snapshot.sessionState;
	_view->rxFrames = snapshot.rxFrames;
	_view->renderedFrames = snapshot.renderedFrames;
	_view->droppedFrames = snapshot.droppedFrames;
	_view->declinedFrames = snapshot.declinedFrames;
	_view->driftMs = snapshot.driftMs;
	_view->lastCopyMs = snapshot.lastCopyMs;
	_view->width = snapshot.width;
	_view->height = snapshot.height;
	_view->fpsNum = snapshot.fpsNum;
	_view->fpsDen = snapshot.fpsDen;
	_view->hwAccelCapable = snapshot.hwAccelCapable;
	_view->hwAccelActive = snapshot.hwAccelActive;
	InterlockedIncrement(&_view->sequence); // -> even
}
