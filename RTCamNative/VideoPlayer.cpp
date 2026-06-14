// VideoPlayer.cpp : Implementation of VideoPlayer class
#include "pch.h"
#include "VideoPlayer.h"
#include "Logger.h"
#include "MediaEventHandler.h"
#include "VideoReaderCbk.h"
#include "CSourceOpenMonitor.h"
#include <combaseapi.h>
#include <evr.h>
#include <guiddef.h>
#include <mftransform.h>
// Note: ICodecAPI / CODECAPI_* come in transitively via strmif.h here; including
// <icodecapi.h> as well causes a CodecAPIEventData redefinition (C2011).
#include <ios>
#include <mfapi.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <PropIdl.h>
#include <sstream>
#include <cwchar>
#include <Unknwnbase.h>
#include <windows.h>

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
* Force any decoder MFT in the reader's video stream into low-latency mode.
* MF_LOW_LATENCY on the reader is not always honored by the decoder, which can
* keep a multi-frame buffer. Setting CODECAPI_AVLowLatencyMode directly on the
* decoder collapses it. GUID defined locally to avoid a codec-API GUID lib link.
* Harmless if the decoder does not expose ICodecAPI.
*/
static void ApplyDecoderLowLatency(IMFSourceReader* pReader)
{
	// CODECAPI_AVLowLatencyMode {9C27891A-ED7A-40E1-88E8-B22727A024EE}
	static const GUID kAVLowLatencyMode =
		{ 0x9c27891a, 0xed7a, 0x40e1, { 0x88, 0xe8, 0xb2, 0x27, 0x27, 0xa0, 0x24, 0xee } };

	IMFSourceReaderEx* pReaderEx = nullptr;
	if (FAILED(pReader->QueryInterface(IID_PPV_ARGS(&pReaderEx))) || !pReaderEx)
	{
		DebugLog("ApplyDecoderLowLatency - IMFSourceReaderEx unavailable, skipping");
		return;
	}

	for (DWORD i = 0; ; i++)
	{
		GUID category = GUID_NULL;
		IMFTransform* pMft = nullptr;
		if (FAILED(pReaderEx->GetTransformForStream(MF_SOURCE_READER_FIRST_VIDEO_STREAM, i, &category, &pMft)) || !pMft)
			break;

		ICodecAPI* pCodec = nullptr;
		if (SUCCEEDED(pMft->QueryInterface(IID_PPV_ARGS(&pCodec))) && pCodec)
		{
			VARIANT v;
			VariantInit(&v);
			v.vt = VT_BOOL;
			v.boolVal = VARIANT_TRUE;
			HRESULT hr = pCodec->SetValue(&kAVLowLatencyMode, &v);
			char msg[128];
			snprintf(msg, sizeof(msg), "ApplyDecoderLowLatency - transform %lu SetValue: 0x%08X", i, hr);
			DebugLog(msg);
			pCodec->Release();
		}
		pMft->Release();
	}

	pReaderEx->Release();
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

int VideoPlayer::getVideoStreamInfo(StreamInfo *pInfo, UINT32 maxBuffElement, UINT32 *count)
{
	IMFPresentationDescriptor* pPD = nullptr;
	DWORD streamCount = 0;

	if (!this->pVideoSource || !pInfo)
		return E_INVALIDARG;

	if (!count)
		return E_INVALIDARG;

	HRESULT hr = pVideoSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr))
		return hr;

	hr = pPD->GetStreamDescriptorCount(&streamCount);
	if (FAILED(hr))
		goto done;
	
	// return user correct no of streams
	*count = streamCount;

	if (streamCount > maxBuffElement)
		streamCount = maxBuffElement;

	for (DWORD i = 0; i < streamCount; i++)
	{
		StreamInfo *info = &pInfo[i];
		BOOL selected = FALSE;

		IMFStreamDescriptor* pSD = nullptr;
		IMFMediaTypeHandler* pHandler = nullptr;

		hr = pPD->GetStreamDescriptorByIndex(i, &selected, &pSD);
		if (FAILED(hr))
			continue;

		hr = pSD->GetMediaTypeHandler(&pHandler);
		if (FAILED(hr))
		{
			SAFE_RELEASE(pSD);
			continue;
		}

		GUID majorType = GUID_NULL;

		hr = pHandler->GetMajorType(&majorType);
		if (FAILED(hr))
		{
			SAFE_RELEASE(pHandler);
			SAFE_RELEASE(pSD);
			continue;
		}

		if (majorType == MFMediaType_Video)
		{
			IMFMediaType* pType = nullptr;

			hr = pHandler->GetCurrentMediaType(&pType);
			if (SUCCEEDED(hr))
			{
				MFGetAttributeSize(
					pType,
					MF_MT_FRAME_SIZE,
					&info->width,
					&info->height
				);

				MFGetAttributeRatio(
					pType,
					MF_MT_FRAME_RATE,
					&info->fpsNum,
					&info->fpsDen
				);

				pType->GetGUID(
					MF_MT_SUBTYPE,
					&info->subtype
				);

				pType->GetUINT32(
					MF_MT_AVG_BITRATE,
					&info->bitrate
				);

				SAFE_RELEASE(pType);

				break;
			}
		}

		SAFE_RELEASE(pHandler);
		SAFE_RELEASE(pSD);
	}

done:
	SAFE_RELEASE(pPD);

	return hr;
}


/**
* Creates media source from URL with source open monitor.
*/
HRESULT CreateMediaSourceFromURL(LPCWSTR path, CSourceOpenMonitor* pMonitor, IMFMediaSource** ppSource)
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

HRESULT CreateMediaSourceFromCaptureDevice(LPCWSTR deviceName, IMFMediaSource** ppSource)
{
	IMFAttributes* pAttributes = NULL;
	IMFActivate** ppDevices = NULL;
	UINT32 deviceCount = 0;
	HRESULT hr = MFCreateAttributes(&pAttributes, 1);
	if (FAILED(hr))
	{
		LogError("MFCreateAttributes for capture devices", hr);
		return hr;
	}

	hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
	if (FAILED(hr))
	{
		LogError("SetGUID MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE", hr);
		SAFE_RELEASE(pAttributes);
		return hr;
	}

	hr = MFEnumDeviceSources(pAttributes, &ppDevices, &deviceCount);
	if (FAILED(hr))
	{
		LogError("MFEnumDeviceSources", hr);
		SAFE_RELEASE(pAttributes);
		return hr;
	}

	hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	for (UINT32 i = 0; i < deviceCount; i++)
	{
		WCHAR* friendlyName = nullptr;
		UINT32 friendlyNameLength = 0;
		HRESULT hrName = ppDevices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &friendlyName, &friendlyNameLength);
		if (SUCCEEDED(hrName) && friendlyName != nullptr)
		{
			if (_wcsicmp(friendlyName, deviceName) == 0)
			{
				hr = ppDevices[i]->ActivateObject(IID_PPV_ARGS(ppSource));
				CoTaskMemFree(friendlyName);
				break;
			}
			CoTaskMemFree(friendlyName);
		}
	}

	for (UINT32 i = 0; i < deviceCount; i++)
	{
		SAFE_RELEASE(ppDevices[i]);
	}

	CoTaskMemFree(ppDevices);
	SAFE_RELEASE(pAttributes);
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

int VideoPlayer::getPreviewStats(PreviewStats* pStats)
{
	if (!pStats)
		return E_INVALIDARG;

	if (!videoReaderCallback)
		return E_FAIL;

	videoReaderCallback->GetStats(pStats);
	return S_OK;
}

// ============================================================================
// VideoPlayer Implementation - Constructor/Destructor
// ============================================================================

VideoPlayer::VideoPlayer()
{
	InitializeVariables();
	windowHandle = nullptr;
	fSelected = false;
	isPlaying = false;
	isPaused = false;
	isInitialized = false;
}

VideoPlayer::~VideoPlayer()
{
	stop();
	ReleaseAllResources();

	// Only shutdown if it was initialized
	if (isInitialized)
	{
		MFShutdown();
	}
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

	if (videoReaderCallback)
	{
		videoReaderCallback->Shutdown();
	}

	if (pVideoReader)
	{
		HRESULT hrFlush = pVideoReader->Flush((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM);
		if (FAILED(hrFlush))
		{
			LogError("IMFSourceReader::Flush", hrFlush);
		}
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

	// Release callback and monitor
	if (videoReaderCallback)
	{
		videoReaderCallback->Release();
		videoReaderCallback = NULL;
	}
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

void VideoPlayer::setVideoPath(LPCWSTR path)
{
	filePath = (path != nullptr) ? path : L"";
	captureDeviceName.clear();
}

void VideoPlayer::setCaptureDeviceName(LPCWSTR name)
{
	captureDeviceName = (name != nullptr) ? name : L"";
	filePath.clear();
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

	if (filePath.empty() && captureDeviceName.empty())
	{
		DebugLog("VideoPlayer::initialize() - no input source configured");
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

	// Get the actual window size and use entire area
	RECT windowRect;
	GetClientRect(windowHandle, &windowRect);
	
	hr = pVideoDisplayControl->SetVideoPosition(NULL, &windowRect);
	if (FAILED(hr))
	{
		LogError("SetVideoPosition", hr);
		return hr;
	}

	// Set aspect ratio mode to preserve video proportions (centered with letterboxing)
	hr = pVideoDisplayControl->SetAspectRatioMode(MFVideoARMode_PreservePicture);
	if (FAILED(hr))
	{
		LogError("SetAspectRatioMode", hr);
		// Non-critical, continue anyway
	}

	DebugLog("Video display configured with dynamic sizing and aspect ratio preservation");
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
	HRESULT hr = S_OK;

	if (!captureDeviceName.empty())
	{
		hr = CreateMediaSourceFromCaptureDevice(captureDeviceName.c_str(), &pVideoSource);
		if (FAILED(hr))
		{
			LogError("CreateMediaSourceFromCaptureDevice", hr);
			return hr;
		}

		DebugLog("VideoPlayer using video capture device source");
	}
	else
	{
		// Create the source open monitor
		pSourceOpenMonitor = new (std::nothrow) CSourceOpenMonitor();
		if (pSourceOpenMonitor == NULL)
		{
			LogError("Failed to create CSourceOpenMonitor", E_OUTOFMEMORY);
			return E_OUTOFMEMORY;
		}

		// Create media source from URL
		hr = CreateMediaSourceFromURL(filePath.c_str(), pSourceOpenMonitor, &pVideoSource);
		if (FAILED(hr))
		{
			LogError("CreateMediaSourceFromURL", hr);
			return hr;
		}

		// Configure for low latency
		ConfigureSourceForLowLatency(pVideoSource);
	}

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

	// Hand the clock to the reader callback so it stamps frames "now" (present-on-
	// arrival) rather than honoring the source PTS, which would let the EVR queue.
	if (videoReaderCallback)
	{
		videoReaderCallback->SetPresentationClock(pClock);
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

	// Collapse the decoder's internal frame buffer now that it has been
	// instantiated (the media type is set). Must run before the first read.
	ApplyDecoderLowLatency(pVideoReader);

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

void VideoPlayer::updateVideoPosition()
{
	if (pVideoDisplayControl == nullptr)
	{
		DebugLog("VideoPlayer::updateVideoPosition() - pVideoDisplayControl is null");
		return;
	}

	if (windowHandle == nullptr || !IsWindow(windowHandle))
	{
		DebugLog("VideoPlayer::updateVideoPosition() - invalid window handle");
		return;
	}

	// Get current window client area
	RECT windowRect;
	GetClientRect(windowHandle, &windowRect);

	// Update video position to fill the entire window
	HRESULT hr = pVideoDisplayControl->SetVideoPosition(NULL, &windowRect);
	if (FAILED(hr))
	{
		LogError("SetVideoPosition in updateVideoPosition", hr);
		return;
	}

	DebugLog("VideoPlayer::updateVideoPosition() - video position updated");
}

