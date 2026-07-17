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

        // Live diagnostics timer for the side panel. The panel itself (statsPanel with
        // the connList / statsList tables) lives in MainForm.Designer.cs so it shows up
        // in the Windows Forms designer; this timer just refreshes the values.
        private System.Windows.Forms.Timer statsTimer;

        private sealed class SourceProbeResult
        {
            public bool Success { get; set; }
            public string UserMessage { get; set; }
            public string TechnicalDetails { get; set; }
            public StreamInfo[] Streams { get; set; }
            public ConnectionInfo Connection { get; set; }
            public bool HasConnection { get; set; }
        }

        public MainForm()
        {
            InitializeComponent();

            ApplyLocalizedTexts();
            InitializeVideoPlayer();
            InitializeStatsTimer();
            videoPanel.Resize += VideoPanel_Resize;
            Shown += MainForm_Shown;
        }

        // The two tables (connList / statsList) and their container (statsPanel) are
        // defined in MainForm.Designer.cs with their fixed rows (each ListViewItem
        // carries a Name key used by SetRow). Here we only drive the refresh timer.
        private void InitializeStatsTimer()
        {
            statsTimer = new System.Windows.Forms.Timer { Interval = 500 };
            statsTimer.Tick += StatsTimer_Tick;
            statsTimer.Start();
        }

        private static void SetRow(ListView lv, string key, string value)
        {
            ListViewItem[] found = lv.Items.Find(key, false);
            if (found.Length > 0)
                found[0].SubItems[1].Text = string.IsNullOrEmpty(value) ? "—" : value;
        }

        private void ApplyConnectionInfo(SourceProbeResult probe)
        {
            if (probe == null || !probe.HasConnection || probe.Connection.valid == 0)
                return;

            ConnectionInfo c = probe.Connection;

            string codec = c.videoCodec ?? string.Empty;
            if (!string.IsNullOrEmpty(c.profile))
                codec = (codec.Length > 0 ? codec + " (" + c.profile + ")" : c.profile);

            string fps = c.fpsDen > 0
                ? (c.fpsNum / (double)c.fpsDen).ToString("0.##")
                : null;

            string bitrate;
            if (c.bitrate <= 0)
                bitrate = AppStrings.Get("Value_NotAvailable");
            else if (c.bitrate >= 1000000)
                bitrate = (c.bitrate / 1000000.0).ToString("0.0") + " Mbps";
            else
                bitrate = (c.bitrate / 1000.0).ToString("0") + " kbps";

            SetRow(connList, "container", c.container);
            SetRow(connList, "transport", c.transport);
            SetRow(connList, "codec", codec);
            SetRow(connList, "pixfmt", c.pixelFormat);
            SetRow(connList, "resolution",
                (c.width > 0 && c.height > 0) ? c.width + "×" + c.height : null);
            SetRow(connList, "fps", fps);
            SetRow(connList, "bitrate", bitrate);
        }

        private void StatsTimer_Tick(object sender, EventArgs e)
        {
            try
            {
                // Reflect the *actual* live RTSP transport (UDP/TCP) in the connection
                // table, overriding the probe's guess once frames are flowing. Null while
                // not connected — then the probed value stays.
                string activeTransport = isVCamRunning
                    ? (virtualCamera != null ? virtualCamera.GetActiveTransportLabel() : null)
                    : (videoPlayer != null ? videoPlayer.GetActiveTransportLabel() : null);
                if (!string.IsNullOrEmpty(activeTransport))
                    SetRow(connList, "transport", activeTransport);

                if (isVCamRunning)
                {
                    FrameServerRates fr;
                    if (virtualCamera != null && virtualCamera.TryGetFrameServerRates(out fr) && fr.Available)
                    {
                        // Decode happens in the app producer (GPU via d3d11va, or software);
                        // the Frame Server's own frame-channel copy is always CPU.
                        bool ffmpegHw = virtualCamera.IsFfmpegProducerHardware();
                        SetRow(statsList, "state", fr.Stale ? AppStrings.Get("Stats_State_WaitingStale") : AppStrings.Get("Stats_State_CameraActive"));
                        SetRow(statsList, "engine", ffmpegHw ? "FFmpeg HW" : "FFmpeg SW");
                        SetRow(statsList, "decode", ffmpegHw ? "GPU (d3d11va)" : "CPU (software)");
                        SetRow(statsList, "rx", fr.RxFps.ToString("0.0"));
                        SetRow(statsList, "render", fr.RenderedFps.ToString("0.0"));
                        SetRow(statsList, "dup", fr.DeclinedFps.ToString("0.0"));
                        SetRow(statsList, "drop", fr.DroppedFps.ToString("0.0"));
                        SetRow(statsList, "proc", fr.LastCopyMs.ToString("0.0"));
                        SetRow(statsList, "drift", fr.DriftMs.ToString("+0;-0;0"));
                    }
                    else
                    {
                        SetRow(statsList, "state", AppStrings.Get("Stats_State_WaitingStats"));
                    }
                }
                else
                {
                    PreviewRates r;
                    if (videoPlayer != null && videoPlayer.TryGetRates(out r))
                    {
                        SetRow(statsList, "state", AppStrings.Get("Stats_State_PreviewActive"));
                        SetRow(statsList, "engine", AppStrings.Get("Stats_Engine_Preview"));
                        SetRow(statsList, "decode", "—");
                        SetRow(statsList, "rx", r.ReceivedFps.ToString("0.0"));
                        SetRow(statsList, "render", r.RenderedFps.ToString("0.0"));
                        SetRow(statsList, "dup", "—");
                        SetRow(statsList, "drop", r.DroppedFps.ToString("0.0"));
                        SetRow(statsList, "proc", r.LastRenderMs.ToString("0.0"));
                        SetRow(statsList, "drift", r.DriftMs.ToString("+0;-0;0"));
                    }
                    else
                    {
                        SetRow(statsList, "state", AppStrings.Get("Stats_State_Inactive"));
                    }
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
                    ApplyConnectionInfo(probe);

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
                    ApplyConnectionInfo(probe);

                    virtualCamera = new VirtualCameraWrapper();
                    virtualCamera.SetCameraName("RTSP Virtual Camera");

                    VCamConfig config = new VCamConfig();
                    config.RtspUrl = pathTextBox.Text ?? string.Empty;
                    config.Width = streamInfo.width;
                    config.Height = streamInfo.height;
                    config.FpsNum = streamInfo.fpsNum;
                    config.FpsDen = streamInfo.fpsDen;
                    config.Format = streamInfo.subtype;
                    config.Overlay = Settings.Current.FrameCounterOverlay ? 1u : 0u;
                    virtualCamera.SetConfig(config);

                    if (virtualCamera.Register())
                    {
                        System.Diagnostics.Debug.WriteLine("Virtual camera registered");

                        if (virtualCamera.Start())
                        {
                            // The Frame Server never opens the RTSP source itself — the app
                            // decodes with FFmpeg and streams frames to it. Start the
                            // user-space producer now that the camera is running.
                            if (!virtualCamera.StartFfmpegProducer(
                                    config.RtspUrl, config.Width, config.Height, config.FpsNum, config.FpsDen))
                                System.Diagnostics.Debug.WriteLine("FFmpeg producer failed to start");

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

                        virtualCamera.StopFfmpegProducer(); // no-op unless the FFmpeg engine was running
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

                ConnectionInfo conn;
                bool hasConn = probePlayer.TryGetConnectionInfo(out conn);

                return new SourceProbeResult
                {
                    Success = true,
                    UserMessage = string.Empty,
                    TechnicalDetails = string.Empty,
                    Streams = infos,
                    Connection = conn,
                    HasConnection = hasConn
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
            // Push the persisted FFmpeg engine options (transport, hardware decode, and
            // the numeric tuning params) into RTCamNative before any connection is opened.
            VirtualCameraWrapper.ApplyEngineSettings();
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
                        ApplyConnectionInfo(probe);
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

        private void aboutToolStripMenuItem_Click(object sender, EventArgs e)
        {
            About formAbout = new About();
            formAbout.ShowDialog(this);
        }
    }
}
