#include "pch.h"
#include "RtspReaderCallback.h"

#pragma comment(lib, "mfuuid.lib")   // IID_IMFSourceReaderCallback

// IUnknown
STDMETHODIMP RtspReaderCallback::QueryInterface(REFIID riid, void** ppv)
{
	if (!ppv) return E_POINTER;
	if (riid == IID_IUnknown || riid == IID_IMFSourceReaderCallback)
	{
		*ppv = static_cast<IMFSourceReaderCallback*>(this);
		AddRef();
		return S_OK;
	}
	*ppv = nullptr;
	return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) RtspReaderCallback::AddRef()
{
	return InterlockedIncrement(&_cRef);
}

STDMETHODIMP_(ULONG) RtspReaderCallback::Release()
{
	ULONG count = InterlockedDecrement(&_cRef);
	if (count == 0) delete this;
	return count;
}
HRESULT RtspReaderCallback::BeginRead(IMFSourceReader* reader)
{
	HRESULT hr;

	RETURN_HR_IF_NULL(E_POINTER, reader);
	{
		winrt::slim_lock_guard lock(_lock);
		RETURN_HR_IF(MF_E_SHUTDOWN, _shutdown);
		_reader = reader;
	}

	// Fire the first async ReadSample to start the chain.
	WINTRACE(L"RtspReaderCallback::BeginRead - starting async read chain");
	hr = reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);
	RETURN_IF_FAILED(hr);

	return S_OK;
}

STDMETHODIMP RtspReaderCallback::OnReadSample(HRESULT hrStatus, DWORD dwStreamIndex, DWORD dwStreamFlags, LONGLONG llTimestamp, IMFSample* pSample)
{
	// Grab reader reference under the lock so Shutdown() can't tear it out mid-flight.
	wil::com_ptr_nothrow<IMFSourceReader> reader;
	bool broken = false;
	{
		winrt::slim_lock_guard lock(_lock);
		if (_shutdown || !_reader)
			return S_OK;

		reader = _reader;

		if (SUCCEEDED(hrStatus) && pSample && !(dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM))
		{
			// Replace previous frame (we only keep the latest).
			_latestSample = pSample;
			//WINTRACE(L"RtspReaderCallback::OnReadSample - frame stored ts:%lld", llTimestamp);
		}
		else if (FAILED(hrStatus))
		{
			WINTRACE(L"RtspReaderCallback::OnReadSample - error 0x%08X, stream broken", hrStatus);
			broken = true; // network drop / camera gone -> trigger reconnection
		}
		else if (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM)
		{
			WINTRACE(L"RtspReaderCallback::OnReadSample - end of stream, stream broken");
			broken = true; // RTSP source ended -> trigger reconnection
		}
	}

	if (broken)
	{
		// Notify the session manager exactly once, OUTSIDE our _lock to avoid a lock
		// inversion (manager->_lock is taken before callback->_lock elsewhere). Stop
		// re-issuing reads; the manager will rebuild the reader on its reconnect thread.
		if (!_brokenSignaled.exchange(true) && _onBroken)
			_onBroken();
		return S_OK;
	}

	// Re-issue ReadSample immediately to keep the async chain running.
	reader->ReadSample( MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, nullptr, nullptr, nullptr);

	return S_OK;
}

HRESULT RtspReaderCallback::SetOutputMediaType(const GUID& subtype, UINT32 width, UINT32 height)
{
	wil::com_ptr_nothrow<IMFMediaType> outputType;
	winrt::slim_lock_guard lock(_lock);

	if (_shutdown || !_reader)
		return MF_E_SHUTDOWN;

	
	RETURN_IF_FAILED(MFCreateMediaType(&outputType));
	RETURN_IF_FAILED(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_IF_FAILED(outputType->SetGUID(MF_MT_SUBTYPE, subtype));
	MFSetAttributeSize(outputType.get(), MF_MT_FRAME_SIZE, width, height);

	HRESULT hr = _reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, outputType.get());
	if (FAILED(hr))
		WINTRACE(L"RtspReaderCallback::SetOutputMediaType - failed 0x%08X (continuing with previous type)", hr);
	else
		WINTRACE(L"RtspReaderCallback::SetOutputMediaType - reconfigured to %ux%u", width, height);
	return hr;
}

HRESULT RtspReaderCallback::TakeLatestSample(IMFSample** ppSample)
{
	RETURN_HR_IF_NULL(E_POINTER, ppSample);

	*ppSample = nullptr;

	winrt::slim_lock_guard lock(_lock);

	if (!_latestSample)
		return S_FALSE;

	// Copy (AddRef) instead of detach: we keep the last decoded frame so that
	// RequestSample can always deliver something at 30fps even when the RTSP
	// source runs at a lower rate. _latestSample is replaced only when
	// OnReadSample stores a newer frame.
	_latestSample.copy_to(ppSample);

	return S_OK;
}

void RtspReaderCallback::Shutdown()
{
	winrt::slim_lock_guard lock(_lock);
	_shutdown = true;
	_latestSample.reset();
	_reader.reset();

	WINTRACE(L"RtspReaderCallback::Shutdown");
}
