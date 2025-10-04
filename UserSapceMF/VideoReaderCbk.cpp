#include "pch.h"
#include "VideoReaderCbk.h"

// Implementazione template SAFE_RELEASE se non definita altrove
template <class T> void SAFE_RELEASE(T** ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

template <class T> inline void SAFE_RELEASE(T*& pT)
{
    if (pT != NULL)
    {
        pT->Release();
        pT = NULL;
    }
}

VideoReaderCall::VideoReaderCall(IMFStreamSink* pSink) : m_pStreamSink(pSink) {
    if (m_pStreamSink)
        m_pStreamSink->AddRef();
}

VideoReaderCall::~VideoReaderCall() {
    if (m_pStreamSink)
        m_pStreamSink->Release();

    SAFE_RELEASE(pD3DVideoSample);
    SAFE_RELEASE(p2DBuffer);
    SAFE_RELEASE(pDstBuffer);
    SAFE_RELEASE(pD3DManager);
    SAFE_RELEASE(pVideoSampleAllocator);
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
        SAFE_RELEASE(pSrcBuffer);
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
