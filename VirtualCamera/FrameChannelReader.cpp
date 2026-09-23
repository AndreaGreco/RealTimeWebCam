#include "pch.h"
#include "FrameChannelReader.h"
#include "WinTrace.h"
#include <mfapi.h>
#include <sddl.h>
#include <atomic>

#pragma comment(lib, "advapi32.lib")

FrameChannelReader& FrameChannelReader::Instance()
{
	static FrameChannelReader instance;
	return instance;
}

FrameChannelReader::~FrameChannelReader()
{
	if (_header)
		UnmapViewOfFile(_header);
	if (_mapping)
		CloseHandle(_mapping);
	if (_frameReadyEvent)
		CloseHandle(_frameReadyEvent);
}

// Local Service (the Frame Server) full control; Interactive Users read+write (the
// app is the writer). Mirrors StatsPublisher::BuildStatsSecurityDescriptor, but the
// app needs WRITE here, not just read — hence GRGW for IU instead of GR for WD.
static PSECURITY_DESCRIPTOR BuildFramesSecurityDescriptor()
{
	PSECURITY_DESCRIPTOR sd = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		L"D:(A;;GA;;;LS)(A;;GRGW;;;IU)", SDDL_REVISION_1, &sd, nullptr))
	{
		WINTRACE(L"FrameChannelReader::BuildFramesSecurityDescriptor - failed, error=%u", GetLastError());
		return nullptr;
	}
	return sd;
}

// Creates the frame-ready event next to the mapping, same "service creates, app opens"
// reason. Interactive Users get SYNCHRONIZE | EVENT_MODIFY_STATE (0x00100002): enough
// to SetEvent, nothing more. Failure only loses the optimization — the timer still
// drives delivery — so it is traced, not propagated.
void FrameChannelReader::EnsureFrameReadyEvent()
{
	if (_frameReadyEvent)
		return;

	PSECURITY_DESCRIPTOR sd = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		L"D:(A;;GA;;;LS)(A;;0x00100002;;;IU)", SDDL_REVISION_1, &sd, nullptr))
	{
		WINTRACE(L"FrameChannelReader::EnsureFrameReadyEvent - security descriptor failed, error=%u", GetLastError());
		sd = nullptr;
	}
	SECURITY_ATTRIBUTES sa{ sizeof(sa), sd, FALSE };

	HANDLE ev = CreateEventW(sd ? &sa : nullptr, FALSE /*auto-reset*/, FALSE, VCAM_FRAMES_EVENT_NAME);
	DWORD err = GetLastError();
	if (sd)
		LocalFree(sd);

	if (!ev)
	{
		WINTRACE(L"FrameChannelReader::EnsureFrameReadyEvent - CreateEventW failed, error=%u", err);
		return;
	}
	_frameReadyEvent = ev;
	WINTRACE(L"FrameChannelReader::EnsureFrameReadyEvent - frame-ready event created (reused existing=%d)",
		err == ERROR_ALREADY_EXISTS);
}

bool FrameChannelReader::EnsureMapped(uint32_t width, uint32_t height, uint32_t fpsNum, uint32_t fpsDen)
{
	if (width == 0 || height == 0 ||
		width > VCAM_FRAMES_MAX_WIDTH || height > VCAM_FRAMES_MAX_HEIGHT)
	{
		WINTRACE(L"FrameChannelReader::EnsureMapped - geometry %ux%u out of range", width, height);
		return false;
	}

	auto stampHeader = [&]() {
		// Publish the header under the seqlock so a mid-init writer/reader sees a
		// consistent geometry. The producer heartbeat/latestSlot are reset so a stale
		// section from a previous session doesn't look live until the app writes again.
		InterlockedIncrement(&_header->publishSeq); // -> odd
		_header->structVersion = VCAM_FRAMES_STRUCT_VERSION;
		_header->width = width;
		_header->height = height;
		_header->stride = width; // tightly packed NV12
		_header->fpsNum = fpsNum;
		_header->fpsDen = fpsDen;
		_header->format = MFVideoFormat_NV12;
		_header->slotCount = VCAM_FRAMES_SLOT_COUNT;
		_header->bytesPerSlot = VCamFrameChannel_SlotBytes(width, height); // page-rounded
		_header->latestSlot = -1;
		_header->frameSeq = 0;
		_header->producerHeartbeatTickMs = 0;
		_header->framesWritten = 0;
		InterlockedIncrement(&_header->publishSeq); // -> even
	};

	if (_header)
	{
		// Already mapped (fixed max size). Always re-stamp, even with the same geometry:
		// otherwise the previous session's latestSlot/heartbeat stay live and its last
		// frame can be served as "fresh" for up to 2s.
		stampHeader();
		EnsureFrameReadyEvent(); // no-op if already created; retries a past failure
		return true;
	}

	PSECURITY_DESCRIPTOR sd = BuildFramesSecurityDescriptor();
	SECURITY_ATTRIBUTES sa{ sizeof(sa), sd, FALSE };

	const uint32_t size = VCamFrameChannel_MaxMappingSize();
	HANDLE mapping = CreateFileMappingW(
		INVALID_HANDLE_VALUE, sd ? &sa : nullptr, PAGE_READWRITE,
		0, size, VCAM_FRAMES_MAPPING_NAME);
	DWORD createErr = GetLastError();
	if (sd)
		LocalFree(sd);

	if (!mapping)
	{
		WINTRACE(L"FrameChannelReader::EnsureMapped - CreateFileMappingW failed, error=%u", createErr);
		return false;
	}

	void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
	if (!view)
	{
		WINTRACE(L"FrameChannelReader::EnsureMapped - MapViewOfFile failed, error=%u", GetLastError());
		CloseHandle(mapping);
		return false;
	}

	// If a previous session's section still exists we reuse it, but zero + re-stamp
	// so no stale frame/heartbeat leaks into this session.
	ZeroMemory(view, sizeof(VCamFrameChannelHeader));
	_mapping = mapping;
	_header = static_cast<VCamFrameChannelHeader*>(view);
	stampHeader();
	WINTRACE(L"FrameChannelReader::EnsureMapped - mapping ready %ux%u (reused existing=%d)",
		width, height, createErr == ERROR_ALREADY_EXISTS);
	EnsureFrameReadyEvent();
	return true;
}

bool FrameChannelReader::AcquireLatest(const uint8_t** ppSlot, uint32_t* width, uint32_t* height, uint32_t* stride,
                                       uint64_t* frameSeq, uint64_t* framesWritten, uint64_t* heartbeatTickMs)
{
	if (!_header || !ppSlot)
		return false;

	// Bounded seqlock read of the metadata (which slot + identity). The pixels
	// themselves are not under the lock — triple buffering keeps the returned slot
	// stable long enough for the caller's copy.
	for (int attempt = 0; attempt < 8; ++attempt)
	{
		long seq0 = _header->publishSeq;
		if (seq0 & 1)
			continue; // writer mid-update
		// Keep the plain (non-volatile) field reads between the two publishSeq reads.
		std::atomic_thread_fence(std::memory_order_acquire);
		uint32_t ver = _header->structVersion;
		long latest = _header->latestSlot;
		uint64_t fseq = _header->frameSeq;
		uint64_t fwritten = _header->framesWritten;
		uint64_t hb = _header->producerHeartbeatTickMs;
		uint32_t w = _header->width, h = _header->height, st = _header->stride;
		uint32_t slots = _header->slotCount;
		std::atomic_thread_fence(std::memory_order_acquire);
		long seq1 = _header->publishSeq;
		if (seq0 != seq1)
			continue; // changed mid-read, retry

		if (ver != VCAM_FRAMES_STRUCT_VERSION)
			return false; // unknown layout: don't trust anything else in it
		if (latest < 0 || (uint32_t)latest >= slots)
			return false; // no frame published yet

		if (ppSlot) *ppSlot = VCamFrameChannel_SlotPtr(_header, (uint32_t)latest);
		if (width) *width = w;
		if (height) *height = h;
		if (stride) *stride = st;
		if (frameSeq) *frameSeq = fseq;
		if (framesWritten) *framesWritten = fwritten;
		if (heartbeatTickMs) *heartbeatTickMs = hb;
		return true;
	}
	return false;
}

bool FrameChannelReader::IsSlotStillValid(uint64_t acquiredFrameSeq) const
{
	if (!_header)
		return false;

	// Order the re-read after the caller's pixel copy.
	std::atomic_thread_fence(std::memory_order_acquire);
	const uint64_t current = _header->frameSeq;
	const uint32_t slots = _header->slotCount;
	// The writer fills latest+1, latest+2, then latest again: once it has published
	// slotCount-1 newer frames, the next write targets the slot we copied from. A
	// re-stamp (frameSeq reset below acquired) also invalidates it; the unsigned
	// difference wraps to a huge value in that case.
	return current - acquiredFrameSeq < (uint64_t)(slots > 1 ? slots - 1 : 1);
}
