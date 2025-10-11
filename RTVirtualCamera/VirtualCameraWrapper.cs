using System;
using System.Runtime.InteropServices;

namespace TestVideo
{
    public class VirtualCameraWrapper : IDisposable
    {
        // P/Invoke declarations for VirtualCamera functions
        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr CreateVirtualCamera();

        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern void DestroyVirtualCamera(IntPtr vcam);

        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        private static extern void SetVirtualCameraName(IntPtr vcam, string name);

        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int RegisterVCam(IntPtr vcam);

        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int StartVCam(IntPtr vcam);

        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int StopVCam(IntPtr vcam);

        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int UnregisterVCam(IntPtr vcam);

        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern bool IsVCamRegistered(IntPtr vcam);

        [DllImport("MFPipeline.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern bool IsVCamStarted(IntPtr vcam);

        private IntPtr vcamHandle;
        private bool disposed = false;

        public VirtualCameraWrapper()
        {
            vcamHandle = CreateVirtualCamera();
            if (vcamHandle == IntPtr.Zero)
            {
                throw new InvalidOperationException("Failed to create virtual camera instance");
            }
        }

        ~VirtualCameraWrapper()
        {
            Dispose(false);
        }

        public void SetCameraName(string name)
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VirtualCameraWrapper));

            if (vcamHandle != IntPtr.Zero && !string.IsNullOrEmpty(name))
            {
                SetVirtualCameraName(vcamHandle, name);
            }
        }

        public bool Register()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VirtualCameraWrapper));

            if (vcamHandle == IntPtr.Zero)
                return false;

            return RegisterVCam(vcamHandle) == 0;
        }

        public bool Start()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VirtualCameraWrapper));

            if (vcamHandle == IntPtr.Zero)
                return false;

            return StartVCam(vcamHandle) == 0;
        }

        public bool Stop()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VirtualCameraWrapper));

            if (vcamHandle == IntPtr.Zero)
                return false;

            return StopVCam(vcamHandle) == 0;
        }

        public bool Unregister()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VirtualCameraWrapper));

            if (vcamHandle == IntPtr.Zero)
                return false;

            return UnregisterVCam(vcamHandle) == 0;
        }

        public bool IsRegistered
        {
            get
            {
                if (disposed || vcamHandle == IntPtr.Zero)
                    return false;

                return IsVCamRegistered(vcamHandle);
            }
        }

        public bool IsStarted
        {
            get
            {
                if (disposed || vcamHandle == IntPtr.Zero)
                    return false;

                return IsVCamStarted(vcamHandle);
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
                if (vcamHandle != IntPtr.Zero)
                {
                    // Stop and unregister before destroying
                    if (IsStarted)
                    {
                        StopVCam(vcamHandle);
                    }

                    if (IsRegistered)
                    {
                        UnregisterVCam(vcamHandle);
                    }

                    DestroyVirtualCamera(vcamHandle);
                    vcamHandle = IntPtr.Zero;
                }

                disposed = true;
            }
        }
    }
}
