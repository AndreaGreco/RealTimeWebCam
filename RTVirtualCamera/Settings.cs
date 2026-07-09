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
