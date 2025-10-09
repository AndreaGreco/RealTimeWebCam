#include "pch.h"
#include "VideoReaderCbk.h"
#include "Logger.h"
#include <string>
#include <stdio.h>

/* Frame aviable for the pipeline */
#define SAMPLE_ALLOCATOR_COUNT 100

VideoReaderCall::VideoReaderCall(IMFStreamSink* pSink) : m_pStreamSink(pSink), m_width(0), m_height(0) {
    if (m_pStreamSink)
        m_pStreamSink->AddRef();
    m_subtype = GUID_NULL;
}

VideoReaderCall::~VideoReaderCall() {
    if (m_pStreamSink)
        m_pStreamSink->Release();

    if (pD3DManager)
        pD3DManager->Release();

    if (pVideoSampleAllocator)
        pVideoSampleAllocator->Release();
}

// Helper function to copy NV12 format
HRESULT VideoReaderCall::CopyNV12Buffer(IMF2DBuffer* p2DBuffer, BYTE* pbSrcData, DWORD cbSrcLength) {
    HRESULT ret;
    BYTE* pbScanline0 = nullptr;
    LONG lPitch = 0;
    
    ret = p2DBuffer->Lock2D(&pbScanline0, &lPitch);
    if (ret != S_OK) {
        DebugLog("Failed to lock 2D buffer for NV12");
        return ret;
    }

    // Calculate expected sizes
    DWORD cbYPlane = m_width * m_height;
    DWORD cbUVPlane = m_width * (m_height / 2);
    DWORD cbExpected = cbYPlane + cbUVPlane;

    if (cbSrcLength < cbExpected) {
        DebugLog("Source buffer too small for NV12 format");
        p2DBuffer->Unlock2D();
        return E_INVALIDARG;
    }

    // Copy Y plane (full resolution)
    // Parameters: destPtr, destStride, srcPtr, srcStride, widthInBytes, lines
    ret = MFCopyImage(pbScanline0, lPitch, pbSrcData, m_width, m_width, m_height);
    if (FAILED(ret)) {
        DebugLog("Failed to copy Y plane");
        p2DBuffer->Unlock2D();
        return ret;
    }
    
    // Copy UV plane (half resolution, interleaved)
    ret = MFCopyImage(pbScanline0 + (lPitch * m_height), lPitch,
                      pbSrcData + cbYPlane, m_width, m_width, m_height / 2);
    if (FAILED(ret)) {
        DebugLog("Failed to copy UV plane");
        p2DBuffer->Unlock2D();
        return ret;
    }
    
    p2DBuffer->Unlock2D();
    return S_OK;
}

// Helper function to copy YUY2 format
HRESULT VideoReaderCall::CopyYUY2Buffer(IMF2DBuffer* p2DBuffer, BYTE* pbSrcData, DWORD cbSrcLength) {
    HRESULT ret;
    BYTE* pbScanline0 = nullptr;
    LONG lPitch = 0;
    
    ret = p2DBuffer->Lock2D(&pbScanline0, &lPitch);
    if (ret != S_OK) {
        DebugLog("Failed to lock 2D buffer for YUY2");
        return ret;
    }

    // YUY2 is 2 bytes per pixel (packed format: Y0 U0 Y1 V0)
    DWORD cbRowSize = m_width * 2;
    DWORD cbExpected = cbRowSize * m_height;

    if (cbSrcLength < cbExpected) {
        char errorLog[256];
        snprintf(errorLog, sizeof(errorLog), 
                "WARNING: Source buffer smaller than expected for YUY2: expected %lu, got %lu - this might be compressed data!",
                cbExpected, cbSrcLength);
        DebugLog(errorLog);
        p2DBuffer->Unlock2D();
        return E_INVALIDARG;
    }

    // Copy YUY2 data (single packed plane)
    // Parameters: destPtr, destStride, srcPtr, srcStride, widthInBytes, lines
    ret = MFCopyImage(pbScanline0, lPitch, pbSrcData, cbRowSize, cbRowSize, m_height);
    if (FAILED(ret)) {
        char errorLog[128];
        snprintf(errorLog, sizeof(errorLog), "Failed to copy YUY2 data, HRESULT: 0x%X", ret);
        DebugLog(errorLog);
        p2DBuffer->Unlock2D();
        return ret;
    }
    
    p2DBuffer->Unlock2D();
    return S_OK;
}

// Helper function to copy RGB32 format
HRESULT VideoReaderCall::CopyRGB32Buffer(IMF2DBuffer* p2DBuffer, BYTE* pbSrcData, DWORD cbSrcLength) {
    HRESULT ret;
    BYTE* pbScanline0 = nullptr;
    LONG lPitch = 0;
    
    ret = p2DBuffer->Lock2D(&pbScanline0, &lPitch);
    if (ret != S_OK) {
        DebugLog("Failed to lock 2D buffer for RGB32");
        return ret;
    }

    // RGB32 is 4 bytes per pixel
    DWORD cbRowSize = m_width * 4;
    DWORD cbExpected = cbRowSize * m_height;

    if (cbSrcLength < cbExpected) {
        DebugLog("Source buffer too small for RGB32 format");
        p2DBuffer->Unlock2D();
        return E_INVALIDARG;
    }

    // Copy RGB32 data (single plane)
    // Parameters: destPtr, destStride, srcPtr, srcStride, widthInBytes, lines
    ret = MFCopyImage(pbScanline0, lPitch, pbSrcData, cbRowSize, cbRowSize, m_height);
    if (FAILED(ret)) {
        DebugLog("Failed to copy RGB32 data");
        p2DBuffer->Unlock2D();
        return ret;
    }
    
    p2DBuffer->Unlock2D();
    return S_OK;
}

HRESULT VideoReaderCall::AllocateInternalBuffer(IMFMediaType* SinkMediaType, IMFSourceReader* pReader) {
    HRESULT ret;

    this->pReader = pReader;

    // Get video format information for proper buffer size calculation
    ret = MFGetAttributeSize(SinkMediaType, MF_MT_FRAME_SIZE, &m_width, &m_height);
    if (ret != S_OK) {
        DebugLog("Failed to get frame size from media type");
        return ret;
    }

    ret = SinkMediaType->GetGUID(MF_MT_SUBTYPE, &m_subtype);
    if (ret != S_OK) {
        DebugLog("Failed to get subtype from media type");
        return ret;
    }

    ret = MFGetService(m_pStreamSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pVideoSampleAllocator));
    if (ret != S_OK) {
        DebugLog("Failed to get video sample allocator");
        return ret;
    }

    ret = MFGetService(m_pStreamSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pD3DManager));
    if (ret != S_OK) {
        DebugLog("Failed to get D3D manager");
        return ret;
    }

    ret = pVideoSampleAllocator->SetDirectXManager(pD3DManager);
    if (ret != S_OK) {
        DebugLog("Failed to set DirectX manager");
        return ret;
    }

    // For RTSP real-time: use minimal buffer pool (3 samples) for low latency
    ret = pVideoSampleAllocator->InitializeSampleAllocator(SAMPLE_ALLOCATOR_COUNT, SinkMediaType);
    if (ret != S_OK) {
        DebugLog("Failed to initialize sample allocator");
        return ret;
    }

    DebugLog("VideoReaderCallback initialized successfully for real-time streaming");
    return S_OK;
}

STDMETHODIMP VideoReaderCall::QueryInterface(REFIID riid, void** ppv) {
    static const QITAB qit[] = {
        QITABENT(VideoReaderCall, IMFSourceReaderCallback),
        { 0 }
    };
    return QISearch(this, qit, riid, ppv);
}

STDMETHODIMP_(ULONG) VideoReaderCall::AddRef() {
    return InterlockedIncrement(&m_cRef);
}

STDMETHODIMP_(ULONG) VideoReaderCall::Release() {
    ULONG uCount = InterlockedDecrement(&m_cRef);
    if (uCount == 0) delete this;
    return uCount;
}

// Callback chiamato quando un frame è pronto dal decoder
STDMETHODIMP VideoReaderCall::OnReadSample(HRESULT hrStatus,
    DWORD dwStreamIndex,
    DWORD dwStreamFlags,
    LONGLONG llTimestamp,
    IMFSample* pSample)
{
    HRESULT ret;

    char logBuffer[256];
    snprintf(logBuffer, sizeof(logBuffer),
        "OnReadSample: hrStatus=0x%X, dwStreamIndex=%lu, dwStreamFlags=0x%X, llTimestamp=%lld, pSample=%p",
        hrStatus, dwStreamIndex, dwStreamFlags, llTimestamp, pSample);
	DebugLog(logBuffer);

    if (FAILED(hrStatus)) {
        DebugLog("OnReadSample failed status");
        return hrStatus;
    }

    if (pSample == NULL) {
        // Request next frame
        if (pReader) {
            pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        }
        return S_OK;
    }

    if (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
        DebugLog("End of stream reached");
        return S_OK;
    }

    // Get a new D3D surface from the allocator pool
    IMFSample* pD3DSample = nullptr;
    ret = pVideoSampleAllocator->AllocateSample(&pD3DSample);
    if (ret != S_OK) {
        DebugLog("Failed to allocate D3D sample from pool");
        // Request next frame anyway
        if (pReader) {
            pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        }
        return ret;
    }

    // Copy timing information with NORMALIZED timestamp for smooth playback
    LONGLONG sampleDuration;
    pSample->GetSampleDuration(&sampleDuration);
    pD3DSample->SetSampleDuration(sampleDuration);
    pD3DSample->SetSampleTime(llTimestamp);

    // Get source buffer
    IMFMediaBuffer* pSrcBuffer = nullptr;
    ret = pSample->ConvertToContiguousBuffer(&pSrcBuffer);
    if (ret != S_OK) {
        pD3DSample->Release();
        DebugLog("Failed to get contiguous buffer from source");
        // Request next frame anyway
        if (pReader) {
            pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        }
        return ret;
    }

    // Get destination D3D buffer
    IMFMediaBuffer* pDstBuffer = nullptr;
    ret = pD3DSample->GetBufferByIndex(0, &pDstBuffer);
    if (ret != S_OK) {
        pSrcBuffer->Release();
        pD3DSample->Release();
        DebugLog("Failed to get destination buffer");
        if (pReader) {
            pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        }
        return ret;
    }

    // Lock source buffer
    BYTE* pbSrcData = nullptr;
    DWORD cbSrcLength = 0;
    ret = pSrcBuffer->Lock(&pbSrcData, nullptr, &cbSrcLength);
    if (ret != S_OK) {
        pDstBuffer->Release();
        pSrcBuffer->Release();
        pD3DSample->Release();
        DebugLog("Failed to lock source buffer");
        if (pReader) {
            pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
        }
        return ret;
    }

    char bufferLog[256];
    snprintf(bufferLog, sizeof(bufferLog), 
            "Buffer size: %lu bytes, Format: %08X-%04X-%04X, Expected for %ux%u",
            cbSrcLength, m_subtype.Data1, m_subtype.Data2, m_subtype.Data3, m_width, m_height);
    DebugLog(bufferLog);

    // Lock destination buffer using 2D interface
    IMF2DBuffer* p2DBuffer = nullptr;
    ret = pDstBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer));
    if (ret == S_OK) {
        // Check format and use appropriate copy function
        if (m_subtype == MFVideoFormat_NV12) {
            ret = CopyNV12Buffer(p2DBuffer, pbSrcData, cbSrcLength);
            if (ret != S_OK) {
                DebugLog("Failed to copy NV12 buffer");
            }
        } else if (m_subtype == MFVideoFormat_YUY2) {
            ret = CopyYUY2Buffer(p2DBuffer, pbSrcData, cbSrcLength);
            if (ret != S_OK) {
                DebugLog("Failed to copy YUY2 buffer");
            }
        } else if (m_subtype == MFVideoFormat_RGB32) {
            ret = CopyRGB32Buffer(p2DBuffer, pbSrcData, cbSrcLength);
            if (ret != S_OK) {
                DebugLog("Failed to copy RGB32 buffer");
            }
        } else {
            char formatLog[128];
            snprintf(formatLog, sizeof(formatLog), 
                    "Unsupported format: %08X-%04X-%04X",
                    m_subtype.Data1, m_subtype.Data2, m_subtype.Data3);
            DebugLog(formatLog);
            ret = E_NOTIMPL;
        }
        p2DBuffer->Release();
    } else {
        // Fallback: standard buffer copy
        BYTE* pbDstData = nullptr;
        DWORD cbDstMaxLength = 0;
        ret = pDstBuffer->Lock(&pbDstData, &cbDstMaxLength, nullptr);
        if (ret == S_OK) {
            DWORD cbToCopy = min(cbSrcLength, cbDstMaxLength);
            snprintf(bufferLog, sizeof(bufferLog), 
                    "Fallback memcpy: copying %lu bytes (src: %lu, dst: %lu)",
                    cbToCopy, cbSrcLength, cbDstMaxLength);
            DebugLog(bufferLog);
            memcpy(pbDstData, pbSrcData, cbToCopy);
            pDstBuffer->Unlock();
        }
    }

    pSrcBuffer->Unlock();
    pSrcBuffer->Release();
    pDstBuffer->Release();

    // Send to EVR sink for rendering
    if (ret == S_OK) {
        ret = m_pStreamSink->ProcessSample(pD3DSample);
        if (ret != S_OK) {
            DebugLog("ProcessSample failed");
        }
    }
    
    pD3DSample->Release();

    // Request next frame asynchronously
    if (pReader) {
        pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
    }

    return S_OK;
}

// Non usati per ora
STDMETHODIMP VideoReaderCall::OnEvent(DWORD, IMFMediaEvent*) { return S_OK; }
STDMETHODIMP VideoReaderCall::OnFlush(DWORD) { return S_OK; }
