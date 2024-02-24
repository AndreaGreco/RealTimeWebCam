// dllmain.cpp : Definisce il punto di ingresso per l'applicazione DLL.
#include "pch.h"

#include "MFUtils.h"
#include <d3d9.h>
#include <Dxva2api.h>
#include <evr.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfobjects.h>
#include <mfplay.h>
#include <mfreadwrite.h>
#include <stdio.h>
#include <tchar.h>
#include <windows.h>
#include <windowsx.h>
#include <iostream>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Strmiids")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "Dxva2.lib")

#define VIDEO_WIDTH  640
#define VIDEO_HEIGHT 480
#define MEDIA_FILE_PATH L"../MediaFiles/big_buck_bunny.mp4"

// Constants 
const WCHAR CLASS_NAME[] = L"MFVideoEVR Window Class";
const WCHAR WINDOW_NAME[] = L"MFVideoEVR";

class MediaEventHandler : IMFAsyncCallback
{
public:

    HRESULT STDMETHODCALLTYPE Invoke(IMFAsyncResult* pAsyncResult)
    {
        HRESULT hr = S_OK;
        IMFMediaEvent* pEvent = NULL;
        MediaEventType meType = MEUnknown;
        BOOL fGetAnotherEvent = TRUE;
        HRESULT hrStatus = S_OK;
        IMFMediaEventGenerator* pEventGenerator = NULL;

        hr = pAsyncResult->GetState((IUnknown**)&pEventGenerator);
        if (!SUCCEEDED(hr))
        {
            return hr;
        }

        // Get the event from the event queue.
        // Assume that m_pEventGenerator is a valid pointer to the
        // event generator's IMFMediaEventGenerator interface.
        hr = pEventGenerator->EndGetEvent(pAsyncResult, &pEvent);

        // Get the event type.
        if (SUCCEEDED(hr))
        {
            hr = pEvent->GetType(&meType);
        }

        // Get the event status. If the operation that triggered the event 
        // did not succeed, the status is a failure code.
        if (SUCCEEDED(hr))
        {
            hr = pEvent->GetStatus(&hrStatus);
        }

        if (SUCCEEDED(hr))
        {
            // TODO: Handle the event.
        }

        // If not finished, request another event.
        // Pass in a pointer to this instance of the application's
        // CEventHandler class, which implements the callback.
        if (fGetAnotherEvent)
        {
            hr = pEventGenerator->BeginGetEvent(this, pEventGenerator);
        }

        SAFE_RELEASE(pEvent);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE GetParameters(
        DWORD* pdwFlags,
        DWORD* pdwQueue
    )
    {
        pdwFlags = 0;
        pdwQueue = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        /* [in] */ REFIID riid,
        /* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject)
    {
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef(void)
    {
        return 0;
    }

    ULONG STDMETHODCALLTYPE Release(void)
    {
        return 0;
    }
};

class VideoPlayer
{
private:
    // Media Foundation interfaces
    IMFMediaSource* pVideoSource;
    IMFSourceReader* pVideoReader;
    IMFMediaType* videoSourceOutputType;
    IMFMediaType* pvideoSourceModType;
    IMFMediaType* pVideoSourceOutType;
    IMFMediaType* pImfEvrSinkType;
    IMFMediaType* pHintMediaType;
    IMFMediaSink* pVideoSink;
    IMFStreamSink* pStreamSink;
    IMFMediaTypeHandler* pSinkMediaTypeHandler;
    IMFMediaTypeHandler* pSourceMediaTypeHandler;
    IMFPresentationDescriptor* pSourcePresentationDescriptor;
    IMFStreamDescriptor* pSourceStreamDescriptor;
    IMFVideoRenderer* pVideoRenderer;
    IMFVideoDisplayControl* pVideoDisplayControl;
    IMFGetService* pService;
    IMFActivate* pActive;
    IMFPresentationClock* pClock;
    IMFPresentationTimeSource* pTimeSource;
    IDirect3DDeviceManager9* pD3DManager;
    IMFVideoSampleAllocator* pVideoSampleAllocator;
    IMFSample* pD3DVideoSample;
    IMF2DBuffer* p2DBuffer;
    IMFMediaBuffer* pDstBuffer;
    
    // Event handlers
    IMFMediaEventGenerator* pEventGenerator;
    IMFMediaEventGenerator* pstreamSinkEventGenerator;
    MediaEventHandler mediaEvtHandler;
    MediaEventHandler streamSinkMediaEvtHandler;
    
    // Configuration
    LPWSTR filePath;
    HWND windowHandle;
    RECT rc;
    BOOL fSelected;
    bool isPlaying;
    bool isPaused;
    
    // Helper methods
    HRESULT GetVideoSourceFromFile(LPWSTR path, IMFMediaSource** ppVideoSource, IMFSourceReader** ppVideoReader);
    void InitializeVariables();
    void ReleaseResources();

public:
    VideoPlayer();
    ~VideoPlayer();
    
    void setVideoPath(LPWSTR path);
    void setWindowHandle(HWND hwnd);
    int initialize();
    int play();
    int pause();
    int stop();
    bool getIsPlaying() const { return isPlaying; }
    bool getIsPaused() const { return isPaused; }
};

VideoPlayer::VideoPlayer()
{
    InitializeVariables();
    filePath = nullptr;
    windowHandle = nullptr;
    rc = { 0, 0, VIDEO_WIDTH, VIDEO_HEIGHT };
    fSelected = false;
    isPlaying = false;
    isPaused = false;
}

VideoPlayer::~VideoPlayer()
{
    stop();
    ReleaseResources();
}

void VideoPlayer::InitializeVariables()
{
    pVideoSource = NULL;
    pVideoReader = NULL;
    videoSourceOutputType = NULL;
    pvideoSourceModType = NULL;
    pVideoSourceOutType = NULL;
    pImfEvrSinkType = NULL;
    pHintMediaType = NULL;
    pVideoSink = NULL;
    pStreamSink = NULL;
    pSinkMediaTypeHandler = NULL;
    pSourceMediaTypeHandler = NULL;
    pSourcePresentationDescriptor = NULL;
    pSourceStreamDescriptor = NULL;
    pVideoRenderer = NULL;
    pVideoDisplayControl = NULL;
    pService = NULL;
    pActive = NULL;
    pClock = NULL;
    pTimeSource = NULL;
    pD3DManager = NULL;
    pVideoSampleAllocator = NULL;
    pD3DVideoSample = NULL;
    p2DBuffer = NULL;
    pDstBuffer = NULL;
    pEventGenerator = NULL;
    pstreamSinkEventGenerator = NULL;
}

void VideoPlayer::ReleaseResources()
{
    SAFE_RELEASE(p2DBuffer);
    SAFE_RELEASE(pDstBuffer);
    SAFE_RELEASE(pVideoReader);
    SAFE_RELEASE(videoSourceOutputType);
    SAFE_RELEASE(pvideoSourceModType);
    SAFE_RELEASE(pImfEvrSinkType);
    SAFE_RELEASE(pHintMediaType);
    SAFE_RELEASE(pVideoSink);
    SAFE_RELEASE(pStreamSink);
    SAFE_RELEASE(pSinkMediaTypeHandler);
    SAFE_RELEASE(pSourceMediaTypeHandler);
    SAFE_RELEASE(pSourcePresentationDescriptor);
    SAFE_RELEASE(pSourceStreamDescriptor);
    SAFE_RELEASE(pVideoRenderer);
    SAFE_RELEASE(pVideoDisplayControl);
    SAFE_RELEASE(pService);
    SAFE_RELEASE(pActive);
    SAFE_RELEASE(pClock);
    SAFE_RELEASE(pTimeSource);
    SAFE_RELEASE(pD3DManager);
    SAFE_RELEASE(pVideoSampleAllocator);
    SAFE_RELEASE(pD3DVideoSample);
    SAFE_RELEASE(pEventGenerator);
    SAFE_RELEASE(pstreamSinkEventGenerator);
    SAFE_RELEASE(pVideoSource);
}

void VideoPlayer::setVideoPath(LPWSTR path)
{
    filePath = path;
}

void VideoPlayer::setWindowHandle(HWND hwnd)
{
    windowHandle = hwnd;
}

/**
* Gets a video source reader from a media file.
* @param[in] path: the media file path to get the source reader for.
* @param[out] ppVideoSource: will be set with the source for the reader if successful.
* @param[out] ppVideoReader: will be set with the reader if successful.
* @@Returns S_OK if successful or an error code if not.
*/
HRESULT VideoPlayer::GetVideoSourceFromFile(LPWSTR path, IMFMediaSource** ppVideoSource, IMFSourceReader** ppVideoReader)
{
    IMFSourceResolver* pSourceResolver = NULL;
    IUnknown* uSource = NULL;

    IMFAttributes* pVideoReaderAttributes = NULL;
    MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;

    HRESULT hr = S_OK;

    hr = MFCreateSourceResolver(&pSourceResolver);
    if (hr != S_OK) {
        goto done;
    }

    hr = pSourceResolver->CreateObjectFromURL(
        path,                       // URL of the source.
        MF_RESOLUTION_MEDIASOURCE,  // Create a source object.
        NULL,                       // Optional property store.
        &ObjectType,                // Receives the created object type. 
        &uSource                    // Receives a pointer to the media source. 
    );
    if (hr != S_OK) {
        goto done;
    }

    hr = uSource->QueryInterface(IID_PPV_ARGS(ppVideoSource));
    if (hr != S_OK) {
        goto done;
    }

    hr = MFCreateAttributes(&pVideoReaderAttributes, 1);
    if (hr != S_OK) {
        goto done;
    }

    hr = pVideoReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, 1);
    if (hr != S_OK) {
        goto done;
    }

    hr = MFCreateSourceReaderFromMediaSource(*ppVideoSource, pVideoReaderAttributes, ppVideoReader);
    if (hr != S_OK) {
        goto done;
    }

done:

    SAFE_RELEASE(pSourceResolver);
    SAFE_RELEASE(uSource);
    SAFE_RELEASE(pVideoReaderAttributes);

    return hr;
}

int VideoPlayer::initialize()
{
    HRESULT ret;
    bool check;

    if (windowHandle == nullptr) {
        return -1;
    }

    check = IsWindow(windowHandle);
    if (!check) {
        return -1;
	}

    if (filePath == nullptr) {
        return -1;
    }

    ret = MFStartup(MF_VERSION);
	if (ret != S_OK) {
        return -1;
    }

    // ----- Set up Video sink (Enhanced Video Renderer). -----
    ret = MFCreateVideoRendererActivate(windowHandle, &pActive);
    if (ret != S_OK) {
        return -2;
    }
    
    ret = pActive->ActivateObject(IID_IMFMediaSink, (void**)&pVideoSink);
    if (ret != S_OK) {
        return -3;
    }

    // Initialize the renderer before doing anything else including querying for other interfaces,
    // see https://msdn.microsoft.com/en-us/library/windows/desktop/ms704667(v=vs.85).aspx.
    ret = pVideoSink->QueryInterface(__uuidof(IMFVideoRenderer), (void**)&pVideoRenderer);
    if (ret != S_OK) {
        return -4;
    }
    
    ret = pVideoRenderer->InitializeRenderer(NULL, NULL);
    if (ret != S_OK) {
        return -5;
    }
    
    ret = pVideoSink->QueryInterface(__uuidof(IMFGetService), (void**)&pService);
    if (ret != S_OK) {
        return -6;
    }

    ret = pService->GetService(MR_VIDEO_RENDER_SERVICE, __uuidof(IMFVideoDisplayControl), (void**)&pVideoDisplayControl);
    if (ret != S_OK) {
        return -7;
    }

    ret = pVideoDisplayControl->SetVideoWindow(windowHandle);
    if (ret != S_OK) {
        return -8;
    }

    ret = pVideoDisplayControl->SetVideoPosition(NULL, &rc);
    if (ret != S_OK) {
        return -9;
    }

    ret = pVideoSink->GetStreamSinkByIndex(0, &pStreamSink);
    if (ret != S_OK) {
        return -10;
    }

    ret = pStreamSink->GetMediaTypeHandler(&pSinkMediaTypeHandler);
    if (ret != S_OK) {
        return -11;
    }

    DWORD sinkMediaTypeCount = 0;
    ret = pSinkMediaTypeHandler->GetMediaTypeCount(&sinkMediaTypeCount);
    if (ret != S_OK) {
        return -12;
    }

    // ----- Set up Video source. -----
    ret = GetVideoSourceFromFile(filePath, &pVideoSource, &pVideoReader);
    if (ret != S_OK) {
        return -13;
    }

    ret = pVideoReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, false);
    if (ret != S_OK) {
        return -14;
    }

    ret = pVideoReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &videoSourceOutputType);
    if (ret != S_OK) {
        return -15;
    }

    ret = pVideoReader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    if (ret != S_OK) {
        return -16;
    }

    ret = pVideoSource->CreatePresentationDescriptor(&pSourcePresentationDescriptor);
    if (ret != S_OK) {
        return -17;
    }

    ret = pSourcePresentationDescriptor->GetStreamDescriptorByIndex(0, &fSelected, &pSourceStreamDescriptor);
    if (ret != S_OK) {
        return -18;
    }

    ret = pSourceStreamDescriptor->GetMediaTypeHandler(&pSourceMediaTypeHandler);
    if (ret != S_OK) {
        return -19;
    }

    DWORD srcMediaTypeCount = 0;
    ret = pSourceMediaTypeHandler->GetMediaTypeCount(&srcMediaTypeCount);
    if (ret != S_OK) {
        return -20;
    }

    // ----- Create a compatible media type and set on the source and sink. -----

    // Set the video output type on the file source.
    ret = MFCreateMediaType(&pVideoSourceOutType);
    if (ret != S_OK) {
        return -21;
    }

    ret = pVideoSourceOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (ret != S_OK) {
        return -22;
    }

    ret = pVideoSourceOutType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (ret != S_OK) {
        return -23;
    }

    ret = pVideoSourceOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (ret != S_OK) {
        return -24;
    }

    ret = pVideoSourceOutType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (ret != S_OK) {
        return -25;
    }

    ret = MFSetAttributeRatio(pVideoSourceOutType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (ret != S_OK) {
        return -26;
    }

    ret = CopyAttribute(videoSourceOutputType, pVideoSourceOutType, MF_MT_FRAME_SIZE);
    if (ret != S_OK) {
        return -27;
    }

    ret = CopyAttribute(videoSourceOutputType, pVideoSourceOutType, MF_MT_FRAME_RATE);
    if (ret != S_OK) {
        return -28;
    }

    // Set the video input type on the EVR sink.
    ret = MFCreateMediaType(&pImfEvrSinkType);
    if (ret != S_OK) {
        return -29;
    }

    ret = pImfEvrSinkType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (ret != S_OK) {
        return -30;
    }

    ret = pImfEvrSinkType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (ret != S_OK) {
        return -31;
    }

    ret = pImfEvrSinkType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (ret != S_OK) {
        return -32;
    }

    ret = pImfEvrSinkType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (ret != S_OK) {
        return -33;
    }

    ret = MFSetAttributeRatio(pImfEvrSinkType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (ret != S_OK) {
        return -34;
    }

    ret = CopyAttribute(videoSourceOutputType, pImfEvrSinkType, MF_MT_FRAME_SIZE);
    if (ret != S_OK) {
        return -35;
    }

    ret = CopyAttribute(videoSourceOutputType, pImfEvrSinkType, MF_MT_FRAME_RATE);
    if (ret != S_OK) {
        return -36;
    }

    ret = pSinkMediaTypeHandler->SetCurrentMediaType(pImfEvrSinkType);
    if (ret != S_OK) {
        return -37;
    }

    ret = pVideoReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pVideoSourceOutType);
    if (ret != S_OK) {
        return -38;
    }

    // ----- Set up event handler for sink events otherwise memory leaks. -----

    ret = pVideoSink->QueryInterface(IID_IMFMediaEventGenerator, (void**)&pEventGenerator);
    if (ret != S_OK) {
        return -39;
    }

    ret = pEventGenerator->BeginGetEvent((IMFAsyncCallback*)&mediaEvtHandler, pEventGenerator);
    if (ret != S_OK) {
        return -40;
    }

    ret = pStreamSink->QueryInterface(IID_IMFMediaEventGenerator, (void**)&pstreamSinkEventGenerator);
    if (ret != S_OK) {
        return -41;
    }

    ret = pstreamSinkEventGenerator->BeginGetEvent((IMFAsyncCallback*)&streamSinkMediaEvtHandler, pstreamSinkEventGenerator);
    if (ret != S_OK) {
        return -42;
    }

    // ----- Source and sink now configured. Set up remaining infrastructure and then start sampling. -----

    // Get Direct3D surface organised.
    ret = MFGetService(pStreamSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pVideoSampleAllocator));
    if (ret != S_OK) {
        return -43;
    }

    ret = MFGetService(pVideoSink, MR_VIDEO_ACCELERATION_SERVICE, IID_PPV_ARGS(&pD3DManager));
    if (ret != S_OK) {
        return -44;
    }

    ret = pVideoSampleAllocator->SetDirectXManager(pD3DManager);
    if (ret != S_OK) {
        return -45;
    }

    ret = pVideoSampleAllocator->InitializeSampleAllocator(1, pImfEvrSinkType);
    if (ret != S_OK) {
        return -46;
    }

    ret = pVideoSampleAllocator->AllocateSample(&pD3DVideoSample);
    if (ret != S_OK) {
        return -47;
    }

    ret = pD3DVideoSample->GetBufferByIndex(0, &pDstBuffer);
    if (ret != S_OK) {
        return -48;
    }

    ret = pDstBuffer->QueryInterface(IID_PPV_ARGS(&p2DBuffer));
    if (ret != S_OK) {
        return -49;
    }

    // Get clocks organised.
    ret = MFCreatePresentationClock(&pClock);
    if (ret != S_OK) {
        return -50;
    }

    ret = MFCreateSystemTimeSource(&pTimeSource);
    if (ret != S_OK) {
        return -51;
    }

    ret = pClock->SetTimeSource(pTimeSource);
    if (ret != S_OK) {
        return -52;
    }

    ret = pVideoSink->SetPresentationClock(pClock);
    if (ret != S_OK) {
        return -53;
    }

    return 0;
}

int VideoPlayer::play()
{
    if (pClock == nullptr) {
        return -1;
    }

    HRESULT ret = pClock->Start(0);
    if (ret != S_OK) {
        return -1;
    }

    isPlaying = true;
    isPaused = false;

    // Start the sample read-write loop in a separate thread for non-blocking operation
    // For now, simplified version without threading
    IMFSample* videoSample = NULL;
    IMFMediaBuffer* pSrcBuffer = NULL;
    BYTE* pbBuffer = NULL;
    DWORD streamIndex, flags;
    LONGLONG llTimeStamp;
    UINT32 uiAttribute = 0;
    DWORD dwBuffer = 0;

    for (int frameCount = 0; frameCount < 100 && isPlaying; frameCount++) // Limit frames for demo
    {
        ret = pVideoReader->ReadSample(
            MF_SOURCE_READER_FIRST_VIDEO_STREAM,
            0,                              // Flags.
            &streamIndex,                   // Receives the actual stream index. 
            &flags,                         // Receives status flags.
            &llTimeStamp,                   // Receives the time stamp.
            &videoSample                    // Receives the sample or NULL.
        );
        if (ret != S_OK) {
            break;
        }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            break;
        }

        if (videoSample)
        {
            LONGLONG sampleDuration = 0;
            ret = videoSample->GetSampleDuration(&sampleDuration);
            
            ret = pD3DVideoSample->SetSampleTime(llTimeStamp);
            ret = pD3DVideoSample->SetSampleDuration(sampleDuration);

            ret = videoSample->ConvertToContiguousBuffer(&pSrcBuffer);
            if (ret == S_OK) {
                ret = pSrcBuffer->Lock(&pbBuffer, NULL, &dwBuffer);
                if (ret == S_OK) {
                    ret = p2DBuffer->ContiguousCopyFrom(pbBuffer, dwBuffer);
                    pSrcBuffer->Unlock();
                }
            }

            ret = pStreamSink->ProcessSample(pD3DVideoSample);
            Sleep(sampleDuration / 10000); // Duration is given in 100's of nano seconds.
        }

        SAFE_RELEASE(pSrcBuffer);
        SAFE_RELEASE(videoSample);
    }

    isPlaying = false;
    return 0;
}

int VideoPlayer::pause()
{
    if (pClock == nullptr) {
        return -1;
    }

    HRESULT ret = pClock->Pause();
    if (ret != S_OK) {
        return -1;
    }

    isPaused = true;
    isPlaying = false;
    return 0;
}

int VideoPlayer::stop()
{
    if (pClock != nullptr) {
        pClock->Stop();
    }
    
    isPlaying = false;
    isPaused = false;
    return 0;
}

// C-style interface for C# interop
extern "C" {
    __declspec(dllexport) VideoPlayer* CreateVideoPlayer()
    {
        return new VideoPlayer();
    }

    __declspec(dllexport) void DestroyVideoPlayer(VideoPlayer* player)
    {
        if (player) {
            delete player;
        }
    }

    __declspec(dllexport) void SetVideoPath(VideoPlayer* player, LPWSTR path)
    {
        if (player) {
            player->setVideoPath(path);
        }
    }

    __declspec(dllexport) void SetWindowHandle(VideoPlayer* player, HWND hwnd)
    {
        if (player) {
            player->setWindowHandle(hwnd);
        }
    }

    __declspec(dllexport) int InitializePlayer(VideoPlayer* player)
    {
        if (player) {
            return player->initialize();
        }
        return -1;
    }

    __declspec(dllexport) int PlayVideo(VideoPlayer* player)
    {
        if (player) {
            return player->play();
        }
        return -1;
    }

    __declspec(dllexport) int PauseVideo(VideoPlayer* player)
    {
        if (player) {
            return player->pause();
        }
        return -1;
    }

    __declspec(dllexport) int StopVideo(VideoPlayer* player)
    {
        if (player) {
            return player->stop();
        }
        return -1;
    }

    __declspec(dllexport) bool IsPlaying(VideoPlayer* player)
    {
        if (player) {
            return player->getIsPlaying();
        }
        return false;
    }

    __declspec(dllexport) bool IsPaused(VideoPlayer* player)
    {
        if (player) {
            return player->getIsPaused();
        }
        return false;
    }
}

BOOL APIENTRY UserSpaceMF( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    
    return TRUE;
}
