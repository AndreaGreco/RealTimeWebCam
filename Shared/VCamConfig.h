#pragma once
#include <guiddef.h>

// ─────────────────────────────────────────────────────────────────────────────
// MF attribute GUIDs used to pass VCamConfig fields from the app to the Frame Server.
// Set via IMFVirtualCamera::SetString / SetUINT32 BEFORE Start(); read in
// MediaSource::Initialize() inside VCamSampleSource.dll.
// Must be identical in RTCamNative/ and VirtualCamera/.
//
// SetString/SetUINT32 on IMFVirtualCamera are forwarded to Initialize() by the
// Frame Server; SetBlob is NOT reliably forwarded — hence individual attributes.
// ─────────────────────────────────────────────────────────────────────────────

// {A3C1E7B2-9F4D-4E82-B061-F5A208D36C10}  RTSP source URL (wstring)
static const GUID MF_VCAM_RTSP_URL =
	{ 0xa3c1e7b2, 0x9f4d, 0x4e82, { 0xb0, 0x61, 0xf5, 0xa2, 0x08, 0xd3, 0x6c, 0x10 } };

// {A3C1E7B3-9F4D-4E82-B061-F5A208D36C10}  video width  in pixels (UINT32, 0 = native)
static const GUID MF_VCAM_WIDTH =
	{ 0xa3c1e7b3, 0x9f4d, 0x4e82, { 0xb0, 0x61, 0xf5, 0xa2, 0x08, 0xd3, 0x6c, 0x10 } };

// {A3C1E7B4-9F4D-4E82-B061-F5A208D36C10}  video height in pixels (UINT32, 0 = native)
static const GUID MF_VCAM_HEIGHT =
	{ 0xa3c1e7b4, 0x9f4d, 0x4e82, { 0xb0, 0x61, 0xf5, 0xa2, 0x08, 0xd3, 0x6c, 0x10 } };

// {A3C1E7B5-9F4D-4E82-B061-F5A208D36C10}  fps numerator   (UINT32, 0 = default 30)
static const GUID MF_VCAM_FPS_NUM =
	{ 0xa3c1e7b5, 0x9f4d, 0x4e82, { 0xb0, 0x61, 0xf5, 0xa2, 0x08, 0xd3, 0x6c, 0x10 } };

// {A3C1E7B6-9F4D-4E82-B061-F5A208D36C10}  fps denominator (UINT32, 0 = default 1)
static const GUID MF_VCAM_FPS_DEN =
	{ 0xa3c1e7b6, 0x9f4d, 0x4e82, { 0xb0, 0x61, 0xf5, 0xa2, 0x08, 0xd3, 0x6c, 0x10 } };

// ─────────────────────────────────────────────────────────────────────────────
// Configuration block — used internally in C++ only (not sent as a blob).
// The C# mirror VCamConfig in VirtualCameraWrapper.cs carries the same fields;
// the C# side fills it from the probed StreamInfo and the C++ SetConfig()
// function extracts the individual fields for the MF attribute calls above.
//
// There is a single receive path: the app (RTCamNative) decodes the RTSP stream
// with FFmpeg (user-space) and pushes NV12 frames through the frame shared memory
// (Shared/VCamFrameChannel.h); the Frame Server reads those. So there is no engine
// selector — the Frame Server never opens the RTSP source itself.
// ─────────────────────────────────────────────────────────────────────────────
struct VCamConfig
{
	wchar_t  rtspUrl[512]; // RTSP source URL, null-terminated wide string
	UINT32   width;        // video width  in pixels  (0 = use RTSP native)
	UINT32   height;       // video height in pixels  (0 = use RTSP native)
	UINT32   fpsNum;       // frame-rate numerator    (e.g. 30); 0 = default 30
	UINT32   fpsDen;       // frame-rate denominator  (e.g. 1);  0 = default 1
	GUID     format;       // preferred output subtype (GUID_NULL = NV12 auto)
};
