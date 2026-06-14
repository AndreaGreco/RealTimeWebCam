// RTCamNative.cpp : DLL entry point and C-style interface for VideoPlayer
#include "pch.h"
#include "VideoPlayer.h"
#include <windows.h>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfplay.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "Strmiids")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "Dxva2.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "mfsensorgroup.lib")

// ============================================================================
// C-style interface for C# interop
// ============================================================================

extern "C" {
	__declspec(dllexport) VideoPlayer* CreateVideoPlayer()
	{
		return new VideoPlayer();
	}

	__declspec(dllexport) void DestroyVideoPlayer(VideoPlayer* player)
	{
		if (player)
		{
			delete player;
		}
	}

	__declspec(dllexport) void SetVideoPath(VideoPlayer* player, LPWSTR path)
	{
		if (player)
		{
			player->setVideoPath(path);
		}
	}

	__declspec(dllexport) void SetVideoCaptureDeviceName(VideoPlayer* player, LPCWSTR name)
	{
		if (player)
		{
			player->setCaptureDeviceName(name);
		}
	}

	__declspec(dllexport) void SetWindowHandle(VideoPlayer* player, HWND hwnd)
	{
		if (player)
		{
			player->setWindowHandle(hwnd);
		}
	}

	__declspec(dllexport) int InitializePlayer(VideoPlayer* player)
	{
		if (player)
		{
			return player->initialize();
		}
		return -1;
	}

	__declspec(dllexport) int PlayVideo(VideoPlayer* player)
	{
		if (player)
		{
			return player->play();
		}
		return -1;
	}

	__declspec(dllexport) int PauseVideo(VideoPlayer* player)
	{
		if (player)
		{
			return player->pause();
		}
		return -1;
	}

	__declspec(dllexport) int StopVideo(VideoPlayer* player)
	{
		if (player)
		{
			return player->stop();
		}
		return -1;
	}

	__declspec(dllexport) int GetVideoStreamInfo(VideoPlayer* player, StreamInfo* pInfo, UINT32 maxBuffElement, UINT32* count)
	{
		if (player)
		{
			return player->getVideoStreamInfo(pInfo, maxBuffElement, count);
		}
		return -1;
	}

	__declspec(dllexport) int GetPreviewStats(VideoPlayer* player, PreviewStats* stats)
	{
		if (player)
		{
			return player->getPreviewStats(stats);
		}
		return -1;
	}

	__declspec(dllexport) void UpdateVideoPosition(VideoPlayer* player)
	{
		if (player)
		{
			player->updateVideoPosition();
		}
	}

	__declspec(dllexport) bool IsPlaying(VideoPlayer* player)
	{
		if (player)
		{
			return player->getIsPlaying();
		}
		return false;
	}

	__declspec(dllexport) bool IsPaused(VideoPlayer* player)
	{
		if (player)
		{
			return player->getIsPaused();
		}
		return false;
	}

	__declspec(dllexport) void ClearVideoCaptureDeviceName(VideoPlayer* player)
	{
		if (player)
		{
			player->setCaptureDeviceName(nullptr);
		}
	}
}

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
