// C-style exports for the FFmpeg-based preview player, P/Invoked by the C# app
// (RTVirtualCamera/VideoPlayerWrapper.cs). The function names and signatures are
// unchanged from the old Media Foundation VideoPlayer so the managed side did not
// have to change; only the implementation behind them (FfmpegPreviewPlayer) did.
// Compiled native (no /clr) because it pulls in libav via FfmpegPreviewPlayer.
#include <windows.h>
#include "FfmpegPreviewPlayer.h"

extern "C" {

	__declspec(dllexport) FfmpegPreviewPlayer* CreateVideoPlayer()
	{
		return new FfmpegPreviewPlayer();
	}

	__declspec(dllexport) void DestroyVideoPlayer(FfmpegPreviewPlayer* player)
	{
		if (player)
			delete player;
	}

	__declspec(dllexport) void SetVideoPath(FfmpegPreviewPlayer* player, LPWSTR path)
	{
		if (player)
			player->setVideoPath(path);
	}

	__declspec(dllexport) void SetWindowHandle(FfmpegPreviewPlayer* player, HWND hwnd)
	{
		if (player)
			player->setWindowHandle(hwnd);
	}

	__declspec(dllexport) int InitializePlayer(FfmpegPreviewPlayer* player)
	{
		return player ? player->initialize() : -1;
	}

	__declspec(dllexport) int PlayVideo(FfmpegPreviewPlayer* player)
	{
		return player ? player->play() : -1;
	}

	__declspec(dllexport) int PauseVideo(FfmpegPreviewPlayer* player)
	{
		return player ? player->pause() : -1;
	}

	__declspec(dllexport) int StopVideo(FfmpegPreviewPlayer* player)
	{
		return player ? player->stop() : -1;
	}

	__declspec(dllexport) int GetVideoStreamInfo(FfmpegPreviewPlayer* player, StreamInfo* pInfo, UINT32 maxBuffElement, UINT32* count)
	{
		return player ? player->getVideoStreamInfo(pInfo, maxBuffElement, count) : -1;
	}

	__declspec(dllexport) int GetPreviewStats(FfmpegPreviewPlayer* player, PreviewStats* stats)
	{
		return player ? player->getPreviewStats(stats) : -1;
	}

	__declspec(dllexport) int GetConnectionInfo(FfmpegPreviewPlayer* player, ConnectionInfo* info)
	{
		return player ? player->getConnectionInfo(info) : -1;
	}

	__declspec(dllexport) void UpdateVideoPosition(FfmpegPreviewPlayer* player)
	{
		if (player)
			player->updateVideoPosition();
	}

	__declspec(dllexport) bool IsPlaying(FfmpegPreviewPlayer* player)
	{
		return player ? player->getIsPlaying() : false;
	}

	__declspec(dllexport) bool IsPaused(FfmpegPreviewPlayer* player)
	{
		return player ? player->getIsPaused() : false;
	}

	// Live RTSP transport carrying the preview: 0 = none, 1 = UDP, 2 = TCP.
	__declspec(dllexport) int GetActiveTransport(FfmpegPreviewPlayer* player)
	{
		return player ? player->getActiveTransport() : 0;
	}

	// Measured received video bitrate in bits/s (0 until measured / disconnected).
	__declspec(dllexport) long long GetPreviewBitrate(FfmpegPreviewPlayer* player)
	{
		return player ? player->getBitrateBps() : 0;
	}

} // extern "C"
