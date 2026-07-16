using System;
using System.Runtime.InteropServices;

namespace RTVirtualCamera
{
    /// <summary>
    /// Mirror of Shared/VCamConfig.h — must match byte-for-byte (x64, no packing surprises).
    /// Filled by the C# app after probing the RTSP source and passed to the Frame Server
    /// via IMFVirtualCamera attributes before Start().
    /// </summary>
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct VCamConfig
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string RtspUrl;   // RTSP source URL

        public uint Width;       // video width  in pixels (0 = use RTSP native)
        public uint Height;      // video height in pixels (0 = use RTSP native)
        public uint FpsNum;      // frame-rate numerator   (0 = default 30)
        public uint FpsDen;      // frame-rate denominator (0 = default 1)
        public Guid Format;      // preferred output subtype (Guid.Empty = NV12 auto)
        public uint Overlay;     // diagnostic frame-counter overlay: 0 = off, 1 = on
    }

    /// <summary>Session state carried over the wire by Shared/VCamStats.h::VCamSessionStateWire.
    /// With the single FFmpeg engine the Frame Server reports Running/Starting based on the
    /// app producer's heartbeat; the reconnect states are legacy but kept for wire stability.</summary>
    public enum FrameServerSessionState : uint
    {
        Stopped = 0,
        Starting = 1,
        Running = 2,
        Reconnecting = 3,
        Failed = 4,
    }

    /// <summary>
    /// Mirror of Shared/VCamStats.h::VCamFrameServerStats (byte-for-byte, Pack=1 to match
    /// the C++ #pragma pack(push,1)). Published ~30x/sec by the Frame Server process
    /// (VirtualCamera.dll, running in svchost.exe) into cross-session shared memory and
    /// read here via RTCamNative's VCam_GetFrameServerStats — see StatsReader.cpp / the
    /// "Live stats (Frame Server -> app)" section in CLAUDE.md.
    /// </summary>
    [StructLayout(LayoutKind.Sequential, Pack = 1)]
    public struct FrameServerStats
    {
        public uint structVersion;
        public int sequence;         // seqlock counter; not meaningful once read into managed memory
        public ulong updatedTickMs;  // GetTickCount64() in the Frame Server process at last publish
        public uint sessionState;    // FrameServerSessionState
        public ulong rxFrames;       // cumulative frames decoded from RTSP
        public ulong renderedFrames; // cumulative frames actually delivered to the consumer (every RequestSample
                                      // once the first frame has arrived, including re-serves of the same frame)
        public ulong droppedFrames;  // cumulative frames replaced before a consumer picked them up
        public ulong declinedFrames; // of renderedFrames, how many re-served the same RTSP frame as last time
                                      // (still delivered — this is a count only, not an actual decline; see
                                      // CLAUDE.md "renderedFrames vs declinedFrames vs rxFrames")
        public double driftMs;       // wall - media elapsed since first frame of the session
        public double lastCopyMs;    // cost of the last frame copy (GPU or CPU path)
        public uint width;
        public uint height;
        public uint fpsNum;
        public uint fpsDen;
        public uint hwAccelCapable;  // 1 = a D3D11 device manager was wired up (GPU path can engage)
        public uint hwAccelActive;   // 1 = the last copy actually used the GPU zero-copy path
    }

    /// <summary>Live Frame-Server rates derived from two FrameServerStats snapshots over wall-clock.</summary>
    public struct FrameServerRates
    {
        public bool Available;       // false if no Frame Server session has ever published stats
        public bool Stale;           // true if updatedTickMs stopped advancing (writer likely gone)
        public FrameServerSessionState SessionState;
        public double RxFps;
        public double RenderedFps;
        public double DroppedFps;
        public double DeclinedFps;   // of RenderedFps, how many were a re-serve of the same RTSP frame (count only)
        public double DriftMs;
        public double LastCopyMs;
        public uint Width;
        public uint Height;
        public uint FpsNum;
        public uint FpsDen;
        public bool HwAccelCapable;
        public bool HwAccelActive;
    }

    public class VirtualCameraWrapper : IDisposable
    {
        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr CreateVirtualCamera();

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void DestroyVirtualCamera(IntPtr vcam);

        [DllImport("kernel32.dll")]
        private static extern uint GetLastError();

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private static extern void SetVirtualCameraName(IntPtr vcam, string name);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void SetVirtualCameraConfig(IntPtr vcam, ref VCamConfig config);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int RegisterVCam(IntPtr vcam);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int StartVCam(IntPtr vcam);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int StopVCam(IntPtr vcam);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int UnregisterVCam(IntPtr vcam);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern bool IsVCamRegistered(IntPtr vcam);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern bool IsVCamStarted(IntPtr vcam);

        // Not tied to a specific vcamHandle: this reads a single, global shared-memory
        // channel published by whichever Frame Server session is currently running
        // (see StatsReader.cpp). Returns 0 on success, -1 if nothing has been published
        // yet (camera never started, or no consumer ever opened it).
        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int VCam_GetFrameServerStats(out FrameServerStats stats);

        // FFmpeg user-space producer (the only receive path): RTCamNative decodes the
        // RTSP stream in-process and pushes NV12 frames to the Frame Server via the
        // frame shared memory. See FfmpegExports.cpp.
        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private static extern int VCam_StartFfmpegProducer(string url, uint width, uint height, uint fpsNum, uint fpsDen);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int VCam_StopFfmpegProducer();

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int VCam_GetFfmpegProducerFrames(out ulong frames);

        [DllImport("RTCamNative.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int VCam_IsFfmpegProducerHardware();

        private IntPtr vcamHandle;
        private bool disposed = false;

        // Previous Frame-Server snapshot + timestamp, for deriving live fps from
        // counter deltas — same approach as VideoPlayerWrapper.TryGetRates.
        private FrameServerStats prevFrameServerStats;
        private long prevFrameServerStatsTicks = 0;
        private bool hasPrevFrameServerStats = false;

        public uint LastWin32Error { get; private set; }

        public VirtualCameraWrapper()
        {
            vcamHandle = CreateVirtualCamera();
            if (vcamHandle == IntPtr.Zero)
                throw new InvalidOperationException("Failed to create virtual camera instance");
        }

        ~VirtualCameraWrapper() { Dispose(false); }

        public void SetCameraName(string name)
        {
            if (disposed) throw new ObjectDisposedException(nameof(VirtualCameraWrapper));
            if (vcamHandle != IntPtr.Zero && !string.IsNullOrEmpty(name))
                SetVirtualCameraName(vcamHandle, name);
        }

        /// <summary>
        /// Sets the full camera configuration (URL, resolution, fps, format).
        /// Must be called before Register().
        /// </summary>
        public void SetConfig(VCamConfig config)
        {
            if (disposed) throw new ObjectDisposedException(nameof(VirtualCameraWrapper));
            if (vcamHandle != IntPtr.Zero)
                SetVirtualCameraConfig(vcamHandle, ref config);
        }

        public bool Register()
        {
            if (disposed) throw new ObjectDisposedException(nameof(VirtualCameraWrapper));
            if (vcamHandle == IntPtr.Zero) return false;
            int result = RegisterVCam(vcamHandle);
            LastWin32Error = GetLastError();
            return result == 0;
        }

        public bool Start()
        {
            if (disposed) throw new ObjectDisposedException(nameof(VirtualCameraWrapper));
            if (vcamHandle == IntPtr.Zero) return false;
            int result = StartVCam(vcamHandle);
            LastWin32Error = GetLastError();
            return result == 0;
        }

        public bool Stop()
        {
            if (disposed) throw new ObjectDisposedException(nameof(VirtualCameraWrapper));
            if (vcamHandle == IntPtr.Zero) return false;
            int result = StopVCam(vcamHandle);
            LastWin32Error = GetLastError();
            return result == 0;
        }

        public bool Unregister()
        {
            if (disposed) throw new ObjectDisposedException(nameof(VirtualCameraWrapper));
            if (vcamHandle == IntPtr.Zero) return false;
            int result = UnregisterVCam(vcamHandle);
            LastWin32Error = GetLastError();
            return result == 0;
        }

        /// <summary>
        /// Starts the user-space FFmpeg producer: the app decodes the RTSP stream and
        /// streams NV12 frames to the Frame Server via shared memory. Call after Start()
        /// succeeds. width/height must match the VCamConfig sent.
        /// </summary>
        public bool StartFfmpegProducer(string url, uint width, uint height, uint fpsNum, uint fpsDen)
        {
            if (disposed) throw new ObjectDisposedException(nameof(VirtualCameraWrapper));
            return VCam_StartFfmpegProducer(url, width, height, fpsNum, fpsDen) == 0;
        }

        /// <summary>Stops the user-space FFmpeg producer (no-op if it was never started).</summary>
        public void StopFfmpegProducer()
        {
            if (disposed) return;
            VCam_StopFfmpegProducer();
        }

        /// <summary>True if the FFmpeg producer's last frame was hardware-decoded (d3d11va).</summary>
        public bool IsFfmpegProducerHardware()
        {
            if (disposed) return false;
            return VCam_IsFfmpegProducerHardware() == 1;
        }

        public bool IsRegistered
        {
            get { return !disposed && vcamHandle != IntPtr.Zero && IsVCamRegistered(vcamHandle); }
        }

        public bool IsStarted
        {
            get { return !disposed && vcamHandle != IntPtr.Zero && IsVCamStarted(vcamHandle); }
        }

        // Live Frame-Server rates: call on a UI timer (see MainForm.StatsTimer_Tick).
        // fps are derived from counter deltas since the previous call, so the cadence
        // of your timer sets the averaging window. "Stale" is set once updatedTickMs
        // stops advancing for longer than a couple of polling intervals, which
        // distinguishes "camera stopped" from "app briefly missed a poll".
        private const ulong StaleAfterMs = 3000;

        public bool TryGetFrameServerRates(out FrameServerRates rates)
        {
            rates = default(FrameServerRates);

            FrameServerStats now;
            if (disposed || VCam_GetFrameServerStats(out now) != 0)
            {
                hasPrevFrameServerStats = false;
                return false;
            }

            rates.Available = true;
            rates.SessionState = (FrameServerSessionState)now.sessionState;
            rates.DriftMs = now.driftMs;
            rates.LastCopyMs = now.lastCopyMs;
            rates.Width = now.width;
            rates.Height = now.height;
            rates.FpsNum = now.fpsNum;
            rates.FpsDen = now.fpsDen;
            rates.HwAccelCapable = now.hwAccelCapable != 0;
            rates.HwAccelActive = now.hwAccelActive != 0;

            ulong nowTickMs64 = (ulong)Environment.TickCount64;
            rates.Stale = nowTickMs64 > now.updatedTickMs && (nowTickMs64 - now.updatedTickMs) > StaleAfterMs;

            long nowTicks = DateTime.UtcNow.Ticks;
            if (hasPrevFrameServerStats)
            {
                double secs = (nowTicks - prevFrameServerStatsTicks) / (double)TimeSpan.TicksPerSecond;
                if (secs > 0.0)
                {
                    rates.RxFps = (now.rxFrames - prevFrameServerStats.rxFrames) / secs;
                    rates.RenderedFps = (now.renderedFrames - prevFrameServerStats.renderedFrames) / secs;
                    rates.DroppedFps = (now.droppedFrames - prevFrameServerStats.droppedFrames) / secs;
                    rates.DeclinedFps = (now.declinedFrames - prevFrameServerStats.declinedFrames) / secs;
                }
            }

            prevFrameServerStats = now;
            prevFrameServerStatsTicks = nowTicks;
            hasPrevFrameServerStats = true;
            return true;
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
                // Stop the user-space producer first (if the FFmpeg engine was running)
                // so its decode thread and RTSP connection are torn down cleanly.
                try { VCam_StopFfmpegProducer(); } catch { /* native may already be unloaded */ }

                if (vcamHandle != IntPtr.Zero)
                {
                    if (IsStarted) StopVCam(vcamHandle);
                    if (IsRegistered) UnregisterVCam(vcamHandle);
                    DestroyVirtualCamera(vcamHandle);
                    vcamHandle = IntPtr.Zero;
                }
                disposed = true;
            }
        }
    }
}
