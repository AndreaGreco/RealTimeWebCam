#pragma once
#include "pch.h"
#include "RtspReaderCallback.h"
#include "..\Shared\VCamConfig.h"
#include <memory>
#include <vector>

struct CameraSessionConfig
{
	bool valid = false;
	wchar_t rtspUrl[512] = {};
	UINT32 width = 0;
	UINT32 height = 0;
	UINT32 fpsNum = 30;
	UINT32 fpsDen = 1;
	GUID format = MFVideoFormat_NV12;
	UINT64 generation = 0;
};

struct RtspFrameSnapshot
{
	bool valid = false;
	UINT32 stride = 0;
	LONGLONG sampleTime = 0;
	LONGLONG sampleDuration = 0;
	UINT64 generation = 0;
	std::shared_ptr<std::vector<BYTE>> bytes;
	DWORD dataSize = 0;
};

enum class RtspSessionState
{
	Stopped,
	Starting,
	Running,
	Failed,
};

struct IRtspSessionManager
{
	virtual ~IRtspSessionManager() = default;
	virtual HRESULT Start(const CameraSessionConfig& config) = 0;
	virtual HRESULT Stop(UINT64 generation) = 0;
	virtual bool IsRunning() const = 0;
	virtual UINT64 GetGeneration() const = 0;
	virtual RtspSessionState GetState() const = 0;
	virtual HRESULT TryGetLatestFrame(RtspFrameSnapshot& frame) = 0;
};

struct StreamRuntimeContext
{
	CameraSessionConfig config;
	IRtspSessionManager* rtspManager = nullptr;
};

class RtspSessionManager final : public IRtspSessionManager
{
public:
	static RtspSessionManager& Instance();

	HRESULT Start(const CameraSessionConfig& config) override;
	HRESULT Stop(UINT64 generation) override;
	bool IsRunning() const override;
	UINT64 GetGeneration() const override;
	RtspSessionState GetState() const override;
	HRESULT TryGetLatestFrame(RtspFrameSnapshot& frame) override;

private:
	RtspSessionManager() = default;
	HRESULT BuildRequestedType(const CameraSessionConfig& config, IMFMediaType** ppType);
	void StopNoLock();

	mutable winrt::slim_mutex _lock;
	CameraSessionConfig _config{};
	GUID _actualSubtype = MFVideoFormat_NV12;
	UINT32 _actualWidth = 0;
	UINT32 _actualHeight = 0;
	UINT32 _actualStride = 0;
	RtspSessionState _state = RtspSessionState::Stopped;
	bool _running = false;
	wil::com_ptr_nothrow<IMFSourceReader> _reader;
	wil::com_ptr_nothrow<RtspReaderCallback> _callback;
};
