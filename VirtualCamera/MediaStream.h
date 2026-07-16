#pragma once
#include "FrameGenerator.h"
#include "CameraSessionConfig.h"
#include <atomic>
#include <deque>

struct MediaStream : winrt::implements<MediaStream, CBaseAttributes<IMFAttributes>, IMFMediaStream2, IKsControl>
{
public:
	// IMFMediaEventGenerator
	STDMETHOD(BeginGetEvent)(IMFAsyncCallback* pCallback, IUnknown* punkState);
	STDMETHOD(EndGetEvent)(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent);
	STDMETHOD(GetEvent)(DWORD dwFlags, IMFMediaEvent** ppEvent);
	STDMETHOD(QueueEvent)(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue);

	// IMFMediaStream
	STDMETHOD(GetMediaSource)(IMFMediaSource** ppMediaSource);
	STDMETHOD(GetStreamDescriptor)(IMFStreamDescriptor** ppStreamDescriptor);
	STDMETHOD(RequestSample)(IUnknown* pToken);

	// IMFMediaStream2
	STDMETHOD(SetStreamState)(MF_STREAM_STATE value);
	STDMETHOD(GetStreamState)(MF_STREAM_STATE* value);

	// IKsControl
	STDMETHOD_(NTSTATUS, KsProperty)(PKSPROPERTY Property, ULONG PropertyLength, LPVOID PropertyData, ULONG DataLength, ULONG* BytesReturned);
	STDMETHOD_(NTSTATUS, KsMethod)(PKSMETHOD Method, ULONG MethodLength, LPVOID MethodData, ULONG DataLength, ULONG* BytesReturned);
	STDMETHOD_(NTSTATUS, KsEvent)(PKSEVENT Event, ULONG EventLength, LPVOID EventData, ULONG DataLength, ULONG* BytesReturned);

public:
	MediaStream() :
		_index(0),
		_state(MF_STREAM_STATE_STOPPED),
		_format(GUID_NULL),
		_videoWidth(1920),
		_videoHeight(1080),
		_videoStride(0),
		_hintFpsNum(30),
		_hintFpsDen(1)
	{
		SetBaseAttributesTraceName(L"MediaStreamAtts");
	}

	HRESULT Initialize(IMFMediaSource* source, int index);
	HRESULT SetAllocator(IUnknown* allocator);
	MFSampleAllocatorUsage GetAllocatorUsage();
	HRESULT SetD3DManager(IUnknown* manager);
	HRESULT Start(IMFMediaType* type);
	HRESULT Stop();
	void Shutdown();
	HRESULT SetRuntimeContext(const StreamRuntimeContext& context);

	// Sets the exact output resolution, fps and format from the app-side probe.
	// Must be called before Start(). Rebuilds the stream descriptor immediately.
	HRESULT SetVideoConfig(UINT32 width, UINT32 height, UINT32 fpsNum, UINT32 fpsDen, GUID format);

private:
#if _DEBUG
	int32_t query_interface_tearoff(winrt::guid const& id, void** object) const noexcept override
	{
		RETURN_HR_MSG(E_NOINTERFACE, "MediaStream QueryInterface failed on IID %s", GUID_ToStringW(id).c_str());
	}
#endif

	// Copies a contiguous NV12 source (Y then interleaved UV at src + srcPitch*height)
	// into the destination sample buffer, honoring the dest's real pitch (2D or flat).
	HRESULT CopyNv12ToSample(IMFMediaBuffer* dstBuffer, const BYTE* src, LONG srcPitch, UINT32 width, UINT32 height);
	// Reads the latest NV12 frame the app published into the frame shared memory
	// (FrameChannelReader) and copies it into targetSample. Returns S_FALSE if no
	// fresh frame is available (caller falls back to the synthetic frame).
	HRESULT CopyFrameChannelFrame(IMFSample* targetSample);
	// Diagnostic overlay: burns a decimal counter into the Y plane of a delivered NV12
	// sample (for real frames; the synthetic path draws its own via FrameGenerator).
	// Best-effort — silently does nothing if the buffer can't be locked as NV12.
	void DrawOverlayCounter(IMFSample* sample, UINT64 value);

	// --- Asynchronous two-queue delivery (per the MS custom-media-source model) -----
	// RequestSample queues the request token and returns immediately (never blocks a
	// work-queue thread). A periodic threadpool timer marks one frame "due" per frame
	// interval; DispatchSamples pairs a due credit with a pending request and produces +
	// delivers exactly one sample. This paces delivery to the advertised frame rate
	// without the fast NV12 copy path free-running to hundreds of samples/sec.
	void DispatchSamples();                    // under _lock: drain (due AND pending) pairs
	HRESULT ProduceAndQueue(IUnknown* pToken); // under _lock: allocate → fill → MEMediaSample
	void OnDeliveryTick();                      // timer callback body (takes _lock)
	static void CALLBACK DeliveryTimerThunk(PTP_CALLBACK_INSTANCE, void* ctx, PTP_TIMER);

	HRESULT BuildFrameTypeNV12(wil::com_ptr_nothrow<IMFMediaType>& nv12Type);
	HRESULT BuildDescriptor();

	winrt::slim_mutex  _lock;
	MF_STREAM_STATE _state;
	GUID _format;
	wil::com_ptr_nothrow<IMFStreamDescriptor> _descriptor;
	wil::com_ptr_nothrow<IMFMediaEventQueue> _queue;
	wil::com_ptr_nothrow<IMFMediaSource> _source;
	wil::com_ptr_nothrow<IMFVideoSampleAllocatorEx> _allocator;
	wil::com_ptr_nothrow<IMFMediaType> _currentType; // cached from last Start(), used by SetStreamState(RUNNING)
	int _index;

	UINT32 _videoWidth;
	UINT32 _videoHeight;
	UINT32 _videoStride;  // bytes per row of the actual RTSP output (may include padding)
	UINT32 _hintFpsNum;   // fps numerator from app-side VCamConfig (default 30)
	UINT32 _hintFpsDen;   // fps denominator from app-side VCamConfig (default 1)
	UINT64 _generation = 0;

	// Fallback frame generator (synthetic frame shown before the first real frame
	// arrives, or whenever the app producer's heartbeat is stale).
	FrameGenerator _frameGenerator;

	// --- Live diagnostics (StatsPublisher, see Shared/VCamStats.h) ---------
	// All of these are only ever touched from RequestSample(), which already
	// runs under _lock, so no extra synchronization is needed here.
	UINT64 _renderedFrameCount = 0;   // frames actually handed to the requesting app (real copy, includes re-serves)
	UINT64 _declinedFrameCount = 0;   // of the above, how many were a re-serve of the same frame as last
	                                   // time (name kept from an earlier, reverted attempt at actually declining
	                                   // these — see RequestSample; it's a count only, delivery is unaffected)
	UINT64 _lastDeliveredFrameSeq = 0; // frameSeq of the last frame delivered (dup detection)
	bool   _hasDeliveredFrame = false; // false until the first frame is ever delivered
	double _lastCopyMs = 0.0;         // cost of the last frame-channel copy (always CPU)

	// The app producer's rx counter and heartbeat freshness from the last RequestSample.
	UINT64 _frameChannelRxFrames = 0; // header framesWritten of the last frame we saw (producer's rx counter)
	bool   _ffmpegFresh = false;      // producer heartbeat was fresh on the last RequestSample

	// --- Diagnostic frame-counter overlay ----------------------------------
	// When enabled (VCamConfig.overlay), every delivered sample gets a decimal counter
	// burned into it — so you can see on the real video (Zoom/preview) at what rate the
	// consumer actually receives distinct frames, and confirm when synthetic frames are
	// being served. Counts every delivery, so it advances at the "render" rate.
	bool   _overlayEnabled = false;
	UINT64 _overlayCounter = 0;

	// --- Delivery pacing (async two-queue model) ----------------------------
	// The Frame Server re-requests a sample as soon as the previous one is delivered,
	// so instead of blocking we hold outstanding requests here and let a periodic
	// threadpool timer gate delivery to the frame rate. See DispatchSamples().
	std::deque<wil::com_ptr_nothrow<IUnknown>> _requests; // pending request tokens (may hold null)
	bool      _frameDue = false;             // a timer tick made one frame due (max one credit)
	PTP_TIMER _deliveryTimer = nullptr;      // periodic pacing timer, armed in Start()

	// Publishes the current snapshot (whatever the outcome of this RequestSample
	// call) to StatsPublisher. Called once at the end of RequestSample().
	void PublishStats();
};
