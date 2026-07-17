using RTVirtualCamera.Properties;
using System;
using System.Globalization;
using System.IO;
using System.Resources;
using System.Text.Json;

namespace RTVirtualCamera
{
    public class Settings
    {
        private static string FileName
        {
            get
            {
                // %LOCALAPPDATA%\RTVirtualCamera — the install dir (Program Files)
                // is not writable for standard users.
                string dir = Path.Combine(
                    Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                    "RTVirtualCamera");
                Directory.CreateDirectory(dir); // idempotent
                return Path.Combine(dir, "settings.json");
            }
        }

        public static Settings Current { get; private set; } = new Settings();

        public string Language { get; set; } = string.Empty;
        public bool AutoStart { get; set; } = false;
        public Uri RtspURL { get; set; }

        // Diagnostic: burn a frame counter into every delivered frame (real + synthetic)
        // so the actual consumer-side frame rate is visible on the video itself.
        public bool FrameCounterOverlay { get; set; } = false;

        // RTSP lower-transport preference. Auto = UDP with TCP fallback (lowest latency
        // when UDP is available). Applied to the FFmpeg engine on the next connection.
        public RtspTransportMode RtspTransport { get; set; } = RtspTransportMode.Auto;

        // Let the decoder use the GPU (d3d11va). Off forces software decode — useful when
        // a d3d11va driver misbehaves. Applied to the FFmpeg engine on the next connection.
        public bool HardwareDecode { get; set; } = true;

        // --- FFmpeg fine-tuning (applied to the engine on the next connection) ------
        // RTSP socket timeout (libav "stimeout"), milliseconds. Also bounds how long a
        // dead UDP attempt waits before Auto falls back to TCP.
        public int SocketTimeoutMs { get; set; } = 5000;

        // RTP jitter/reorder buffer depth (libav "reorder_queue_size"), packets. 0 =
        // lowest latency but no tolerance for UDP reordering; a few packets smooth it.
        public int ReorderQueueSize { get; set; } = 8;

        // Latency cap: resync-to-live threshold (ms). Lower = tighter latency, more jumps.
        public int LatencyCapMs { get; set; } = 350;

        public static void Load()
        {
            try
            {
                if (!File.Exists(FileName))
                {
                    Current = new Settings();
                    return;
                }

                string json = File.ReadAllText(FileName);
                Settings loaded = JsonSerializer.Deserialize<Settings>(json);
                Current = loaded ?? new Settings();
            }
            catch
            {
                Current = new Settings();
            }
        }

        public void Save()
        {
            var options = new JsonSerializerOptions();
            options.WriteIndented = true;
            File.WriteAllText(FileName, JsonSerializer.Serialize(this, options));
        }

        public string GetEffectiveLanguage()
        {
            if (!string.IsNullOrWhiteSpace(Language))
            {
                return Language;
            }

            try
            {
                return CultureInfo.InstalledUICulture.Name;
            }
            catch
            {
                return "en";
            }
        }
    }

    internal static class AppStrings
    {
        private static readonly ResourceManager ResourceManager = Resources.ResourceManager;

        public static string Get(string key)
        {
            string value = ResourceManager.GetString(key, LocalizationManager.ResolveCulture());
            return string.IsNullOrEmpty(value) ? key : value;
        }
    }
}
