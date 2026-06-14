#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <evr.h>
#include <string>
#include <windows.h>

// Forward declarations
class VideoReaderCall;
class CSourceOpenMonitor;
struct PreviewStats; // defined in VideoReaderCbk.h

struct StreamInfo
{
	UINT32 width = 0;
	UINT32 height = 0;

	GUID subtype = GUID_NULL;

	UINT32 fpsNum = 0;
	UINT32 fpsDen = 0;

	UINT32 bitrate = 0;
};

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

	// Configuration
	std::wstring filePath;
	HWND windowHandle;
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

	// Playback setup (called on each play)
	HRESULT CreateVideoSourceAndReader();
	HRESULT ConfigureVideoSourceStream();
	HRESULT GetSourceDescriptors();
	HRESULT CreateAndSetMediaTypes();
	HRESULT SetupPresentationClock();

public:
	VideoPlayer();
	~VideoPlayer();

	void setVideoPath(LPCWSTR path);
	void setWindowHandle(HWND hwnd);
	int initialize();
	int play();
	int pause();
	int stop();
	int getVideoStreamInfo(StreamInfo* pInfo, UINT32 max, UINT32* count);
	int getPreviewStats(PreviewStats* pStats);

	void updateVideoPosition();  // Update video position when window is resized
	bool getIsPlaying() const { return isPlaying; }
	bool getIsPaused() const { return isPaused; }
};