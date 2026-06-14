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

        private IntPtr vcamHandle;
        private bool disposed = false;

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

        public bool IsRegistered
        {
            get { return !disposed && vcamHandle != IntPtr.Zero && IsVCamRegistered(vcamHandle); }
        }

        public bool IsStarted
        {
            get { return !disposed && vcamHandle != IntPtr.Zero && IsVCamStarted(vcamHandle); }
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
