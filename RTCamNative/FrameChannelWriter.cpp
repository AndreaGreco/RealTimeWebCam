#include "FrameChannelWriter.h"
#include "Logger.h"
#include <cstring>

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
                                    const uint8_t* srcUV, int srcStrideUV)
{
	if (!_header || !srcY || !srcUV)
		return;

	const uint32_t width = _header->width;
	const uint32_t height = _header->height;
	const uint32_t dstStride = _header->stride; // writer/reader agree: tightly packed (stride == width)
	const uint32_t copyBytes = width; // per-row payload for both Y and interleaved-UV planes

	// Choose the next ring slot. Only this thread writes latestSlot, so a plain
	// read is fine. Treat the initial -1 as "start at slot 0".
	long cur = _header->latestSlot;
	uint32_t next = (cur < 0) ? 0u : ((uint32_t)cur + 1u) % _header->slotCount;

	uint8_t* dst = VCamFrameChannel_SlotPtr(_header, next);
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
}
