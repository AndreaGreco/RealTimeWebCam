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
        static void Main()
        {
            Settings.Load();
            LocalizationManager.InitializeCulture(Settings.Current.GetEffectiveLanguage());

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm());
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
