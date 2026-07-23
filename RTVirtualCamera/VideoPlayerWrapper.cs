using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    [StructLayout(LayoutKind.Sequential)]
    public struct StreamInfo
    {
        public uint width;
        public uint height;

        public Guid subtype;

        public uint fpsNum;
        public uint fpsDen;

        public uint bitrate;

        public int getFSP()
        {
            if (fpsDen == 0)
                return 0;

            return (int)(fpsNum / fpsDen);
        }
    }

    // Mirror of RTCamNative/VideoReaderCbk.h::PreviewStats (byte-for-byte).
    [StructLayout(LayoutKind.Sequential)]
    public struct PreviewStats
    {
        public ulong receivedFrames; // delivered by the source reader
        public ulong renderedFrames; // sent to the EVR
        public ulong droppedFrames;  // skipped by the adaptive drop
        public double driftMs;       // wall - media elapsed since first frame; rising => accumulating delay
        public double lastRenderMs;  // last CPU->GPU copy + ProcessSample cost
    }

    // Mirror of RTCamNative/FfmpegPreviewPlayer.h::ConnectionInfo (byte-for-byte).
    // CharSet.Ansi: the char[] buffers hold NUL-terminated ASCII/UTF-8 straight from
    // libav. Int64 first so the following uint fields stay naturally aligned, matching
    // the native struct's default packing.
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    public struct ConnectionInfo
    {
        public long bitrate;   // bits/s reported by libav (0 = unknown)
        public uint width;
        public uint height;
        public uint fpsNum;
        public uint fpsDen;
        public int valid;      // 1 = populated (source was probed)

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string container;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 16)]
        public string transport;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)]
        public string videoCodec;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 48)]
        public string profile;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 24)]
        public string pixelFormat;
    }

    // Live preview rates derived from two PreviewStats snapshots over wall-clock.
    public struct PreviewRates
    {
        public double ReceivedFps;
        public double RenderedFps;
        public double DroppedFps;
        public double DriftMs;
        public double LastRenderMs;
    }

    public class VideoPlayerWrapper : IDisposable
    {
        private IntPtr playerInstance;
        private bool disposed = false;

        // Previous snapshot + timestamp for computing live fps from counter deltas.
        private PreviewStats prevStats;
        private long prevStatsTicks = 0;
        private bool hasPrevStats = false;

        // Import delle funzioni dalla DLL C++
        [DllImport("RTCamNative.dll")]
        private static extern IntPtr CreateVideoPlayer();

        [DllImport("RTCamNative.dll")]
        private static extern void DestroyVideoPlayer(IntPtr player);

        [DllImport("kernel32.dll")]
        private static extern uint GetLastError();

        [DllImport("RTCamNative.dll", CharSet = CharSet.Unicode)]
        private static extern void SetVideoPath(IntPtr player, string path);

        [DllImport("RTCamNative.dll")]
        private static extern int GetVideoStreamInfo(IntPtr player,
            [Out] StreamInfo[] infos,
            uint maxCount,
            out uint writtenCount);

        [DllImport("RTCamNative.dll")]
        private static extern int GetPreviewStats(IntPtr player, out PreviewStats stats);

        [DllImport("RTCamNative.dll")]
        private static extern int GetConnectionInfo(IntPtr player, out ConnectionInfo info);

        [DllImport("RTCamNative.dll")]
        private static extern void SetWindowHandle(IntPtr player, IntPtr hwnd);

        [DllImport("RTCamNative.dll")]
        private static extern int InitializePlayer(IntPtr player);

        [DllImport("RTCamNative.dll")]
        private static extern int PlayVideo(IntPtr player);

        [DllImport("RTCamNative.dll")]
        private static extern int PauseVideo(IntPtr player);

        [DllImport("RTCamNative.dll")]
        private static extern int StopVideo(IntPtr player);

        [DllImport("RTCamNative.dll")]
        private static extern bool IsPlaying(IntPtr player);

        [DllImport("RTCamNative.dll")]
        private static extern bool IsPaused(IntPtr player);

        // Live RTSP transport carrying the preview's frames: 0 none, 1 UDP, 2 TCP.
        [DllImport("RTCamNative.dll")]
        private static extern int GetActiveTransport(IntPtr player);

        // Measured received video bitrate in bits/s (0 until measured / disconnected).
        [DllImport("RTCamNative.dll")]
        private static extern long GetPreviewBitrate(IntPtr player);

        public uint LastWin32Error { get; private set; }

        public VideoPlayerWrapper()
        {
            playerInstance = CreateVideoPlayer();
            if (playerInstance == IntPtr.Zero)
            {
                throw new InvalidOperationException("Failed to create VideoPlayer instance");
            }
        }

        public void SetVideoPath(string videoPath)
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            SetVideoPath(playerInstance, videoPath);
        }

        public StreamInfo[] GetStreamInfos()
        {
            StreamInfo[] infos = new StreamInfo[8];
            StreamInfo[] outs;
            uint written;
            GetVideoStreamInfo(playerInstance, infos, 8, out written);

            outs = new StreamInfo[written];
            for (int i = 0; i < written; i++)
            {
                outs[i] = infos[i];
            }

            return outs;
        }

        // Connection characteristics (container/transport/codec/…) cached by the
        // native player at Initialize(). valid==0 until the source has been probed.
        public bool TryGetConnectionInfo(out ConnectionInfo info)
        {
            info = default(ConnectionInfo);
            if (disposed)
                return false;

            return GetConnectionInfo(playerInstance, out info) == 0 && info.valid != 0;
        }

        // Raw counters snapshot from the native preview path.
        public bool TryGetStats(out PreviewStats stats)
        {
            stats = default(PreviewStats);
            if (disposed)
                return false;

            return GetPreviewStats(playerInstance, out stats) == 0;
        }

        // Live rates: call on a UI timer. fps are derived from counter deltas
        // since the previous call, so the cadence of your timer sets the window.
        public bool TryGetRates(out PreviewRates rates)
        {
            rates = default(PreviewRates);

            PreviewStats now;
            if (!TryGetStats(out now))
            {
                hasPrevStats = false;
                return false;
            }

            long nowTicks = DateTime.UtcNow.Ticks;
            rates.DriftMs = now.driftMs;
            rates.LastRenderMs = now.lastRenderMs;

            if (hasPrevStats)
            {
                double secs = (nowTicks - prevStatsTicks) / (double)TimeSpan.TicksPerSecond;
                if (secs > 0.0)
                {
                    rates.ReceivedFps = (now.receivedFrames - prevStats.receivedFrames) / secs;
                    rates.RenderedFps = (now.renderedFrames - prevStats.renderedFrames) / secs;
                    rates.DroppedFps = (now.droppedFrames - prevStats.droppedFrames) / secs;
                }
            }

            prevStats = now;
            prevStatsTicks = nowTicks;
            hasPrevStats = true;
            return true;
        }

        /// <summary>Live transport carrying the preview's frames ("UDP"/"TCP"), or null.</summary>
        public string GetActiveTransportLabel()
        {
            if (disposed)
                return null;

            return VirtualCameraWrapper.TransportLabel(GetActiveTransport(playerInstance));
        }

        /// <summary>Measured received video bitrate in bits/s (0 if unknown/disconnected).</summary>
        public long GetBitrateBps()
        {
            if (disposed)
                return 0;

            return GetPreviewBitrate(playerInstance);
        }

        public void SetWindowHandle(IntPtr windowHandle)
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            SetWindowHandle(playerInstance, windowHandle);
        }

        public void SetWindowHandle(Control control)
        {
            SetWindowHandle(control.Handle);
        }

        public bool Initialize()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            int result = InitializePlayer(playerInstance);
            LastWin32Error = GetLastError();
            return result == 0;
        }

        public bool Play()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            int result = PlayVideo(playerInstance);
            LastWin32Error = GetLastError();
            return result == 0;
        }

        public bool Pause()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            int result = PauseVideo(playerInstance);
            LastWin32Error = GetLastError();
            return result == 0;
        }

        public bool Stop()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            int result = StopVideo(playerInstance);
            LastWin32Error = GetLastError();
            return result == 0;
        }

        public bool IsCurrentlyPlaying
        {
            get
            {
                if (disposed)
                    return false;

                return IsPlaying(playerInstance);
            }
        }

        public bool IsCurrentlyPaused
        {
            get
            {
                if (disposed)
                    return false;

                return IsPaused(playerInstance);
            }
        }

        // Expose the native handle for sharing with VirtualCamera
        public IntPtr NativeHandle
        {
            get
            {
                if (disposed)
                    throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

                return playerInstance;
            }
        }

        public void Dispose()
        {
            Dispose(true);
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!disposed)
            {
                if (playerInstance != IntPtr.Zero)
                {
                    DestroyVideoPlayer(playerInstance);
                    playerInstance = IntPtr.Zero;
                }
                disposed = true;
            }
        }

        ~VideoPlayerWrapper()
        {
            Dispose(false);
        }
    }
}