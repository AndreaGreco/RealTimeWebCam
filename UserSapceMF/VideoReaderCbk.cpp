#include "pch.h"
#include "VideoReaderCbk.h"

VideoReaderCall::VideoReaderCall(IMFStreamSink* pSink) : m_pStreamSink(pSink) {
    if (m_pStreamSink)
        m_pStreamSink->AddRef();
}

VideoReaderCall::~VideoReaderCall() {
    if (m_pStreamSink)
        m_pStreamSink->Release();

    pD3DVideoSample->Release();
    pD3DVideoSample = nullptr;

    p2DBuffer->Release();
    p2DBuffer = nullptr;

    pDstBuffer->Release();
    pDstBuffer = nullptr;

    pD3DManager->Release();
    pD3DManager = nullptr;

    pVideoSampleAllocator->Release();
    pVideoSampleAllocator = nullptr;
}

HRESULT VideoReaderCall::AllocateInternalBuffer(IMFMediaType* SinkMediaType, IMFSourceReader* pReader) {
    HRESULT ret;

    this->pReader = pReader;

    ret = MFGetService(m_pStreamSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pVideoSampleAllocator));
    if (ret != S_OK) {
        return ret;
    }

    ret = MFGetService(m_pStreamSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pD3DManager));
    if (ret != S_OK) {
        return ret;
    }

    ret = pVideoSampleAllocator->SetDirectXManager(pD3DManager);
    if (ret != S_OK) {
        return ret;
    }

    ret = pVideoSampleAllocator->InitializeSampleAllocator(1, SinkMediaType);
    if (ret != S_OK) {
        return ret;
    }

    ret = pVideoSampleAllocator->AllocateSample(&pD3DVideoSample);
    if (ret != S_OK) {
        return ret;
    }

    ret = pD3DVideoSample->GetBufferByIndex(0, &pDstBuffer);
    if (ret != S_OK) {
        return ret;
    }

    ret = pDstBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer));
    if (ret != S_OK) {
        return ret;
    }

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

// Questo è il cuore: viene chiamato quando un frame è pronto
STDMETHODIMP VideoReaderCall::OnReadSample(HRESULT hrStatus,
    DWORD dwStreamIndex,
    DWORD dwStreamFlags,
    LONGLONG llTimestamp,
    IMFSample* pSample)
{
    IMFMediaBuffer* pSrcBuffer = NULL;
    LONGLONG sampleDuration;
    BYTE* pbBuffer = NULL;

    DWORD dwBuffer = 0;
    HRESULT ret;

    if (FAILED(hrStatus))
        return hrStatus;

    if (pSample == NULL)
        return S_OK;

    if (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM)
        return S_OK;

    pSample->GetSampleDuration(&sampleDuration);

    ret = pD3DVideoSample->SetSampleTime(llTimestamp);
    ret = pD3DVideoSample->SetSampleDuration(sampleDuration);

    ret = pSample->ConvertToContiguousBuffer(&pSrcBuffer);
    if (ret == S_OK) {
        ret = pSrcBuffer->Lock(&pbBuffer, NULL, &dwBuffer);
        if (ret == S_OK) {
            ret = p2DBuffer->ContiguousCopyFrom(pbBuffer, dwBuffer);
            pSrcBuffer->Unlock();
        }
		pSrcBuffer->Release();
		pSrcBuffer = NULL;
    }

    ret = m_pStreamSink->ProcessSample(pD3DVideoSample);

    if (pReader) {
        pReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
    }

    return S_OK;
}

// Non usati per ora
STDMETHODIMP VideoReaderCall::OnEvent(DWORD, IMFMediaEvent*) { return S_OK; }
STDMETHODIMP VideoReaderCall::OnFlush(DWORD) { return S_OK; }
