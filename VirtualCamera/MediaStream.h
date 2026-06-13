#pragma once
#include "FrameGenerator.h"
#include "RtspSessionManager.h"
#include <atomic>

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

	// HRESULT InitializeRTSPReader();
	HRESULT CopyRtspFrame(const RtspFrameSnapshot& frame, IMFSample* targetSample);
	// GPU zero-copy: CopySubresourceRegion between two DXGI textures on the shared device.
	HRESULT CopyRtspFrameGpu(IMFDXGIBuffer* srcDxgi, IMFDXGIBuffer* dstDxgi);
	// CPU fallback: a single MFCopyImage from the source MF buffer into the dest buffer.
	HRESULT CopyRtspFrameCpu(IMFMediaBuffer* srcBuffer, IMFMediaBuffer* dstBuffer);
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
	wil::com_ptr_nothrow<IUnknown> _dxgiManager;
	wil::com_ptr_nothrow<IMFDXGIDeviceManager> _deviceManager; // QI of _dxgiManager, for GPU copy
	HANDLE _deviceHandle = nullptr;                            // open device handle for LockDevice
	int _index;

	UINT32 _videoWidth;
	UINT32 _videoHeight;
	UINT32 _videoStride;  // bytes per row of the actual RTSP output (may include padding)
	UINT32 _hintFpsNum;   // fps numerator from app-side VCamConfig (default 30)
	UINT32 _hintFpsDen;   // fps denominator from app-side VCamConfig (default 1)
	UINT64 _generation = 0;
	IRtspSessionManager* _rtspManager = nullptr;

	// Fallback frame generator (used when RTSP is unavailable)
	FrameGenerator _frameGenerator;
};
