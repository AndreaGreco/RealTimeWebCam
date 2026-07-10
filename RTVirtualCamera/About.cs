using System;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    public partial class About : Form
    {
        private const string ProjectUrl = "https://github.com/AndreaGreco/RealTimeWebCam";

        public About()
        {
            InitializeComponent();
        }

        private void About_Load(object sender, EventArgs e)
        {
            Text = AppStrings.Get("App_Title");
            titleLabel.Text = AppStrings.Get("App_Title");
            versionLabel.Text = string.Format(AppStrings.Get("About_VersionFormat"), Application.ProductVersion);
            taglineLabel.Text = AppStrings.Get("About_Tagline");
            creditsLabel.Text = AppStrings.Get("About_Credits");
            closeButton.Text = AppStrings.Get("Button_Close");

            // Pull the icon straight out of the running exe (the one embedded via
            // ApplicationIcon/Resources/icon.ico) instead of shipping a second,
            // separately-maintained copy of the image as a form resource.
            try
            {
                using (Icon exeIcon = Icon.ExtractAssociatedIcon(Application.ExecutablePath))
                {
                    if (exeIcon != null)
                    {
                        appIconBox.Image = exeIcon.ToBitmap();
                    }
                }
            }
            catch
            {
                // Non-critical: the dialog still works without the icon preview.
            }
        }

        private void GithubLink_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
        {
            try
            {
                Process.Start(new ProcessStartInfo(ProjectUrl) { UseShellExecute = true });
            }
            catch
            {
                // Non-critical: if the shell can't resolve a browser, there's nothing
                // more useful to do than leave the label showing the URL as text.
            }
        }
    }
}
