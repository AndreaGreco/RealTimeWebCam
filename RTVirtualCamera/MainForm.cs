using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    public partial class MainForm : Form
    {
        private const int ProbeTimeoutMs = 8000;

        private VideoPlayerWrapper videoPlayer;
        private VirtualCameraWrapper virtualCamera;
        private bool isVCamRunning = false;
        private StreamInfo streamInfo;

        // Live preview diagnostics (preview path only — the VCam/Zoom path runs in
        // the Frame Server process and can't be read from here).
        private System.Windows.Forms.Timer statsTimer;
        private Label statsLabel;

        private sealed class SourceProbeResult
        {
            public bool Success { get; set; }
            public string UserMessage { get; set; }
            public string TechnicalDetails { get; set; }
            public StreamInfo[] Streams { get; set; }
        }

        public MainForm()
        {
            InitializeComponent();

            ApplyLocalizedTexts();
            InitializeVideoPlayer();
            InitializeStatsOverlay();
            videoPanel.Resize += VideoPanel_Resize;
            Shown += MainForm_Shown;
        }

        private void InitializeStatsOverlay()
        {
            statsLabel = new Label
            {
                Dock = DockStyle.Top,
                Height = 22,
                TextAlign = ContentAlignment.MiddleLeft,
                BackColor = Color.Black,
                ForeColor = Color.Lime,
                Font = new Font("Consolas", 9f),
                Text = "preview stats: --"
            };
            Controls.Add(statsLabel);
            statsLabel.BringToFront();

            statsTimer = new System.Windows.Forms.Timer { Interval = 500 };
            statsTimer.Tick += StatsTimer_Tick;
            statsTimer.Start();
        }

        private void StatsTimer_Tick(object sender, EventArgs e)
        {
            try
            {
                PreviewRates r;
                if (videoPlayer != null && !isVCamRunning && videoPlayer.TryGetRates(out r))
                {
                    statsLabel.Text = string.Format(
                        "RX {0:0.0} fps  |  render {1:0.0} fps  |  drop {2:0.0} fps  |  drift {3:+0;-0} ms  |  copy {4:0} ms",
                        r.ReceivedFps, r.RenderedFps, r.DroppedFps, r.DriftMs, r.LastRenderMs);
                }
                else if (isVCamRunning)
                {
                    statsLabel.Text = "preview stats: n/d (camera virtuale attiva — gira nel Frame Server)";
                }
                else
                {
                    statsLabel.Text = "preview stats: anteprima non attiva";
                }
            }
            catch
            {
                // Diagnostics only — never let a stats hiccup disturb the UI.
            }
        }



        private void ApplyLocalizedTexts()
        {
            Text = AppStrings.Get("App_Title");

            if (playButton != null)
                playButton.Text = AppStrings.Get("Button_StartPreview");

            if (startVCamButton != null)
                startVCamButton.Text = isVCamRunning ? AppStrings.Get("Button_StopVCam") : AppStrings.Get("Button_StartVCam");

            if (previewStatusLabel != null && string.IsNullOrWhiteSpace(previewStatusLabel.Text))
                previewStatusLabel.Text = AppStrings.Get("Preview_Inactive");
        }

        private void VideoPanel_Resize(object sender, EventArgs e)
        {
            try
            {
                if (videoPlayer != null)
                {
                    videoPlayer.SetWindowHandle(videoPanel);
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine("Error updating video position: " + ex.Message);
            }
        }

        private void InitializeVideoPlayer()
        {
            try
            {
                videoPlayer = new VideoPlayerWrapper();
                videoPlayer.SetWindowHandle(videoPanel);
                previewStatusLabel.Visible = false;
            }
            catch (Exception ex)
            {
                ShowFriendlyError(AppStrings.Get("Preview_Error_Title"), AppStrings.Get("Preview_Error_Start"), ex.Message);
            }
        }

        private void BrowseButton_Click(object sender, EventArgs e)
        {
            using (OpenFileDialog dialog = new OpenFileDialog())
            {
                dialog.Filter = "Video files (*.mp4;*.avi;*.wmv)|*.mp4;*.avi;*.wmv|All files (*.*)|*.*";
                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    pathTextBox.Text = dialog.FileName;
                }
            }
        }

        private void PlayButton_Click(object sender, EventArgs e)
        {
            try
            {
                if (!string.IsNullOrWhiteSpace(pathTextBox.Text))
                {
                    UseWaitCursor = true;

                    SourceProbeResult probe = ProbeSourceWithTimeout(pathTextBox.Text);
                    if (!probe.Success)
                    {
                        ShowFriendlyError(AppStrings.Get("Source_Unavailable_Title"), probe.UserMessage, probe.TechnicalDetails);
                        return;
                    }

                    ApplyStreamInfo(probe.Streams);

                    if (StartPreviewFromPath())
                    {
                        startVCamButton.Enabled = true;
                        Settings.Current.RtspURL = new Uri(pathTextBox.Text);
                        Settings.Current.Save();
                    }
                }
                else
                {
                    ShowFriendlyError(AppStrings.Get("Source_Missing_Title"), AppStrings.Get("Source_SelectPrompt"));
                }
            }
            catch (Exception ex)
            {
                ShowFriendlyError(AppStrings.Get("Preview_Error_Title"), AppStrings.Get("Preview_Error_Start"), ex.Message);
            }
            finally
            {
                UseWaitCursor = false;
            }
        }

        private void StartVCamButton_Click(object sender, EventArgs e)
        {
            try
            {
                if (!isVCamRunning)
                {
                    UseWaitCursor = true;

                    SourceProbeResult probe = ProbeSourceWithTimeout(pathTextBox.Text);
                    if (!probe.Success)
                    {
                        ShowFriendlyError(AppStrings.Get("Source_Unavailable_Title"), probe.UserMessage, probe.TechnicalDetails);
                        return;
                    }

                    ApplyStreamInfo(probe.Streams);

                    virtualCamera = new VirtualCameraWrapper();
                    virtualCamera.SetCameraName("RTSP Virtual Camera");

                    VCamConfig config = new VCamConfig();
                    config.RtspUrl = pathTextBox.Text ?? string.Empty;
                    config.Width = streamInfo.width;
                    config.Height = streamInfo.height;
                    config.FpsNum = streamInfo.fpsNum;
                    config.FpsDen = streamInfo.fpsDen;
                    config.Format = streamInfo.subtype;
                    virtualCamera.SetConfig(config);

                    if (virtualCamera.Register())
                    {
                        System.Diagnostics.Debug.WriteLine("Virtual camera registered");

                        if (virtualCamera.Start())
                        {
                            StopPreview();
                            SetPreviewStatus(AppStrings.Get("Preview_VCamStarted"));

                            isVCamRunning = true;
                            startVCamButton.Text = AppStrings.Get("Button_StopVCam");
                            startVCamButton.BackColor = Color.LightCoral;
                        }
                        else
                        {
                            ShowFriendlyError(
                                AppStrings.Get("VirtualCamera_StartFail_Title"),
                                AppStrings.Get("VirtualCamera_StartFail_Message"),
                                AppStrings.Get("VirtualCamera_StartFail_Details"));
                            if (virtualCamera != null)
                            {
                                virtualCamera.Dispose();
                                virtualCamera = null;
                            }
                        }
                    }
                    else
                    {
                        ShowFriendlyError(
                            AppStrings.Get("VirtualCamera_RegisterFail_Title"),
                            AppStrings.Get("VirtualCamera_RegisterFail_Message"),
                            AppStrings.Get("VirtualCamera_RegisterFail_Details"));
                        if (virtualCamera != null)
                        {
                            virtualCamera.Dispose();
                            virtualCamera = null;
                        }
                    }
                }
                else
                {
                    if (virtualCamera != null)
                    {
                        try
                        {
                            StopPreview();
                        }
                        catch (Exception ex)
                        {
                            System.Diagnostics.Debug.WriteLine("Error stopping preview: " + ex.Message);
                        }

                        virtualCamera.Stop();
                        virtualCamera.Unregister();
                        virtualCamera.Dispose();
                        virtualCamera = null;
                    }

                    if (!string.IsNullOrEmpty(pathTextBox.Text))
                    {
                        previewStatusLabel.Visible = false;
                        StartPreviewFromPath();
                    }
                    else
                    {
                        SetPreviewStatus(AppStrings.Get("Preview_Inactive"));
                    }

                    isVCamRunning = false;
                    startVCamButton.Text = AppStrings.Get("Button_StartVCam");
                    startVCamButton.BackColor = Color.LightGreen;

                    System.Diagnostics.Debug.WriteLine("Virtual camera stopped");
                    MessageBox.Show(AppStrings.Get("VirtualCamera_Stopped"), AppStrings.Get("Info_Title"), MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
            }
            catch (Exception ex)
            {
                ShowFriendlyError(AppStrings.Get("VirtualCamera_Error_Title"), AppStrings.Get("VirtualCamera_Error_Message"), ex.Message);

                if (virtualCamera != null)
                {
                    virtualCamera.Dispose();
                    virtualCamera = null;
                }

                isVCamRunning = false;
                startVCamButton.Text = AppStrings.Get("Button_StartVCam");
                startVCamButton.BackColor = Color.LightGreen;
            }
            finally
            {
                UseWaitCursor = false;
            }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            if (statsTimer != null)
            {
                statsTimer.Stop();
                statsTimer.Dispose();
                statsTimer = null;
            }

            if (isVCamRunning && virtualCamera != null)
            {
                try
                {
                    virtualCamera.Stop();
                    virtualCamera.Unregister();
                    virtualCamera.Dispose();
                }
                catch (Exception ex)
                {
                    System.Diagnostics.Debug.WriteLine("Error disposing virtual camera: " + ex.Message);
                }
            }

            if (videoPlayer != null)
            {
                videoPlayer.Dispose();
            }

            base.OnFormClosed(e);
        }

        private bool StartPreviewFromPath()
        {
            videoPlayer.Stop();
            videoPlayer.ClearCaptureDeviceName();
            videoPlayer.SetVideoPath(pathTextBox.Text);
            previewStatusLabel.Visible = false;

            bool init = videoPlayer.Initialize();
            if (!init)
            {
                ShowFriendlyError(
                    AppStrings.Get("Preview_OpenFail_Title"),
                    AppStrings.Get("Preview_OpenFail_Message"),
                    AppStrings.Get("Preview_OpenFail_Details"));
                return false;
            }

            StreamInfo[] infos = videoPlayer.GetStreamInfos();
            if (!videoPlayer.Play())
            {
                ShowFriendlyError(
                    AppStrings.Get("Preview_PlayFail_Title"),
                    AppStrings.Get("Preview_PlayFail_Message"),
                    AppStrings.Get("Preview_PlayFail_Details"));
                return false;
            }

            ApplyStreamInfo(infos);
            return true;
        }

        private void StopPreview()
        {
            videoPlayer.Stop();
            videoPlayer.ClearCaptureDeviceName();
        }

        private void SetPreviewStatus(string message)
        {
            previewStatusLabel.Text = message;
            previewStatusLabel.Visible = true;
            previewStatusLabel.BringToFront();
        }

        private void ApplyStreamInfo(StreamInfo[] infos)
        {
            if (infos == null || infos.Length == 0)
            {
                return;
            }

            streamInfo = infos[0];
            streamProp_lbl.Text = string.Format(AppStrings.Get("Resolution_Format"), streamInfo.width, streamInfo.height, streamInfo.getFSP());
        }

        private SourceProbeResult ProbeSourceWithTimeout(string path)
        {
            string validationError = ValidateSourcePath(path);
            if (!string.IsNullOrEmpty(validationError))
            {
                return FailProbe(validationError, AppStrings.Get("Probe_Validation_Details"));
            }

            IntPtr previewHandle = videoPanel.Handle;
            Task<SourceProbeResult> probeTask = Task.Factory.StartNew(
                delegate { return ProbeSource(path, previewHandle); },
                TaskCreationOptions.LongRunning);

            if (!probeTask.Wait(ProbeTimeoutMs))
            {
                return FailProbe(
                    AppStrings.Get("Probe_Timeout_Message"),
                    string.Format(AppStrings.Get("Probe_Timeout_Details"), ProbeTimeoutMs));
            }

            try
            {
                return probeTask.Result;
            }
            catch (AggregateException ex)
            {
                Exception inner = ex.Flatten().InnerException ?? ex;
                return MapProbeException(inner);
            }
            catch (Exception ex)
            {
                return MapProbeException(ex);
            }
        }

        private SourceProbeResult ProbeSource(string path, IntPtr previewHandle)
        {
            using (VideoPlayerWrapper probePlayer = new VideoPlayerWrapper())
            {
                probePlayer.SetWindowHandle(previewHandle);
                probePlayer.ClearCaptureDeviceName();
                probePlayer.SetVideoPath(path);

                if (!probePlayer.Initialize())
                {
                    return FailProbe(
                        AppStrings.Get("Probe_InitFail_Message"),
                        AppStrings.Get("Probe_InitFail_Details"));
                }

                StreamInfo[] infos = probePlayer.GetStreamInfos();
                if (infos == null || infos.Length == 0)
                {
                    return FailProbe(
                        AppStrings.Get("Probe_NoStreams_Message"),
                        AppStrings.Get("Probe_NoStreams_Details"));
                }

                return new SourceProbeResult
                {
                    Success = true,
                    UserMessage = string.Empty,
                    TechnicalDetails = string.Empty,
                    Streams = infos
                };
            }
        }

        private static SourceProbeResult MapProbeException(Exception ex)
        {
            COMException comEx = ex as COMException;
            if (comEx != null)
            {
                return FailProbe(
                    AppStrings.Get("Probe_COM_Message"),
                    string.Format("COM/HRESULT: 0x{0:X8} - {1}", comEx.ErrorCode, comEx.Message));
            }

            SEHException sehEx = ex as SEHException;
            if (sehEx != null)
            {
                return FailProbe(
                    AppStrings.Get("Probe_SEH_Message"),
                    sehEx.Message);
            }

            return FailProbe(
                AppStrings.Get("Probe_Unexpected_Message"),
                ex.Message);
        }

        private static SourceProbeResult FailProbe(string userMessage, string technicalDetails)
        {
            return new SourceProbeResult
            {
                Success = false,
                UserMessage = userMessage,
                TechnicalDetails = technicalDetails,
                Streams = new StreamInfo[0]
            };
        }

        private static string ValidateSourcePath(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
            {
                return AppStrings.Get("Source_SelectPrompt");
            }

            if (File.Exists(path))
            {
                return null;
            }

            Uri uri;
            if (Uri.TryCreate(path, UriKind.Absolute, out uri))
            {
                string scheme = uri.Scheme.ToLowerInvariant();
                if (scheme == "rtsp" || scheme == Uri.UriSchemeHttp || scheme == Uri.UriSchemeHttps)
                {
                    return null;
                }

                if (scheme == Uri.UriSchemeFile)
                {
                    return File.Exists(uri.LocalPath) ? null : AppStrings.Get("Path_FileMissing");
                }
            }

            return AppStrings.Get("Path_Invalid");
        }

        private static void ShowFriendlyError(string title, string message, string technicalDetails = null)
        {
            string fullMessage = message;
            if (!string.IsNullOrWhiteSpace(technicalDetails))
            {
                fullMessage = fullMessage + Environment.NewLine + Environment.NewLine + AppStrings.Get("Tech_Details") + Environment.NewLine + technicalDetails;
            }

            MessageBox.Show(fullMessage, title, MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }

        private void autoStartToolStripMenuItem_Click(object sender, EventArgs e)
        {
            ToolStripMenuItem menuItem = sender as ToolStripMenuItem;

            Settings.Current.AutoStart = menuItem.Checked;
            Settings.Current.Save();
        }

        private void panel1_Paint(object sender, PaintEventArgs e)
        {

        }

        private void MainForm_Load(object sender, EventArgs e)
        {
            Settings.Load();
            this.pathTextBox.Text = Settings.Current.RtspURL?.ToString() ?? "rtsp://example.io:1234/webcam";
            // Autostart is handled in MainForm_Shown (after the window is visible) on a
            // background thread, so an unreachable source never blocks the UI thread.
        }

        // Runs after the window is first shown. If autostart is configured, probe the
        // source on a background thread and only touch the UI via BeginInvoke. No
        // async/await — a single fire-and-forget worker keeps the UI thread free.
        private void MainForm_Shown(object sender, EventArgs e)
        {
            if (Settings.Current.RtspURL == null || !Settings.Current.AutoStart)
                return;

            string path = pathTextBox.Text;
            if (string.IsNullOrWhiteSpace(path))
                return;

            IntPtr previewHandle = videoPanel.Handle; // must be read on the UI thread

            System.Threading.Thread worker = new System.Threading.Thread(delegate ()
            {
                SourceProbeResult probe = ProbeSource(path, previewHandle);

                if (IsDisposed || !IsHandleCreated)
                    return;

                try
                {
                    BeginInvoke((Action)delegate ()
                    {
                        if (!probe.Success)
                        {
                            SetPreviewStatus(AppStrings.Get("Preview_Inactive"));
                            ShowFriendlyError(AppStrings.Get("Source_Unavailable_Title"), probe.UserMessage, probe.TechnicalDetails);
                            return;
                        }

                        ApplyStreamInfo(probe.Streams);
                        if (StartPreviewFromPath())
                            startVCamButton.Enabled = true;
                    });
                }
                catch (InvalidOperationException)
                {
                    // Window was closed between the IsHandleCreated check and BeginInvoke — ignore.
                }
            });
            worker.IsBackground = true;
            worker.Name = "AutostartProbe";
            worker.Start();
        }

        private void exitToolStripMenuItem_Click(object sender, EventArgs e)
        {
            this.Close();
        }

        private void toolStripMenuItem1_Click(object sender, EventArgs e)
        {
            SettingsForm langForm = new SettingsForm();
            langForm.ShowDialog(this);
        }

        private void settingsToolStripMenuItem_Click(object sender, EventArgs e)
        {
            SettingsForm settingsForm = new SettingsForm();
            settingsForm.ShowDialog(this);
        }
    }
}
