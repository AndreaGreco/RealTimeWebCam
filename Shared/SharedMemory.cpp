#include "pch.h"
#include <atomic>
#include <sstream>
#include "SharedMemory.h"

SharedMemoryConsumer::SharedMemoryConsumer()
{
    this->hMapFile = NULL;
    this->hFrameReadyEvent = NULL;
    this->shared = NULL;
}

SharedMemoryConsumer::~SharedMemoryConsumer()
{
    this->Cleanup();
}

HRESULT SharedMemoryConsumer::Initialize()
{
    SIZE_T sz = sizeof(SharedMemoryLayout);

    hMapFile = OpenFileMappingW(FILE_MAP_READ, FALSE, SHARED_MEMORY_NAME);
    if (hMapFile == NULL) {
        DWORD error;
        
        error = GetLastError();
        return HRESULT_FROM_WIN32(error);
    }
    
    
    this->shared = (SharedMemoryLayout*)MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, sz);
    if (this->shared == nullptr) {
        DWORD error;

        error = GetLastError();
        this->Cleanup();
        return HRESULT_FROM_WIN32(error);
    }

    hFrameReadyEvent = OpenEventW(SYNCHRONIZE, FALSE, FRAME_READY_EVENT_NAME);
    if (hFrameReadyEvent == NULL) {
        DWORD error;

        error  = GetLastError();
        this->Cleanup();
        return HRESULT_FROM_WIN32(error);
    }

    if (this->shared->header.magic != INIT_MAGIC_NUMBER)
        return S_FALSE;
    
    return S_OK;
}

uint32_t SharedMemoryConsumer::GetAvailableFrames() const
{
    uint32_t produced, consumed;

    if (this->shared == NULL)
        return 0;
    
   produced = this->shared->header.writeIndex;
   consumed = this->shared->header.readIndex;
    
    return produced - consumed;
}

HRESULT SharedMemoryConsumer::TryReadFrame(struct ShmFrame_st *out)
{
    FrameSlot *frame;
    uint32_t index;

    /* Memory init fails */
    if ( (this->shared == NULL) || (out == NULL))
        return E_FAIL;
    
    /* Check if is there some frame to read */
    if (GetAvailableFrames() == 0)
        return S_FALSE;
    
    index = this->shared->header.readIndex.load() % CIRCULAR_BUFFER_FRAMES;
    frame = &this->shared->frames[index];
      
    // Copy data to frame
    out->width = frame->width;
    out->height = frame->height;
    out->stride = frame->stride;
    out->pixel_format = frame->pixel_format;
    out->timestamp = frame->timestamp;
    out->buff_size = frame->buff_size;
    memcpy(out->buff, frame->buff, out->buff_size);
    
	shared->header.readIndex.fetch_add(1);
    
    return S_OK;
}

void SharedMemoryConsumer::Cleanup()
{
    if (this->shared) {
        UnmapViewOfFile(this->shared);
        this->shared = nullptr;
    }
    
    if (hMapFile) {
        CloseHandle(hMapFile);
        hMapFile = NULL;
    }
    
    if (hFrameReadyEvent) {
        CloseHandle(hFrameReadyEvent);
        hFrameReadyEvent = NULL;
    }
}

// ============================================================================
// SharedMemoryProducer Implementation
// ============================================================================

SharedMemoryProducer::SharedMemoryProducer() {
    this->hMapFile = NULL;
    this->hFrameReadyEvent = NULL;
    this->shared = NULL;
	this->enabled = false;
}

SharedMemoryProducer::~SharedMemoryProducer()
{
    Cleanup();
}

HRESULT SharedMemoryProducer::Initialize()
{
	DWORD sz;
    SECURITY_ATTRIBUTES sa = { 0 };
    PSECURITY_DESCRIPTOR pSD = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
    InitializeSecurityDescriptor(pSD, SECURITY_DESCRIPTOR_REVISION);
    SetSecurityDescriptorDacl(pSD, TRUE, NULL, FALSE); // Permette accesso a tutti

    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;
    sz = sizeof(SharedMemoryLayout);

    HANDLE hMapFile = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        &sa,
        PAGE_READWRITE,
        0,
        sz,
        SHARED_MEMORY_NAME
    );

    if (hMapFile == NULL) {
        DWORD error;
        
        error = GetLastError();
        return HRESULT_FROM_WIN32(error);
    }

    this->shared = (SharedMemoryLayout*)MapViewOfFile(
        hMapFile,
        FILE_MAP_ALL_ACCESS,
        0,
        0,
        sizeof(SharedMemoryLayout)
    );

    if (this->shared == nullptr)
    {
        DWORD error = GetLastError();
        Cleanup();
        return HRESULT_FROM_WIN32(error);
    }

    memset(&this->shared->header, 0, sizeof(SharedMemoryHeader));
    this->shared->header.writeIndex = 0;
    this->shared->header.readIndex = 0;
    this->shared->header.magic = INIT_MAGIC_NUMBER;
    hFrameReadyEvent = CreateEventW(&sa, FALSE, FALSE, FRAME_READY_EVENT_NAME);
    if (hFrameReadyEvent == NULL)
    {
        DWORD error;

        error = GetLastError();
        Cleanup();
        return HRESULT_FROM_WIN32(error);
    }

    return S_OK;
}

uint32_t SharedMemoryProducer::GetAvailableSlots() const {
    uint32_t occupiedSlots, consumed, produced;

    if (this->shared == NULL) {
        return -1;
    }

    produced = this->shared->header.writeIndex.load();
    consumed = this->shared->header.readIndex.load();

    occupiedSlots = produced - consumed;

    if (occupiedSlots > CIRCULAR_BUFFER_FRAMES)
        occupiedSlots = CIRCULAR_BUFFER_FRAMES;

    return CIRCULAR_BUFFER_FRAMES - occupiedSlots;
}

HRESULT SharedMemoryProducer::WriteFrame(struct ShmFrame_st *out)
{
    uint32_t idx, aviable_slot;
    FrameSlot *slot;

    if (this->shared == NULL)
        return E_FAIL;

    aviable_slot = this->GetAvailableSlots();
    if (aviable_slot == 0) {
        return S_FALSE;
    }

    idx = this->shared->header.writeIndex % CIRCULAR_BUFFER_FRAMES;
    slot = &shared->frames[idx];

    slot->width = out->width;
    slot->height = out->height;
    slot->stride = out->stride;
    slot->pixel_format = out->pixel_format;
    slot->timestamp = out->timestamp;
    slot->buff_size = out->buff_size;
    memcpy(slot->buff, out->buff, slot->buff_size);
    
	shared->header.writeIndex.fetch_add(1);

    SetEvent(hFrameReadyEvent);

    return S_OK;
}

void SharedMemoryProducer::ResetIndex() {
	this->shared->header.writeIndex.store(0);
	this->shared->header.readIndex.store(0);
}

void SharedMemoryProducer::SetEnabled(bool enable) {
	if(enable)
        this->ResetIndex();

    this->enabled = enable;
}

void SharedMemoryProducer::Cleanup()
{
    if (this->shared) {
        UnmapViewOfFile(this->shared);
        this->shared = nullptr;
    }

    if (hMapFile) {
        CloseHandle(hMapFile);
        hMapFile = NULL;
    }

    if (hFrameReadyEvent) {
        CloseHandle(hFrameReadyEvent);
        hFrameReadyEvent = NULL;
    }
}

bool SharedMemoryProducer::IsEnabled() {
    return this->enabled;
}