using System;
using System.Globalization;
using System.Threading;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    internal static class Program
    {
        /// <summary>
        /// Punto di ingresso principale dell'applicazione.
        /// </summary>
        [STAThread]
        static int Main(string[] args)
        {
            // Run by the MSI on uninstall (Setup/Package.wxs, as the uninstalling user):
            // remove the persistent virtual camera if one is registered, no UI.
            if (Array.Exists(args, a => string.Equals(a, "--remove-camera", StringComparison.OrdinalIgnoreCase)))
                return RemoveCamera();

            Settings.Load();
            LocalizationManager.InitializeCulture(Settings.Current.GetEffectiveLanguage());

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
            return 0;
        }

        // Always attempts the removal (harmless when no camera is registered), so a
        // camera the settings don't know about is removed too. On a worker (MTA) thread,
        // like every other virtual-camera call in the app. Never throws and never hangs
        // for long: uninstall must not fail or stall because of it.
        private static int RemoveCamera()
        {
            try
            {
                var task = System.Threading.Tasks.Task.Run(() => VirtualCameraWrapper.RemovePersistentCamera());
                if (!task.Wait(TimeSpan.FromSeconds(30)))
                    return 2;
                bool removed = task.Result;

                Settings.Load();
                Settings.Current.PersistentCameraRegistered = false;
                Settings.Current.PersistentCameraConfig = string.Empty;
                Settings.Current.Save();
                return removed ? 0 : 1;
            }
            catch (Exception)
            {
                return 1;
            }
        }
    }

    internal static class LocalizationManager
    {
        public static CultureInfo ResolveCulture()
        {
            return Thread.CurrentThread.CurrentUICulture ?? CultureInfo.InstalledUICulture;
        }

        public static void InitializeCulture(string languageCode)
        {
            CultureInfo culture;

            if (!string.IsNullOrWhiteSpace(languageCode))
            {
                try
                {
                    culture = CultureInfo.GetCultureInfo(languageCode);
                }
                catch (CultureNotFoundException)
                {
                    culture = CultureInfo.InstalledUICulture;
                }
            }
            else
            {
                culture = CultureInfo.InstalledUICulture;
            }

            Thread.CurrentThread.CurrentCulture = culture;
            Thread.CurrentThread.CurrentUICulture = culture;
        }
    }
}
