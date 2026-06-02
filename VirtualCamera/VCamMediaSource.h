#pragma once
#include <wchar.h>
#include "RtspSessionManager.h"

struct MediaStream;

struct VCamMediaSource : winrt::implements<VCamMediaSource, CBaseAttributes<IMFAttributes>, IMFMediaSourceEx, IMFGetService, IKsControl, IMFSampleAllocatorControl>
{
public:
	// IMFMediaEventGenerator
	STDMETHOD(BeginGetEvent)(IMFAsyncCallback* pCallback, IUnknown* punkState);
	STDMETHOD(EndGetEvent)(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent);
	STDMETHOD(GetEvent)(DWORD dwFlags, IMFMediaEvent** ppEvent);
	STDMETHOD(QueueEvent)(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue);

	// IMFMediaSource
	STDMETHOD(CreatePresentationDescriptor)(IMFPresentationDescriptor** ppPresentationDescriptor);
	STDMETHOD(GetCharacteristics)(DWORD* pdwCharacteristics);
	STDMETHOD(Pause)();
	STDMETHOD(Shutdown)();
	STDMETHOD(Start)(IMFPresentationDescriptor* pPresentationDescriptor, const GUID* pguidTimeFormat, const PROPVARIANT* pvarStartPosition);
	STDMETHOD(Stop)();

	// IMFMediaSourceEx
	STDMETHOD(GetSourceAttributes)(IMFAttributes** ppAttributes);
	STDMETHOD(GetStreamAttributes)(DWORD dwStreamIdentifier, IMFAttributes** ppAttributes);
	STDMETHOD(SetD3DManager)(IUnknown* pManager);

	// IMFGetService
	STDMETHOD(GetService)(REFGUID guidService, REFIID riid, LPVOID* ppvObject);

	// IMFSampleAllocatorControl
	STDMETHOD(SetDefaultAllocator)(DWORD dwOutputStreamID, IUnknown* pAllocator);
	STDMETHOD(GetAllocatorUsage)(DWORD dwOutputStreamID, DWORD* pdwInputStreamID, MFSampleAllocatorUsage* peUsage);

	// IKsControl
	STDMETHOD_(NTSTATUS, KsProperty)(PKSPROPERTY Property, ULONG PropertyLength, LPVOID PropertyData, ULONG DataLength, ULONG* BytesReturned);
	STDMETHOD_(NTSTATUS, KsMethod)(PKSMETHOD Method, ULONG MethodLength, LPVOID MethodData, ULONG DataLength, ULONG* BytesReturned);
	STDMETHOD_(NTSTATUS, KsEvent)(PKSEVENT Event, ULONG EventLength, LPVOID EventData, ULONG DataLength, ULONG* BytesReturned);

	VCamMediaSource() :	_streams(_numStreams)
	{
		SetBaseAttributesTraceName(L"VCamMediaSourceAtts");
		_rtspManager = &RtspSessionManager::Instance();
		for (auto i = 0; i < _numStreams; i++)
		{
			auto stream = winrt::make_self<MediaStream>();
			_streams[i].attach(stream.detach());
		}
	}

	HRESULT Initialize(IMFAttributes* attributes);
	HRESULT SetupCameraSettings(IMFAttributes* attributes);

private:
	int GetStreamIndexById(DWORD id);
	HRESULT ApplyRuntimeContext();
	CameraSessionConfig _camera_config{};
	const int _numStreams = 1;  // 1 stream for now
	winrt::slim_mutex _lock;
	IRtspSessionManager* _rtspManager = nullptr;

	//wil::com_ptr_nothrow<RtspReaderCallback> _rtspFrameReader;
	winrt::com_array<wil::com_ptr_nothrow<MediaStream>> _streams;
	wil::com_ptr_nothrow<IMFMediaEventQueue> _queue;
	wil::com_ptr_nothrow<IMFPresentationDescriptor> _descriptor;
};
