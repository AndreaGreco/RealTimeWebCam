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

    // Lock destination buffer using 2D interface for NV12
    IMF2DBuffer* p2DBuffer = nullptr;
    ret = pDstBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer));
    if (ret == S_OK) {
        // Use 2D buffer for efficient copy
        BYTE* pbScanline0 = nullptr;
        LONG lPitch = 0;
        ret = p2DBuffer->Lock2D(&pbScanline0, &lPitch);
        if (ret == S_OK) {
            // For NV12: Y plane + UV plane
            DWORD cbYPlane = m_width * m_height;
            
            // Copy Y plane
            MFCopyImage(pbScanline0, lPitch, pbSrcData, m_width, m_width, m_height);
            
            // Copy UV plane
            MFCopyImage(pbScanline0 + (lPitch * m_height), lPitch,
                       pbSrcData + cbYPlane, m_width, m_width, m_height / 2);
            
            p2DBuffer->Unlock2D();
        }
        p2DBuffer->Release();
    } else {
        // Fallback: standard buffer copy
        BYTE* pbDstData = nullptr;
        DWORD cbDstMaxLength = 0;
        ret = pDstBuffer->Lock(&pbDstData, &cbDstMaxLength, nullptr);
        if (ret == S_OK) {
            DWORD cbToCopy = min(cbSrcLength, cbDstMaxLength);
            memcpy(pbDstData, pbSrcData, cbToCopy);
            pDstBuffer->Unlock();
        }
    }

    pSrcBuffer->Unlock();
    pSrcBuffer->Release();
    pDstBuffer->Release();

    // Send to EVR sink for rendering
    ret = m_pStreamSink->ProcessSample(pD3DSample);
    pD3DSample->Release();

    if (ret != S_OK) {
        DebugLog("ProcessSample failed");
    }

    // Request next frame asynchronously
    if (pReader) {
        pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
    }

    return S_OK;
}

// Non usati per ora
STDMETHODIMP VideoReaderCall::OnEvent(DWORD, IMFMediaEvent*) { return S_OK; }
STDMETHODIMP VideoReaderCall::OnFlush(DWORD) { return S_OK; }
