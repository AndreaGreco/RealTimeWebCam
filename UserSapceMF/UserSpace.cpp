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

template <class T> void SAFE_DELETE(T*& pT)
{
	if (pT != NULL)
	{
		delete pT;
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
	
	HRESULT hr = MFCreateAttributes(&pVideoReaderAttributes, 3);
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

	// Enable video processing for automatic format conversion when needed
	hr = pVideoReaderAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
	if (FAILED(hr))
	{
		LogError("SetUINT32 MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING", hr);
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
	// One-time initialization resources (created once, released in destructor)
	IMFMediaSink* pVideoSink;
	IMFStreamSink* pStreamSink;
	IMFVideoRenderer* pVideoRenderer;
	IMFVideoDisplayControl* pVideoDisplayControl;
	IMFGetService* pService;
	IMFActivate* pActive;
	IMFMediaTypeHandler* pSinkMediaTypeHandler;
	
	// Playback resources (created in initialize/play, released in stop)
	IMFMediaSource* pVideoSource;
	IMFSourceReader* pVideoReader;
	IMFMediaType* videoSourceOutputType;
	IMFMediaType* pVideoSourceOutType;
	IMFMediaType* pImfEvrSinkType;
	IMFPresentationDescriptor* pSourcePresentationDescriptor;
	IMFStreamDescriptor* pSourceStreamDescriptor;
	IMFMediaTypeHandler* pSourceMediaTypeHandler;
	IMFPresentationClock* pClock;
	IMFPresentationTimeSource* pTimeSource;
	VideoReaderCall* videoReaderCallback;
	CSourceOpenMonitor* pSourceOpenMonitor;

	// Event handlers
	IMFMediaEventGenerator* pEventGenerator;
	IMFMediaEventGenerator* pstreamSinkEventGenerator;
	MediaEventHandler mediaEvtHandler;
	MediaEventHandler streamSinkMediaEvtHandler;

	// Unused/deprecated members
	IMFMediaType* pvideoSourceModType;
	IMFMediaType* pHintMediaType;
	IMFSample* pD3DVideoSample;

	// Configuration
	LPWSTR filePath;
	HWND windowHandle;
	RECT rc;
	BOOL fSelected;
	bool isPlaying;
	bool isPaused;
	bool isInitialized;  // Track if one-time initialization is done

	// Initialization helpers
	void InitializeVariables();
	void ReleaseAllResources();
	void ReleasePlaybackResources();
	HRESULT ValidateInitializationParameters();
	
	// One-time setup (called once)
	HRESULT CreateAndInitializeVideoSink();
	HRESULT SetupVideoDisplayControl();
	HRESULT GetStreamSinkAndMediaTypeHandler();
	HRESULT SetupEventHandlers();
	
	// Playback setup (called on each play)
	HRESULT CreateVideoSourceAndReader();
	HRESULT ConfigureVideoSourceStream();
	HRESULT GetSourceDescriptors();
	HRESULT CreateAndSetMediaTypes();
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
	isInitialized = false;
}

VideoPlayer::~VideoPlayer()
{
	stop();
	ReleaseAllResources();
	MFShutdown();
}

void VideoPlayer::InitializeVariables()
{
	// One-time resources
	pVideoSink = NULL;
	pStreamSink = NULL;
	pVideoRenderer = NULL;
	pVideoDisplayControl = NULL;
	pService = NULL;
	pActive = NULL;
	pSinkMediaTypeHandler = NULL;
	
	// Playback resources
	pVideoSource = NULL;
	pVideoReader = NULL;
	videoSourceOutputType = NULL;
	pVideoSourceOutType = NULL;
	pImfEvrSinkType = NULL;
	pSourcePresentationDescriptor = NULL;
	pSourceStreamDescriptor = NULL;
	pSourceMediaTypeHandler = NULL;
	pClock = NULL;
	pTimeSource = NULL;
	videoReaderCallback = NULL;
	pSourceOpenMonitor = NULL;
	
	// Event handlers
	pEventGenerator = NULL;
	pstreamSinkEventGenerator = NULL;
	
	// Unused
	pvideoSourceModType = NULL;
	pHintMediaType = NULL;
	pD3DVideoSample = NULL;
}

void VideoPlayer::ReleasePlaybackResources()
{
	DebugLog("ReleasePlaybackResources - start");

	// Stop the clock first
	if (pClock)
	{
		pClock->Stop();
	}

	// Release source reader and related
	SAFE_RELEASE(pVideoReader);
	SAFE_RELEASE(videoSourceOutputType);
	SAFE_RELEASE(pVideoSourceOutType);
	SAFE_RELEASE(pImfEvrSinkType);
	
	// Release source and descriptors
	SAFE_RELEASE(pSourceMediaTypeHandler);
	SAFE_RELEASE(pSourceStreamDescriptor);
	SAFE_RELEASE(pSourcePresentationDescriptor);
	SAFE_RELEASE(pVideoSource);
	
	// Release clock resources
	SAFE_RELEASE(pClock);
	SAFE_RELEASE(pTimeSource);
	
	// Delete callback and monitor
	SAFE_DELETE(videoReaderCallback);
	SAFE_RELEASE(pSourceOpenMonitor);
	
	DebugLog("ReleasePlaybackResources - complete");
}

void VideoPlayer::ReleaseAllResources()
{
	DebugLog("ReleaseAllResources - start");
	
	// First release playback resources
	ReleasePlaybackResources();
	
	// Then release event handlers
	SAFE_RELEASE(pstreamSinkEventGenerator);
	SAFE_RELEASE(pEventGenerator);
	
	// Release one-time sink resources
	SAFE_RELEASE(pSinkMediaTypeHandler);
	SAFE_RELEASE(pStreamSink);
	SAFE_RELEASE(pVideoDisplayControl);
	SAFE_RELEASE(pService);
	SAFE_RELEASE(pVideoRenderer);
	SAFE_RELEASE(pVideoSink);
	SAFE_RELEASE(pActive);
	
	// Release unused resources
	SAFE_RELEASE(pvideoSourceModType);
	SAFE_RELEASE(pHintMediaType);
	SAFE_RELEASE(pD3DVideoSample);
	
	isInitialized = false;
	
	DebugLog("ReleaseAllResources - complete");
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
	// Try formats in order of preference: YUY2 (hybrid) -> RGB32 (SW)
	// Note: With video processing enabled, source reader will automatically convert from decoder output
	const GUID formatsToTry[] = { MFVideoFormat_YUY2, MFVideoFormat_RGB32 };
	const int numFormats = sizeof(formatsToTry) / sizeof(formatsToTry[0]);
	
	HRESULT hr = E_FAIL;
	bool formatAccepted = false;

	for (int i = 0; i < numFormats && !formatAccepted; i++)
	{
		// Create media type with current format
		SAFE_RELEASE(pVideoSourceOutType);
		hr = MFCreateMediaType(&pVideoSourceOutType);
		if (FAILED(hr)) continue;

		hr = pVideoSourceOutType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
		if (FAILED(hr)) continue;

		hr = pVideoSourceOutType->SetGUID(MF_MT_SUBTYPE, formatsToTry[i]);
		if (FAILED(hr)) continue;

		hr = pVideoSourceOutType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
		if (FAILED(hr)) continue;

		hr = pVideoSourceOutType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
		if (FAILED(hr)) continue;

		hr = MFSetAttributeRatio(pVideoSourceOutType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
		if (FAILED(hr)) continue;

		hr = CopyAttribute(videoSourceOutputType, pVideoSourceOutType, MF_MT_FRAME_SIZE);
		if (FAILED(hr)) continue;

		hr = CopyAttribute(videoSourceOutputType, pVideoSourceOutType, MF_MT_FRAME_RATE);
		if (FAILED(hr)) continue;

		// CRITICAL: Tell the Source Reader to DECODE to this format
		// Without this, we get compressed data (H.264) instead of decoded frames
		hr = pVideoReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, pVideoSourceOutType);
		if (FAILED(hr))
		{
			char logMsg[256];
			snprintf(logMsg, sizeof(logMsg), 
					"SetCurrentMediaType failed for format %08X, trying next format", formatsToTry[i].Data1);
			DebugLog(logMsg);
			continue;
		}

		// Create sink type
		SAFE_RELEASE(pImfEvrSinkType);
		hr = MFCreateMediaType(&pImfEvrSinkType);
		if (FAILED(hr)) continue;

		hr = pVideoSourceOutType->CopyAllItems(pImfEvrSinkType);
		if (FAILED(hr)) continue;

		// Try to set on sink - if this succeeds, we're good
		hr = pSinkMediaTypeHandler->SetCurrentMediaType(pImfEvrSinkType);
		if (SUCCEEDED(hr))
		{
			char logMsg[256];
			snprintf(logMsg, sizeof(logMsg), 
					"Successfully configured pipeline with format: %08X", formatsToTry[i].Data1);
			DebugLog(logMsg);
			formatAccepted = true;
			break;
		}
	}

	if (!formatAccepted)
	{
		LogError("No compatible format found for EVR sink", hr);
		return E_FAIL;
	}

	// Allocate internal buffer with validated media type
	hr = videoReaderCallback->AllocateInternalBuffer(pImfEvrSinkType, pVideoReader);
	if (FAILED(hr))
	{
		LogError("AllocateInternalBuffer", hr);
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

	// One-time initialization
	if (!isInitialized)
	{
		// Initialize Media Foundation
		hr = MFStartup(MF_VERSION);
		if (FAILED(hr))
		{
			LogError("MFStartup", hr);
			return -1;
		}

		// Set up Video Sink (Enhanced Video Renderer) - ONE TIME
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

		// Set up event handlers - ONE TIME
		hr = SetupEventHandlers();
		if (FAILED(hr))
		{
			return -9;
		}

		isInitialized = true;
		DebugLog("One-time initialization complete");
	}

	// Always clean up previous playback resources before creating new ones
	ReleasePlaybackResources();

	// Set up Video Source - EVERY TIME (may change)
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
		DebugLog("VideoPlayer::play() - pClock is null, need to call initialize first");
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

	// Stop playback
	isPlaying = false;
	isPaused = false;

	// Stop the clock
	if (pClock != nullptr)
	{
		HRESULT hr = pClock->Stop();
		if (FAILED(hr))
		{
			LogError("pClock->Stop", hr);
		}
	}

	// Release playback resources to prevent memory leaks
	ReleasePlaybackResources();

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
