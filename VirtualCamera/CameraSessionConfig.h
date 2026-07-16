#pragma once
#include <mfapi.h>

// Runtime configuration the app sends (via IMFVirtualCamera attributes, read in
// VCamMediaSource::SetupCameraSettings) and the Frame Server applies to its
// streams. There is a single receive engine now: the app (RTCamNative) decodes
// the RTSP stream with FFmpeg and pushes NV12 frames through the frame shared
// memory (Shared/VCamFrameChannel.h); the Frame Server reads them in
// MediaStream::RequestSample. So this block carries only geometry/identity — no
// in-process RTSP reader exists anymore (the old Media Foundation engine and its
// RtspSessionManager were removed).
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
	UINT32 overlay = 0; // diagnostic frame-counter overlay (0 = off, 1 = on)
};

// Passed from VCamMediaSource to each MediaStream so a stream can (re)build its
// descriptor from the current config.
struct StreamRuntimeContext
{
	CameraSessionConfig config;
};
