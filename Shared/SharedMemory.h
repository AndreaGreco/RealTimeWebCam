#pragma once

#include <windows.h>
#include <stdint.h>
#include <unknwn.h>
#include <mfapi.h>
#include <mfidl.h>
#include <atomic>

#define SHARED_MEMORY_NAME L"Global\\MFPipeline_SharedMemory"
#define FRAME_READY_EVENT_NAME L"Global\\MFPipeline_FrameReady"

#define MAX_FRAME_WIDTH 1920
#define MAX_FRAME_HEIGHT 1080
#define MAX_FRAME_SIZE (MAX_FRAME_WIDTH * MAX_FRAME_HEIGHT * 4)
#define CIRCULAR_BUFFER_FRAMES 120
#define INIT_MAGIC_NUMBER 0x3D62C73E

struct FrameSlot
{
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format;  // FOURCC code
    uint64_t timestamp;
    uint32_t buff_size;
    uint8_t buff[MAX_FRAME_SIZE];
};

// Header della memoria condivisa
struct SharedMemoryHeader
{
    std::atomic<uint32_t> writeIndex;
    std::atomic<uint32_t> readIndex;
    uint32_t magic;

    // Padding per allineamento cache
    uint8_t padding[48];
};

// Layout completo della memoria condivisa
struct SharedMemoryLayout
{
    SharedMemoryHeader header;
    FrameSlot frames[CIRCULAR_BUFFER_FRAMES];
};
struct ShmFrame_st {
    uint64_t timestamp;
    uint32_t buff_size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t pixel_format;
    uint32_t _gap1;

    uint8_t* buff;
};

// Gestore della memoria condivisa (Consumer side - solo per VirtualCamera)
class SharedMemoryConsumer
{
private:
    SharedMemoryLayout* shared;
    HANDLE hFrameReadyEvent;
    HANDLE hMapFile;

public:
    SharedMemoryConsumer();
    ~SharedMemoryConsumer();

    HRESULT Initialize();
    
    HRESULT TryReadFrame(struct ShmFrame_st* frame);
    
    void Cleanup();
    
private:
    uint32_t GetAvailableFrames() const;
};

class SharedMemoryProducer
{
private:
    SharedMemoryLayout* shared;
    HANDLE hFrameReadyEvent;
    HANDLE hMapFile;
    bool enabled;

public:
    SharedMemoryProducer();
    ~SharedMemoryProducer();

    HRESULT Initialize();
    void ResetIndex();
    void SetEnabled(bool enable);
    bool IsEnabled();

    HRESULT WriteFrame(struct ShmFrame_st* out);

    void Cleanup();

private:
    uint32_t GetAvailableSlots() const;
};