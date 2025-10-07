// dllmain.cpp : Definisce il punto di ingresso per l'applicazione DLL.
#include "pch.h"

#include "Logger.h"
#include "MediaEventHandler.h"
#include "VideoReaderCbk.h"
#include "CSourceOpenMonitor.h"
#include <combaseapi.h>
#include <evr.h>
#include <guiddef.h>
#include <ios>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <PropIdl.h>
#include <sstream>
#include <Unknwnbase.h>
#include <windows.h>

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
#pragma comment(lib, "Propsys.lib")

#define VIDEO_WIDTH  640
#define VIDEO_HEIGHT 480

// Constants 
const WCHAR CLASS_NAME[] = L"MFVideoEVR Window Class";
const WCHAR WINDOW_NAME[] = L"MFVideoEVR";

// ============================================================================
// HELPER FUNCTIONS - Template utilities
// ============================================================================

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

// ============================================================================
// HELPER FUNCTIONS - Logging
// ============================================================================

void LogError(const char* function, HRESULT hr) 
{
	std::ostringstream oss;
	oss << function << " failed with HRESULT: 0x" << std::hex << hr;
	DebugLog(oss.str().c_str());
}

void LogErrorCode(const char* function, int errorCode) 
{
	std::ostringstream oss;
	oss << function << " failed with error code: " << errorCode;
	DebugLog(oss.str().c_str());
}

/**
* Copies a media type attribute from source to destination.
*/
HRESULT CopyAttribute(IMFAttributes* pSrc, IMFAttributes* pDest, const GUID& key)
{
	PROPVARIANT var;
	PropVariantInit(&var);

	HRESULT hr = pSrc->GetItem(key, &var);
	if (SUCCEEDED(hr))
	{
		hr = pDest->SetItem(key, var);
	}

	PropVariantClear(&var);
	return hr;
}

/**
* Creates a property store with source open monitor.
*/
HRESULT CreatePropertyStoreWithMonitor(CSourceOpenMonitor* pMonitor, IPropertyStore** ppConfig)
{
	HRESULT hr = PSCreateMemoryPropertyStore(IID_PPV_ARGS(ppConfig));
	if (FAILED(hr))
	{
		LogError("PSCreateMemoryPropertyStore", hr);
		return hr;
	}

	PROPVARIANT var;
	var.vt = VT_UNKNOWN;
	pMonitor->QueryInterface(IID_PPV_ARGS(&var.punkVal));

	hr = (*ppConfig)->SetValue(MFPKEY_SourceOpenMonitor, var);
	PropVariantClear(&var);

	return hr;
}

/**
* Configures media source for low-latency RTSP streaming.
*/
void ConfigureSourceForLowLatency(IMFMediaSource* pSource)
{
	IMFAttributes* pSourceConfig = NULL;
	HRESULT hr = pSource->QueryInterface(IID_PPV_ARGS(&pSourceConfig));
	
	if (SUCCEEDED(hr))
	{
		// Set max buffer time for network source (in milliseconds) - 0 for minimal latency
		pSourceConfig->SetUINT32(MFNETSOURCE_MAXBUFFERTIMEMS, 0);
		
		// Enable RTSP protocol support
		pSourceConfig->SetUINT32(MFNETSOURCE_ENABLE_RTSP, TRUE);
		
		// Minimal buffering for real-time (in milliseconds)
		pSourceConfig->SetUINT32(MFNETSOURCE_BUFFERINGTIME, 0);
		
		DebugLog("Network source configured with minimal buffering (0ms)");
		SAFE_RELEASE(pSourceConfig);
	}
}

/**
* Creates source reader with async callback and low latency settings.
*/
HRESULT CreateSourceReaderWithCallback(IMFMediaSource* pSource, VideoReaderCall* pCallback, IMFSourceReader** ppReader)
{
	IMFAttributes* pVideoReaderAttributes = NULL;
	
	HRESULT hr = MFCreateAttributes(&pVideoReaderAttributes, 2);
	if (FAILED(hr))
	{
		LogError("MFCreateAttributes", hr);
		return hr;
	}

	hr = pVideoReaderAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, pCallback);
	if (FAILED(hr))
	{
		LogError("SetUnknown MF_SOURCE_READER_ASYNC_CALLBACK", hr);
		SAFE_RELEASE(pVideoReaderAttributes);
		return hr;
	}

	hr = pVideoReaderAttributes->SetUINT32(MF_LOW_LATENCY, 1);
	if (FAILED(hr))
	{
		LogError("SetUINT32 MF_LOW_LATENCY", hr);
		SAFE_RELEASE(pVideoReaderAttributes);
		return hr;
	}

	hr = MFCreateSourceReaderFromMediaSource(pSource, pVideoReaderAttributes, ppReader);
	if (FAILED(hr))
	{
		LogError("MFCreateSourceReaderFromMediaSource", hr);
	}

	SAFE_RELEASE(pVideoReaderAttributes);
	return hr;
}

/**
* Creates media source from URL with source open monitor.
*/
HRESULT CreateMediaSourceFromURL(LPWSTR path, CSourceOpenMonitor* pMonitor, IMFMediaSource** ppSource)
{
	IMFSourceResolver* pSourceResolver = NULL;
	IUnknown* uSource = NULL;
	IPropertyStore* pConfig = NULL;
	MF_OBJECT_TYPE ObjectType = MF_OBJECT_INVALID;

	HRESULT hr = MFCreateSourceResolver(&pSourceResolver);
	if (FAILED(hr))
	{
		LogError("MFCreateSourceResolver", hr);
		return hr;
	}

	hr = CreatePropertyStoreWithMonitor(pMonitor, &pConfig);
	if (FAILED(hr))
	{
		SAFE_RELEASE(pSourceResolver);
		return hr;
	}

	hr = pSourceResolver->CreateObjectFromURL(
		path,
		MF_RESOLUTION_MEDIASOURCE,
		pConfig,
		&ObjectType,
		&uSource
	);
	
	if (FAILED(hr))
	{
		LogError("CreateObjectFromURL", hr);
		goto done;
	}

	hr = uSource->QueryInterface(IID_PPV_ARGS(ppSource));
	if (FAILED(hr))
	{
		LogError("QueryInterface for IMFMediaSource", hr);
	}

done:
	SAFE_RELEASE(pSourceResolver);
	SAFE_RELEASE(uSource);
	SAFE_RELEASE(pConfig);
	return hr;
}

/**
* Creates NV12 media type for video output (optimal for Direct3D/GPU).
*/
HRESULT CreateNV12VideoOutputType(IMFMediaType* pSourceType, IMFMediaType** ppOutputType)
{
	HRESULT hr = MFCreateMediaType(ppOutputType);
	if (FAILED(hr))
	{
		LogError("MFCreateMediaType for source output", hr);
		return hr;
	}

	hr = (*ppOutputType)->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	if (FAILED(hr))
	{
		LogError("SetGUID MF_MT_MAJOR_TYPE", hr);
		return hr;
	}

	// Use NV12 instead of RGB32 for Direct3D/EVR - GPU will handle conversion
	hr = (*ppOutputType)->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
	if (FAILED(hr))
	{
		LogError("SetGUID MF_MT_SUBTYPE to NV12", hr);
		return hr;
	}

	hr = (*ppOutputType)->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
	if (FAILED(hr))
	{
		LogError("SetUINT32 MF_MT_INTERLACE_MODE", hr);
		return hr;
	}

	hr = (*ppOutputType)->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
	if (FAILED(hr))
	{
		LogError("SetUINT32 MF_MT_ALL_SAMPLES_INDEPENDENT", hr);
		return hr;
	}

	hr = MFSetAttributeRatio(*ppOutputType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
	if (FAILED(hr))
	{
		LogError("MFSetAttributeRatio for pixel aspect ratio", hr);
		return hr;
	}

	hr = CopyAttribute(pSourceType, *ppOutputType, MF_MT_FRAME_SIZE);
	if (FAILED(hr))
	{
		LogError("CopyAttribute MF_MT_FRAME_SIZE", hr);
		return hr;
	}

	hr = CopyAttribute(pSourceType, *ppOutputType, MF_MT_FRAME_RATE);
	if (FAILED(hr))
	{
		LogError("CopyAttribute MF_MT_FRAME_RATE", hr);
		return hr;
	}

	return S_OK;
}

// ============================================================================
// VideoPlayer Class
// ============================================================================

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
	IMFSample* pD3DVideoSample;
	VideoReaderCall* videoReaderCallback;
	CSourceOpenMonitor* pSourceOpenMonitor;

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

	// Initialization helpers
	void InitializeVariables();
	void ReleaseResources();
	HRESULT ValidateInitializationParameters();
	
	// Video sink setup
	HRESULT CreateAndInitializeVideoSink();
	HRESULT SetupVideoDisplayControl();
	HRESULT GetStreamSinkAndMediaTypeHandler();
	
	// Video source setup
	HRESULT CreateVideoSourceAndReader();
	HRESULT ConfigureVideoSourceStream();
	HRESULT GetSourceDescriptors();
	
	// Media type configuration
	HRESULT CreateAndSetMediaTypes();
	
	// Event and clock setup
	HRESULT SetupEventHandlers();
	HRESULT SetupPresentationClock();

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

// ============================================================================
// VideoPlayer Implementation - Constructor/Destructor
// ============================================================================

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
	pD3DVideoSample = NULL;
	pEventGenerator = NULL;
	pstreamSinkEventGenerator = NULL;
	pSourceOpenMonitor = NULL;
	videoReaderCallback = NULL;
}

void VideoPlayer::ReleaseResources()
{
	SAFE_RELEASE(pVideoReader);
	SAFE_RELEASE(videoSourceOutputType);
	SAFE_RELEASE(pvideoSourceModType);
	SAFE_RELEASE(pVideoSourceOutType);
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
	SAFE_RELEASE(pD3DVideoSample);
	SAFE_RELEASE(pEventGenerator);
	SAFE_RELEASE(pstreamSinkEventGenerator);
	SAFE_RELEASE(pSourceOpenMonitor);
	SAFE_RELEASE(pVideoSource);
	
	if (videoReaderCallback)
	{
		delete videoReaderCallback;
		videoReaderCallback = NULL;
	}
}

// ============================================================================
// VideoPlayer Implementation - Setters
// ============================================================================

void VideoPlayer::setVideoPath(LPWSTR path)
{
	filePath = path;
}

void VideoPlayer::setWindowHandle(HWND hwnd)
{
	windowHandle = hwnd;
}

// ============================================================================
// VideoPlayer Implementation - Initialization Helpers
// ============================================================================

HRESULT VideoPlayer::ValidateInitializationParameters()
{
	if (windowHandle == nullptr)
	{
		DebugLog("VideoPlayer::initialize() - windowHandle is null");
		return E_INVALIDARG;
	}

	if (!IsWindow(windowHandle))
	{
		DebugLog("VideoPlayer::initialize() - windowHandle is not a valid window");
		return E_INVALIDARG;
	}

	if (filePath == nullptr)
	{
		DebugLog("VideoPlayer::initialize() - filePath is null");
		return E_INVALIDARG;
	}

	return S_OK;
}

HRESULT VideoPlayer::CreateAndInitializeVideoSink()
{
	HRESULT hr = MFCreateVideoRendererActivate(windowHandle, &pActive);
	if (FAILED(hr))
	{
		LogError("MFCreateVideoRendererActivate", hr);
		return hr;
	}

	hr = pActive->ActivateObject(IID_IMFMediaSink, (void**)&pVideoSink);
	if (FAILED(hr))
	{
		LogError("ActivateObject for IMFMediaSink", hr);
		return hr;
	}

	// Initialize the renderer before doing anything else
	hr = pVideoSink->QueryInterface(__uuidof(IMFVideoRenderer), (void**)&pVideoRenderer);
	if (FAILED(hr))
	{
		LogError("QueryInterface for IMFVideoRenderer", hr);
		return hr;
	}

	hr = pVideoRenderer->InitializeRenderer(NULL, NULL);
	if (FAILED(hr))
	{
		LogError("InitializeRenderer", hr);
		return hr;
	}

	return S_OK;
}

HRESULT VideoPlayer::SetupVideoDisplayControl()
{
	HRESULT hr = pVideoSink->QueryInterface(__uuidof(IMFGetService), (void**)&pService);
	if (FAILED(hr))
	{
		LogError("QueryInterface for IMFGetService", hr);
		return hr;
	}

	hr = pService->GetService(MR_VIDEO_RENDER_SERVICE, __uuidof(IMFVideoDisplayControl), (void**)&pVideoDisplayControl);
	if (FAILED(hr))
	{
		LogError("GetService for IMFVideoDisplayControl", hr);
		return hr;
	}

	hr = pVideoDisplayControl->SetVideoWindow(windowHandle);
	if (FAILED(hr))
	{
		LogError("SetVideoWindow", hr);
		return hr;
	}

	hr = pVideoDisplayControl->SetVideoPosition(NULL, &rc);
	if (FAILED(hr))
	{
		LogError("SetVideoPosition", hr);
		return hr;
	}

	return S_OK;
}

HRESULT VideoPlayer::GetStreamSinkAndMediaTypeHandler()
{
	HRESULT hr = pVideoSink->GetStreamSinkByIndex(0, &pStreamSink);
	if (FAILED(hr))
	{
		LogError("GetStreamSinkByIndex", hr);
		return hr;
	}

	hr = pStreamSink->GetMediaTypeHandler(&pSinkMediaTypeHandler);
	if (FAILED(hr))
	{
		LogError("GetMediaTypeHandler for sink", hr);
		return hr;
	}

	DWORD sinkMediaTypeCount = 0;
	hr = pSinkMediaTypeHandler->GetMediaTypeCount(&sinkMediaTypeCount);
	if (FAILED(hr))
	{
		LogError("GetMediaTypeCount for sink", hr);
		return hr;
	}

	return S_OK;
}

HRESULT VideoPlayer::CreateVideoSourceAndReader()
{
	// Create the source open monitor
	pSourceOpenMonitor = new (std::nothrow) CSourceOpenMonitor();
	if (pSourceOpenMonitor == NULL)
	{
		LogError("Failed to create CSourceOpenMonitor", E_OUTOFMEMORY);
		return E_OUTOFMEMORY;
	}

	// Create media source from URL
	HRESULT hr = CreateMediaSourceFromURL(filePath, pSourceOpenMonitor, &pVideoSource);
	if (FAILED(hr))
	{
		LogError("CreateMediaSourceFromURL", hr);
		return hr;
	}

	// Configure for low latency
	ConfigureSourceForLowLatency(pVideoSource);

	// Create video reader callback
	videoReaderCallback = new (std::nothrow) VideoReaderCall(pStreamSink);
	if (!videoReaderCallback)
	{
		LogError("Failed to create VideoReaderCall", E_OUTOFMEMORY);
		return E_OUTOFMEMORY;
	}

	// Create source reader with callback
	hr = CreateSourceReaderWithCallback(pVideoSource, videoReaderCallback, &pVideoReader);
	if (FAILED(hr))
	{
		LogError("CreateSourceReaderWithCallback", hr);
		return hr;
	}

	DebugLog("Video source and reader created successfully (optimized for RTSP low-latency)");
	return S_OK;
}

HRESULT VideoPlayer::ConfigureVideoSourceStream()
{
	HRESULT hr = pVideoReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, false);
	if (FAILED(hr))
	{
		LogError("SetStreamSelection for all streams", hr);
		return hr;
	}

	hr = pVideoReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &videoSourceOutputType);
	if (FAILED(hr))
	{
		LogError("GetCurrentMediaType", hr);
		return hr;
	}

	hr = pVideoReader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
	if (FAILED(hr))
	{
		LogError("SetStreamSelection for first video stream", hr);
		return hr;
	}

	return S_OK;
}

HRESULT VideoPlayer::GetSourceDescriptors()
{
	HRESULT hr = pVideoSource->CreatePresentationDescriptor(&pSourcePresentationDescriptor);
	if (FAILED(hr))
	{
		LogError("CreatePresentationDescriptor", hr);
		return hr;
	}

	hr = pSourcePresentationDescriptor->GetStreamDescriptorByIndex(0, &fSelected, &pSourceStreamDescriptor);
	if (FAILED(hr))
	{
		LogError("GetStreamDescriptorByIndex", hr);
		return hr;
	}

	hr = pSourceStreamDescriptor->GetMediaTypeHandler(&pSourceMediaTypeHandler);
	if (FAILED(hr))
	{
		LogError("GetMediaTypeHandler for source", hr);
		return hr;
	}

	DWORD srcMediaTypeCount = 0;
	hr = pSourceMediaTypeHandler->GetMediaTypeCount(&srcMediaTypeCount);
	if (FAILED(hr))
	{
		LogError("GetMediaTypeCount for source", hr);
		return hr;
	}

	return S_OK;
}

HRESULT VideoPlayer::CreateAndSetMediaTypes()
{
	// Create NV12 video output type
	HRESULT hr = CreateNV12VideoOutputType(videoSourceOutputType, &pVideoSourceOutType);
	if (FAILED(hr))
	{
		return hr;
	}

	// Create EVR sink type by copying source output type
	hr = MFCreateMediaType(&pImfEvrSinkType);
	if (FAILED(hr))
	{
		LogError("MFCreateMediaType for EVR sink", hr);
		return hr;
	}

	hr = pVideoSourceOutType->CopyAllItems(pImfEvrSinkType);
	if (FAILED(hr))
	{
		LogError("CopyAllItems from source to sink", hr);
		return hr;
	}

	// Set the media type on the sink first
	hr = pSinkMediaTypeHandler->SetCurrentMediaType(pImfEvrSinkType);
	if (FAILED(hr))
	{
		LogError("SetCurrentMediaType for sink", hr);
		return hr;
	}

	// Allocate internal buffer with validated media type
	hr = videoReaderCallback->AllocateInternalBuffer(pImfEvrSinkType, pVideoReader);
	if (FAILED(hr))
	{
		LogError("AllocateInternalBuffer", hr);
		return hr;
	}

	// Set the media type on the source reader
	hr = pVideoReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pVideoSourceOutType);
	if (FAILED(hr))
	{
		LogError("SetCurrentMediaType for video reader", hr);
		return hr;
	}

	return S_OK;
}

HRESULT VideoPlayer::SetupEventHandlers()
{
	HRESULT hr = pVideoSink->QueryInterface(IID_IMFMediaEventGenerator, (void**)&pEventGenerator);
	if (FAILED(hr))
	{
		LogError("QueryInterface for IMFMediaEventGenerator (sink)", hr);
		return hr;
	}

	hr = pEventGenerator->BeginGetEvent((IMFAsyncCallback*)&mediaEvtHandler, pEventGenerator);
	if (FAILED(hr))
	{
		LogError("BeginGetEvent for event generator", hr);
		return hr;
	}

	hr = pStreamSink->QueryInterface(IID_IMFMediaEventGenerator, (void**)&pstreamSinkEventGenerator);
	if (FAILED(hr))
	{
		LogError("QueryInterface for IMFMediaEventGenerator (stream sink)", hr);
		return hr;
	}

	hr = pstreamSinkEventGenerator->BeginGetEvent((IMFAsyncCallback*)&streamSinkMediaEvtHandler, pstreamSinkEventGenerator);
	if (FAILED(hr))
	{
		LogError("BeginGetEvent for stream sink event generator", hr);
		return hr;
	}

	return S_OK;
}

HRESULT VideoPlayer::SetupPresentationClock()
{
	HRESULT hr = MFCreatePresentationClock(&pClock);
	if (FAILED(hr))
	{
		LogError("MFCreatePresentationClock", hr);
		return hr;
	}

	hr = MFCreateSystemTimeSource(&pTimeSource);
	if (FAILED(hr))
	{
		LogError("MFCreateSystemTimeSource", hr);
		return hr;
	}

	hr = pClock->SetTimeSource(pTimeSource);
	if (FAILED(hr))
	{
		LogError("SetTimeSource", hr);
		return hr;
	}

	hr = pVideoSink->SetPresentationClock(pClock);
	if (FAILED(hr))
	{
		LogError("SetPresentationClock", hr);
		return hr;
	}

	return S_OK;
}

// ============================================================================
// VideoPlayer Implementation - Main Operations
// ============================================================================

int VideoPlayer::initialize()
{
	DebugLog("VideoPlayer::initialize() - start");

	// Validate parameters
	HRESULT hr = ValidateInitializationParameters();
	if (FAILED(hr))
	{
		return -1;
	}

	// Initialize Media Foundation
	hr = MFStartup(MF_VERSION);
	if (FAILED(hr))
	{
		LogError("MFStartup", hr);
		return -1;
	}

	// Set up Video Sink (Enhanced Video Renderer)
	hr = CreateAndInitializeVideoSink();
	if (FAILED(hr))
	{
		return -2;
	}

	hr = SetupVideoDisplayControl();
	if (FAILED(hr))
	{
		return -3;
	}

	hr = GetStreamSinkAndMediaTypeHandler();
	if (FAILED(hr))
	{
		return -4;
	}

	// Set up Video Source
	hr = CreateVideoSourceAndReader();
	if (FAILED(hr))
	{
		return -5;
	}

	hr = ConfigureVideoSourceStream();
	if (FAILED(hr))
	{
		return -6;
	}

	hr = GetSourceDescriptors();
	if (FAILED(hr))
	{
		return -7;
	}

	// Create and set media types
	hr = CreateAndSetMediaTypes();
	if (FAILED(hr))
	{
		return -8;
	}

	// Set up event handlers
	hr = SetupEventHandlers();
	if (FAILED(hr))
	{
		return -9;
	}

	// Set up presentation clock
	hr = SetupPresentationClock();
	if (FAILED(hr))
	{
		return -10;
	}

	DebugLog("VideoPlayer::initialize() - success");
	return 0;
}

int VideoPlayer::play()
{
	DebugLog("VideoPlayer::play() - start");

	if (pClock == nullptr)
	{
		DebugLog("VideoPlayer::play() - pClock is null");
		return -1;
	}

	HRESULT hr = pClock->Start(0);
	if (FAILED(hr))
	{
		LogError("pClock->Start", hr);
		return -1;
	}

	isPlaying = true;
	isPaused = false;

	hr = pVideoReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
	if (FAILED(hr))
	{
		LogError("ReadSample", hr);
		return -2;
	}

	DebugLog("VideoPlayer::play() - success (live streaming mode)");
	return 0;
}

int VideoPlayer::pause()
{
	DebugLog("VideoPlayer::pause() - start");

	if (pClock == nullptr)
	{
		DebugLog("VideoPlayer::pause() - pClock is null");
		return -1;
	}

	HRESULT hr = pClock->Pause();
	if (FAILED(hr))
	{
		LogError("pClock->Pause", hr);
		return -1;
	}

	isPaused = true;
	isPlaying = false;

	DebugLog("VideoPlayer::pause() - success");
	return 0;
}

int VideoPlayer::stop()
{
	DebugLog("VideoPlayer::stop() - start");

	if (pClock != nullptr)
	{
		HRESULT hr = pClock->Stop();
		if (FAILED(hr))
		{
			LogError("pClock->Stop", hr);
		}
	}

	isPlaying = false;
	isPaused = false;

	DebugLog("VideoPlayer::stop() - success");
	return 0;
}

// ============================================================================
// C-style interface for C# interop
// ============================================================================

extern "C" {
	__declspec(dllexport) VideoPlayer* CreateVideoPlayer()
	{
		return new VideoPlayer();
	}

	__declspec(dllexport) void DestroyVideoPlayer(VideoPlayer* player)
	{
		if (player)
		{
			delete player;
		}
	}

	__declspec(dllexport) void SetVideoPath(VideoPlayer* player, LPWSTR path)
	{
		if (player)
		{
			player->setVideoPath(path);
		}
	}

	__declspec(dllexport) void SetWindowHandle(VideoPlayer* player, HWND hwnd)
	{
		if (player)
		{
			player->setWindowHandle(hwnd);
		}
	}

	__declspec(dllexport) int InitializePlayer(VideoPlayer* player)
	{
		if (player)
		{
			return player->initialize();
		}
		return -1;
	}

	__declspec(dllexport) int PlayVideo(VideoPlayer* player)
	{
		if (player)
		{
			return player->play();
		}
		return -1;
	}

	__declspec(dllexport) int PauseVideo(VideoPlayer* player)
	{
		if (player)
		{
			return player->pause();
		}
		return -1;
	}

	__declspec(dllexport) int StopVideo(VideoPlayer* player)
	{
		if (player)
		{
			return player->stop();
		}
		return -1;
	}

	__declspec(dllexport) bool IsPlaying(VideoPlayer* player)
	{
		if (player)
		{
			return player->getIsPlaying();
		}
		return false;
	}

	__declspec(dllexport) bool IsPaused(VideoPlayer* player)
	{
		if (player)
		{
			return player->getIsPaused();
		}
		return false;
	}
}

// ============================================================================
// DLL Entry Point
// ============================================================================

BOOL APIENTRY UserSpaceMF(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
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
