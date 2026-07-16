// RTCamNative.cpp : DLL entry point.
//
// The preview player's C exports live in PreviewExports.cpp and the FFmpeg
// producer's in FfmpegExports.cpp (both native TUs — they pull in libav, which
// does not mix with this project's /clr compilation). This file keeps only the
// DLL entry point and the Media Foundation link pragmas the virtual-camera side
// (VirtualCamera.cpp) needs.
#include "pch.h"
#include <windows.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Strmiids")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "mfsensorgroup.lib")

// ============================================================================
// DLL Entry Point
// ============================================================================
#pragma unmanaged

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
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
