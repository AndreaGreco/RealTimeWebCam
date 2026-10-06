using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Security.AccessControl;
using System.Security.Principal;

namespace RTVirtualCamera
{
    /// <summary>
    /// The custom image the virtual camera shows while there is no live video (synthetic
    /// frame: app closed, camera offline, producer stalled). Per Windows user: the chosen
    /// picture is re-encoded to %LOCALAPPDATA%\RTVirtualCamera\offline-image.png
    /// (Shared/VCamConfig.h) with an explicit read ACE for LOCAL SERVICE, since the Frame
    /// Server otherwise can't open files in the profile. The path is passed to the Frame
    /// Server as an MF attribute at camera start; FrameGenerator re-checks the file, so a
    /// change shows up within a couple of seconds.
    /// </summary>
    internal static class OfflineImage
    {
        // Larger images are scaled down: the camera never outputs more than 4K and the
        // Frame Server decodes the file on every change.
        private const int MaxWidth = 3840;
        private const int MaxHeight = 2160;

        public static string TargetPath
        {
            get { return VirtualCameraWrapper.GetOfflineImagePath(); }
        }

        public static bool IsInstalled
        {
            get
            {
                string path = TargetPath;
                return path != null && File.Exists(path);
            }
        }

        /// <summary>
        /// Validates <paramref name="sourcePath"/> as an image, scales it down if needed and
        /// publishes it for the Frame Server. Throws on an unreadable image or an I/O error.
        /// </summary>
        public static void Install(string sourcePath)
        {
            string target = TargetPath;
            if (target == null)
                throw new InvalidOperationException("LocalAppData folder not available");

            Directory.CreateDirectory(Path.GetDirectoryName(target));

            // Write next to the target, then swap it in, so the Frame Server never reads
            // a half-written file.
            string tmp = target + ".tmp";
            using (Image source = Image.FromFile(sourcePath))
            using (Bitmap scaled = ScaleToFit(source))
            {
                scaled.Save(tmp, ImageFormat.Png);
            }

            try
            {
                FileInfo info = new FileInfo(tmp);
                FileSecurity security = info.GetAccessControl();
                security.AddAccessRule(new FileSystemAccessRule(
                    new SecurityIdentifier(WellKnownSidType.LocalServiceSid, null),
                    FileSystemRights.Read,
                    AccessControlType.Allow));
                info.SetAccessControl(security);

                File.Move(tmp, target, true);
            }
            catch
            {
                try { File.Delete(tmp); } catch { /* best effort */ }
                throw;
            }
        }

        /// <summary>Removes the custom image: the camera goes back to the default frame.</summary>
        public static void Remove()
        {
            string target = TargetPath;
            if (target != null && File.Exists(target))
                File.Delete(target);
        }

        /// <summary>
        /// Loads the installed image for a UI preview without keeping the file open (the
        /// Frame Server and a later Install must be able to touch it). Null if none.
        /// </summary>
        public static Image LoadPreview()
        {
            string target = TargetPath;
            if (target == null || !File.Exists(target))
                return null;

            try
            {
                using (MemoryStream ms = new MemoryStream(File.ReadAllBytes(target)))
                using (Image img = Image.FromStream(ms))
                {
                    return new Bitmap(img);
                }
            }
            catch (Exception)
            {
                return null;
            }
        }

        private static Bitmap ScaleToFit(Image source)
        {
            double scale = Math.Min(1.0, Math.Min(MaxWidth / (double)source.Width, MaxHeight / (double)source.Height));
            int w = Math.Max(1, (int)Math.Round(source.Width * scale));
            int h = Math.Max(1, (int)Math.Round(source.Height * scale));

            Bitmap result = new Bitmap(w, h, PixelFormat.Format32bppArgb);
            using (Graphics g = Graphics.FromImage(result))
            {
                g.InterpolationMode = System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic;
                g.DrawImage(source, 0, 0, w, h);
            }
            return result;
        }
    }
}
