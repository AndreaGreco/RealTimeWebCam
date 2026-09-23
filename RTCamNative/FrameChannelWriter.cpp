#include "FrameChannelWriter.h"
#include "Logger.h"
#include <cstring>
#include <cstdio>

FrameChannelWriter::~FrameChannelWriter()
{
	Close();
}

bool FrameChannelWriter::EnsureOpen()
{
	if (_header)
		return true;

	// FILE_MAP_WRITE implies read on a PAGE_READWRITE section. The Frame Server's
	// DACL grants Interactive Users read+write, so this succeeds for the app's
	// (non-elevated) interactive process once the mapping exists.
	HANDLE mapping = OpenFileMappingW(FILE_MAP_WRITE | FILE_MAP_READ, FALSE, VCAM_FRAMES_MAPPING_NAME);
	if (!mapping)
		return false; // not created yet (or access denied) — caller retries

	void* view = MapViewOfFile(mapping, FILE_MAP_WRITE | FILE_MAP_READ, 0, 0, 0);
	if (!view)
	{
		CloseHandle(mapping);
		return false;
	}

	auto* header = static_cast<VCamFrameChannelHeader*>(view);
	// The Frame Server stamps the header before the app can meaningfully write.
	// If it looks half-initialized, back off and retry on the next call.
	if (header->structVersion != VCAM_FRAMES_STRUCT_VERSION ||
		header->width == 0 || header->height == 0 ||
		header->slotCount == 0 || header->slotCount > 16 ||
		header->bytesPerSlot == 0)
	{
		UnmapViewOfFile(view);
		CloseHandle(mapping);
		return false;
	}

	_mapping = mapping;
	_header = header;
	DebugLog("FrameChannelWriter::EnsureOpen - mapping opened");
	return true;
}

void FrameChannelWriter::WriteFrame(const uint8_t* srcY, int srcStrideY,
                                    const uint8_t* srcUV, int srcStrideUV,
                                    uint32_t width, uint32_t height)
{
	if (!_header || !srcY || !srcUV)
		return;

	// Snapshot the header geometry once: the Frame Server may re-stamp it at any
	// time (re-activation with a different config), so every bound below comes
	// from this single read, never from _header again.
	const uint32_t hdrWidth = _header->width;
	const uint32_t hdrHeight = _header->height;
	const uint32_t dstStride = _header->stride; // writer/reader agree: tightly packed (stride == width)
	const uint32_t slotCount = _header->slotCount;
	const uint32_t bytesPerSlot = _header->bytesPerSlot;
	const uint32_t copyBytes = width; // per-row payload for both Y and interleaved-UV planes

	// The source buffer is exactly width x height. If the header asks for another
	// geometry (or is inconsistent), copying header-sized rows would read past the
	// source or write past the section — skip the frame instead.
	const bool geometryOk =
		width == hdrWidth && height == hdrHeight &&
		dstStride >= width &&
		slotCount != 0 && slotCount <= 16 &&
		VCamFrameChannel_Nv12Bytes(width, height) <= bytesPerSlot &&
		(uint64_t)dstStride * height * 3u / 2u <= bytesPerSlot &&
		sizeof(VCamFrameChannelHeader) + (uint64_t)slotCount * bytesPerSlot <= VCamFrameChannel_MaxMappingSize();
	if (!geometryOk)
	{
		if (!_geometryMismatch)
		{
			char msg[192];
			sprintf_s(msg, "FrameChannelWriter::WriteFrame - geometry mismatch, skipping frames "
				"(frame %ux%u, header %ux%u stride=%u slots=%u bytesPerSlot=%u)",
				width, height, hdrWidth, hdrHeight, dstStride, slotCount, bytesPerSlot);
			DebugLog(msg);
			_geometryMismatch = true;
		}
		return;
	}
	if (_geometryMismatch)
	{
		DebugLog("FrameChannelWriter::WriteFrame - geometry matches the header again, resuming writes");
		_geometryMismatch = false;
	}

	// Choose the next ring slot. Only this thread writes latestSlot, so a plain
	// read is fine. Treat the initial -1 (or an out-of-range index left by a
	// re-stamp with fewer slots) as "start at slot 0".
	long cur = _header->latestSlot;
	uint32_t next = (cur < 0 || (uint32_t)cur >= slotCount) ? 0u : ((uint32_t)cur + 1u) % slotCount;

	// Same arithmetic as VCamFrameChannel_SlotPtr, but on the snapshotted bytesPerSlot.
	uint8_t* dst = reinterpret_cast<uint8_t*>(_header) + sizeof(VCamFrameChannelHeader)
		+ (size_t)next * bytesPerSlot;
	uint8_t* dstY = dst;
	uint8_t* dstUV = dst + (size_t)dstStride * height;

	// Y plane: height rows.
	for (uint32_t row = 0; row < height; ++row)
		memcpy(dstY + (size_t)row * dstStride, srcY + (size_t)row * srcStrideY, copyBytes);
	// UV plane (interleaved): height/2 rows.
	for (uint32_t row = 0; row < height / 2; ++row)
		memcpy(dstUV + (size_t)row * dstStride, srcUV + (size_t)row * srcStrideUV, copyBytes);

	// Publish under the header seqlock: bump odd, update metadata, bump even. The
	// pixels above are in a slot the reader is not currently reading (triple
	// buffering), so only the small metadata needs the lock.
	InterlockedIncrement(&_header->publishSeq); // -> odd
	_header->latestSlot = (long)next;
	_header->frameSeq++;
	_header->framesWritten++;
	_header->producerHeartbeatTickMs = GetTickCount64();
	InterlockedIncrement(&_header->publishSeq); // -> even
}

void FrameChannelWriter::Close()
{
	if (_header)
	{
		UnmapViewOfFile(_header);
		_header = nullptr;
	}
	if (_mapping)
	{
		CloseHandle(_mapping);
		_mapping = nullptr;
	}
	_geometryMismatch = false;
}
