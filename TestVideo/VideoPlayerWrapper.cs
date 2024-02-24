using System;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace TestVideo
{
    public class VideoPlayerWrapper : IDisposable
    {
        private IntPtr playerInstance;
        private bool disposed = false;

        // Import delle funzioni dalla DLL C++
        [DllImport("UserSpaceMF.dll")]
        private static extern IntPtr CreateVideoPlayer();

        [DllImport("UserSpaceMF.dll")]
        private static extern void DestroyVideoPlayer(IntPtr player);

        [DllImport("UserSpaceMF.dll", CharSet = CharSet.Unicode)]
        private static extern void SetVideoPath(IntPtr player, string path);

        [DllImport("UserSpaceMF.dll")]
        private static extern void SetWindowHandle(IntPtr player, IntPtr hwnd);

        [DllImport("UserSpaceMF.dll")]
        private static extern int InitializePlayer(IntPtr player);

        [DllImport("UserSpaceMF.dll")]
        private static extern int PlayVideo(IntPtr player);

        [DllImport("UserSpaceMF.dll")]
        private static extern int PauseVideo(IntPtr player);

        [DllImport("UserSpaceMF.dll")]
        private static extern int StopVideo(IntPtr player);

        [DllImport("UserSpaceMF.dll")]
        private static extern bool IsPlaying(IntPtr player);

        [DllImport("UserSpaceMF.dll")]
        private static extern bool IsPaused(IntPtr player);

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

            return InitializePlayer(playerInstance) == 0;
        }

        public bool Play()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            return PlayVideo(playerInstance) == 0;
        }

        public bool Pause()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            return PauseVideo(playerInstance) == 0;
        }

        public bool Stop()
        {
            if (disposed)
                throw new ObjectDisposedException(nameof(VideoPlayerWrapper));

            return StopVideo(playerInstance) == 0;
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