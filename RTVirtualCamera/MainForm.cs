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
        // True while the preview decode is live on videoPanel. Drives the preview button's
        // toggle label (Start/Stop Preview); mutually exclusive with isVCamRunning because
        // the preview and the virtual-camera producer share the single RTSP decode core.
        private bool isPreviewRunning = false;
        private StreamInfo streamInfo;

        // Live diagnostics timer for the side panel. The panel itself (statsPanel with
        // the connList / statsList tables) lives in MainForm.Designer.cs so it shows up
        // in the Windows Forms designer; this timer just refreshes the values.
        private System.Windows.Forms.Timer statsTimer;

        // True while an async start/stop transition is running. All the blocking native
        // work (probe, RTSP open, Frame Server start/stop, thread joins) now runs on a
        // worker thread so the UI thread stays responsive; this flag serves two purposes:
        //   - re-entrancy guard: the Play / Start-VCam buttons are disabled while it is set;
        //   - it pauses StatsTimer_Tick so the timer never reads a videoPlayer / virtualCamera
        //     object that a worker thread is concurrently tearing down or setting up.
        private bool isBusy;

        // startVCamButton's enabled state captured at BeginBusy() and restored by EndBusy(),
        // so a transient disable-while-busy does not lose the button's real availability.
        private bool savedStartVCamEnabled;

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
            EnableDoubleBuffering(connList);
            EnableDoubleBuffering(statsList);

            statsTimer = new System.Windows.Forms.Timer { Interval = 500 };
            statsTimer.Tick += StatsTimer_Tick;
            statsTimer.Start();
        }

        private static void SetRow(ListView lv, string key, string value)
        {
            ListViewItem[] found = lv.Items.Find(key, false);
            if (found.Length == 0)
                return;

            string text = string.IsNullOrEmpty(value) ? "—" : value;
            // Only touch the cell when the text actually changed: an unchanged assignment
            // still invalidates/repaints the row, and most rows are static between ticks.
            if (found[0].SubItems[1].Text != text)
                found[0].SubItems[1].Text = text;
        }

        // Enables the protected Control.DoubleBuffered on a ListView (not exposed publicly)
        // so its single batched repaint is flicker-free.
        private static void EnableDoubleBuffering(Control control)
        {
            try
            {
                typeof(Control).InvokeMember("DoubleBuffered",
                    System.Reflection.BindingFlags.SetProperty
                        | System.Reflection.BindingFlags.Instance
                        | System.Reflection.BindingFlags.NonPublic,
                    null, control, new object[] { true });
            }
            catch
            {
                // Non-critical: BeginUpdate/EndUpdate already suppresses most of the flicker.
            }
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

            connList.BeginUpdate();
            try
            {
                SetRow(connList, "container", c.container);
                SetRow(connList, "transport", c.transport);
                SetRow(connList, "codec", codec);
                SetRow(connList, "pixfmt", c.pixelFormat);
                SetRow(connList, "resolution",
                    (c.width > 0 && c.height > 0) ? c.width + "×" + c.height : null);
                SetRow(connList, "fps", fps);
                SetRow(connList, "bitrate", FormatBitrate(c.bitrate));
            }
            finally
            {
                connList.EndUpdate();
            }
        }

        // Formats a bits/s value for the connection table; "n/a" for <= 0.
        private static string FormatBitrate(long bps)
        {
            if (bps <= 0)
                return AppStrings.Get("Value_NotAvailable");
            if (bps >= 1000000)
                return (bps / 1000000.0).ToString("0.0") + " Mbps";
            return (bps / 1000.0).ToString("0") + " kbps";
        }

        // Mirrors the active engine settings into the connection table so the user can
        // see exactly what is in use (values are language-neutral: numbers + units).
        private void UpdateSettingsRows()
        {
            Settings s = Settings.Current;
            SetRow(connList, "cfgTransport", TransportModeLabel(s.RtspTransport));
            SetRow(connList, "cfgHw", s.HardwareDecode ? "on" : "off");
            SetRow(connList, "cfgTimeout", s.SocketTimeoutMs + " ms");
            SetRow(connList, "cfgReorder", s.ReorderQueueSize + " pkt");
            SetRow(connList, "cfgBuffer", (s.UdpBufferSize / 1024) + " KB");
            SetRow(connList, "cfgMaxDelay", s.MaxDelayMs + " ms");
            SetRow(connList, "cfgLatency", s.LatencyCapMs + " ms");
        }

        private static string TransportModeLabel(RtspTransportMode mode)
        {
            switch (mode)
            {
                case RtspTransportMode.Udp: return "UDP";
                case RtspTransportMode.Tcp: return "TCP";
                default: return "Auto";
            }
        }

        // Enters a busy transition: disables the action buttons and pauses the stats timer
        // work so no worker thread and the UI thread touch the same native object at once.
        private void BeginBusy()
        {
            isBusy = true;
            UseWaitCursor = true;
            savedStartVCamEnabled = startVCamButton.Enabled;
            playButton.Enabled = false;
            startVCamButton.Enabled = false;
        }

        // Leaves a busy transition. startVCamButton is restored to whatever the handler left
        // in savedStartVCamEnabled (a successful start/stop sets it to true before this runs);
        // the preview button's label and enabled state are reconciled from the current
        // preview/vcam state by RefreshActionButtons.
        private void EndBusy()
        {
            if (IsDisposed)
                return;
            isBusy = false;
            UseWaitCursor = false;
            startVCamButton.Enabled = savedStartVCamEnabled;
            RefreshActionButtons();
        }

        // Reconciles the preview button with the resting state after a transition. The button
        // is a toggle: its label is Start/Stop Preview depending on isPreviewRunning, and it is
        // disabled entirely while the virtual camera is running — the producer owns the single
        // RTSP decode core, so a preview can never run alongside it. Not called while isBusy;
        // BeginBusy/EndBusy own the buttons during a transition.
        private void RefreshActionButtons()
        {
            playButton.Text = isPreviewRunning
                ? AppStrings.Get("Button_StopPreview")
                : AppStrings.Get("Button_StartPreview");
            playButton.Enabled = !isVCamRunning;
        }

        // Shows the modal wait dialog while `work` runs on its worker thread, and returns once
        // the work finishes. ShowDialog() pumps a nested message loop, so the UI stays live (and
        // the dialog's countdown animates) while this line blocks the calling async method.
        // With countdownSeconds > 0 the dialog counts down and returns true if it elapses before
        // the work completes (a timeout the caller must honour); otherwise it returns false.
        private bool RunWaitDialog(string message, int countdownSeconds, Task work)
        {
            if (work.IsCompleted)
                return false;

            using (WaitDialog dlg = new WaitDialog(message, countdownSeconds, work))
            {
                dlg.ShowDialog(this);
                return dlg.TimedOut;
            }
        }

        private void StatsTimer_Tick(object sender, EventArgs e)
        {
            // While a start/stop transition is running the native objects may be mid-teardown
            // on a worker thread — skip this tick rather than race it.
            if (isBusy)
                return;

            // Suspend drawing on both tables while we rewrite their cells, so the whole
            // tick produces a single repaint instead of one per changed cell (flicker).
            connList.BeginUpdate();
            statsList.BeginUpdate();
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

                // Live measured bitrate (the SDP rarely advertises one, so the probe's
                // value is usually n/a). Overrides the connection row once frames flow.
                long liveBps = isVCamRunning
                    ? (virtualCamera != null ? virtualCamera.GetBitrateBps() : 0)
                    : (videoPlayer != null ? videoPlayer.GetBitrateBps() : 0);
                if (liveBps > 0)
                    SetRow(connList, "bitrate", FormatBitrate(liveBps));

                // Reflect the engine settings currently in use.
                UpdateSettingsRows();

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
            finally
            {
                // Pair every BeginUpdate with EndUpdate (repaints once, in reverse order).
                statsList.EndUpdate();
                connList.EndUpdate();
            }
        }



        // Single place that applies every user-facing string from AppStrings (the app's own
        // 4-language resource set), so the form itself is not localized per-culture: the layout
        // lives only in MainForm.resx and just the translations differ. Runs from the ctor after
        // InitializeComponent; the thread UI culture is already set in Program.Main.
        private void ApplyLocalizedTexts()
        {
            Text = AppStrings.Get("App_Title");

            // Menu bar
            fileToolStripMenuItem.Text = AppStrings.Get("Menu_File");
            clearHistoryToolStripMenuItem.Text = AppStrings.Get("Menu_ClearHistory");
            exitToolStripMenuItem.Text = AppStrings.Get("Menu_Exit");
            settingsToolStripMenuItem.Text = AppStrings.Get("Menu_Settings");
            guideToolStripMenuItem.Text = AppStrings.Get("Menu_Guide");
            aboutToolStripMenuItem.Text = AppStrings.Get("Menu_About");

            // Source row + action buttons
            pathLabel.Text = AppStrings.Get("Label_Source");
            label2.Text = AppStrings.Get("Label_Properties");

            if (startVCamButton != null)
                startVCamButton.Text = isVCamRunning ? AppStrings.Get("Button_StopVCam") : AppStrings.Get("Button_StartVCam");

            if (playButton != null)
                playButton.Text = isPreviewRunning ? AppStrings.Get("Button_StopPreview") : AppStrings.Get("Button_StartPreview");

            if (previewStatusLabel != null && string.IsNullOrWhiteSpace(previewStatusLabel.Text))
                previewStatusLabel.Text = AppStrings.Get("Preview_Inactive");

            // Diagnostics side panel: headers and column captions
            connHeader.Text = AppStrings.Get("Header_Connection");
            statsHeader.Text = AppStrings.Get("Header_Stats");
            connPropCol.Text = AppStrings.Get("Col_Property");
            connValueCol.Text = AppStrings.Get("Col_Value");
            statsPropCol.Text = AppStrings.Get("Col_Metric");
            statsValueCol.Text = AppStrings.Get("Col_Value");

            // Diagnostics row labels. These rows are deserialized from the .resx without their
            // Name (the designer drops ListViewItem.Name on serialization) — and Name is the key
            // SetRow uses to find a row, so without this the tables never populate. Set both the
            // lookup Name and the localized label here, in the fixed designer item order.
            ApplyRowLabels(connList, new[] { "container", "transport", "codec", "pixfmt", "resolution", "fps", "bitrate", "cfgTransport", "cfgHw", "cfgTimeout", "cfgReorder", "cfgBuffer", "cfgMaxDelay", "cfgLatency" });
            ApplyRowLabels(statsList, new[] { "state", "engine", "decode", "rx", "render", "dup", "drop", "proc", "drift" });
        }

        // Assigns each ListView row its lookup Name (used by SetRow's Items.Find) and its
        // localized label (SubItems[0]) from AppStrings, matching rows to keys by index.
        private static void ApplyRowLabels(ListView lv, string[] keys)
        {
            for (int i = 0; i < keys.Length && i < lv.Items.Count; i++)
            {
                ListViewItem item = lv.Items[i];
                item.Name = keys[i];
                item.Text = AppStrings.Get("Row_" + keys[i]);
            }
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

        private async void PlayButton_Click(object sender, EventArgs e)
        {
            // Toggle: a running preview is stopped (and the panel goes black); otherwise the
            // source is validated and the preview started. The button is disabled while the
            // virtual camera runs, so this handler never fires in that state.
            if (isPreviewRunning)
            {
                BeginBusy();
                try
                {
                    await StopPreviewAsync();
                }
                finally
                {
                    EndBusy();
                }
                return;
            }

            if (string.IsNullOrWhiteSpace(pathTextBox.Text))
            {
                ShowFriendlyError(AppStrings.Get("Source_Missing_Title"), AppStrings.Get("Source_SelectPrompt"));
                return;
            }

            BeginBusy();
            try
            {
                // The probe and the RTSP open both run on a worker thread (see the async
                // helpers), so this handler awaits without ever blocking the UI thread.
                SourceProbeResult probe = await ProbeSourceWithTimeoutAsync(pathTextBox.Text);
                if (!probe.Success)
                {
                    ShowFriendlyError(AppStrings.Get("Source_Unavailable_Title"), probe.UserMessage, probe.TechnicalDetails);
                    return;
                }

                ApplyStreamInfo(probe.Streams);
                ApplyConnectionInfo(probe);

                if (await StartPreviewFromPathAsync())
                {
                    savedStartVCamEnabled = true; // applied by EndBusy()
                    RememberSuccessfulUrl(pathTextBox.Text);
                }
            }
            catch (Exception ex)
            {
                ShowFriendlyError(AppStrings.Get("Preview_Error_Title"), AppStrings.Get("Preview_Error_Start"), ex.Message);
            }
            finally
            {
                EndBusy();
            }
        }

        private async void StartVCamButton_Click(object sender, EventArgs e)
        {
            BeginBusy();
            try
            {
                if (!isVCamRunning)
                    await StartVirtualCameraAsync();
                else
                    await StopVirtualCameraAsync();
            }
            catch (Exception ex)
            {
                ShowFriendlyError(AppStrings.Get("VirtualCamera_Error_Title"), AppStrings.Get("VirtualCamera_Error_Message"), ex.Message);

                // Tear the camera down off the UI thread; the object may already be null.
                VirtualCameraWrapper failed = virtualCamera;
                virtualCamera = null;
                await DisposeCameraAsync(failed);

                isVCamRunning = false;
                startVCamButton.Text = AppStrings.Get("Button_StartVCam");
                startVCamButton.BackColor = Color.LightGreen;
            }
            finally
            {
                EndBusy();
            }
        }

        // Result of the off-thread virtual-camera start sequence.
        private enum VCamStartOutcome { Started, RegisterFailed, StartFailed }

        private sealed class VCamStartResult
        {
            public VCamStartOutcome Outcome;
            public VirtualCameraWrapper Camera; // created on the worker thread; owned by the caller
            public bool ProducerStarted;
        }

        // Starts the virtual camera. All the blocking native work — creating the MF virtual
        // camera, Register(), Start() (the Frame Server loads the DLL cross-process), and the
        // FFmpeg producer opening the RTSP source — runs on a worker thread. The camera object
        // is created and driven entirely on that one worker thread (consistent apartment); it is
        // only handed back to the UI thread as a field once fully started.
        private async Task StartVirtualCameraAsync()
        {
            SourceProbeResult probe = await ProbeSourceWithTimeoutAsync(pathTextBox.Text);
            if (!probe.Success)
            {
                ShowFriendlyError(AppStrings.Get("Source_Unavailable_Title"), probe.UserMessage, probe.TechnicalDetails);
                return;
            }

            ApplyStreamInfo(probe.Streams);
            ApplyConnectionInfo(probe);

            VCamConfig config = new VCamConfig();
            config.RtspUrl = pathTextBox.Text ?? string.Empty;
            config.Width = streamInfo.width;
            config.Height = streamInfo.height;
            config.FpsNum = streamInfo.fpsNum;
            config.FpsDen = streamInfo.fpsDen;
            config.Format = streamInfo.subtype;
            config.Overlay = Settings.Current.FrameCounterOverlay ? 1u : 0u;

            Task<VCamStartResult> startTask = Task.Run(delegate ()
            {
                VirtualCameraWrapper cam = new VirtualCameraWrapper();
                cam.SetCameraName("RTSP Virtual Camera");
                cam.SetConfig(config);

                if (!cam.Register())
                    return new VCamStartResult { Outcome = VCamStartOutcome.RegisterFailed, Camera = cam };

                if (!cam.Start())
                    return new VCamStartResult { Outcome = VCamStartOutcome.StartFailed, Camera = cam };

                // The Frame Server never opens the RTSP source itself — the app decodes with
                // FFmpeg and streams frames to it. Start the user-space producer now that the
                // camera is running, then stop the preview (they share the single decode core).
                bool producer = cam.StartFfmpegProducer(
                    config.RtspUrl, config.Width, config.Height, config.FpsNum, config.FpsDen);
                videoPlayer.Stop();

                return new VCamStartResult { Outcome = VCamStartOutcome.Started, Camera = cam, ProducerStarted = producer };
            });

            RunWaitDialog(AppStrings.Get("Wait_Starting"), 0, startTask);
            VCamStartResult result = await startTask;

            switch (result.Outcome)
            {
                case VCamStartOutcome.RegisterFailed:
                    ShowFriendlyError(
                        AppStrings.Get("VirtualCamera_RegisterFail_Title"),
                        AppStrings.Get("VirtualCamera_RegisterFail_Message"),
                        AppStrings.Get("VirtualCamera_RegisterFail_Details"));
                    await DisposeCameraAsync(result.Camera);
                    return;

                case VCamStartOutcome.StartFailed:
                    ShowFriendlyError(
                        AppStrings.Get("VirtualCamera_StartFail_Title"),
                        AppStrings.Get("VirtualCamera_StartFail_Message"),
                        AppStrings.Get("VirtualCamera_StartFail_Details"));
                    await DisposeCameraAsync(result.Camera);
                    return;
            }

            System.Diagnostics.Debug.WriteLine("Virtual camera registered and started");
            if (!result.ProducerStarted)
                System.Diagnostics.Debug.WriteLine("FFmpeg producer failed to start");

            virtualCamera = result.Camera;
            RememberSuccessfulUrl(config.RtspUrl);
            SetPreviewStatus(AppStrings.Get("Preview_VCamStarted"));

            isVCamRunning = true;
            isPreviewRunning = false; // the worker stopped the preview when the producer started
            startVCamButton.Text = AppStrings.Get("Button_StopVCam");
            startVCamButton.BackColor = Color.LightCoral;
            savedStartVCamEnabled = true; // applied by EndBusy()
        }

        // Stops the virtual camera. The producer thread join, the Frame Server teardown, and
        // the preview restart all run on a worker thread so the UI stays responsive.
        private async Task StopVirtualCameraAsync()
        {
            VirtualCameraWrapper cam = virtualCamera;
            virtualCamera = null;

            Task stopTask = Task.Run(delegate ()
            {
                try { videoPlayer.Stop(); }
                catch (Exception ex) { System.Diagnostics.Debug.WriteLine("Error stopping preview: " + ex.Message); }

                if (cam != null)
                {
                    cam.StopFfmpegProducer(); // no-op unless the FFmpeg engine was running
                    cam.Stop();
                    cam.Unregister();
                    cam.Dispose();
                }
            });

            RunWaitDialog(AppStrings.Get("Wait_Stopping"), 0, stopTask);
            await stopTask;

            isVCamRunning = false;
            startVCamButton.Text = AppStrings.Get("Button_StartVCam");
            startVCamButton.BackColor = Color.LightGreen;
            savedStartVCamEnabled = true; // applied by EndBusy()

            if (!string.IsNullOrEmpty(pathTextBox.Text))
            {
                previewStatusLabel.Visible = false;
                await StartPreviewFromPathAsync();
            }
            else
            {
                isPreviewRunning = false;
                SetPreviewStatus(AppStrings.Get("Preview_Inactive"));
            }

            System.Diagnostics.Debug.WriteLine("Virtual camera stopped");
            MessageBox.Show(AppStrings.Get("VirtualCamera_Stopped"), AppStrings.Get("Info_Title"), MessageBoxButtons.OK, MessageBoxIcon.Information);
        }

        // Disposes a virtual-camera instance off the UI thread (Dispose joins the producer
        // decode thread and tears down the cross-process Frame Server session — both blocking).
        private static Task DisposeCameraAsync(VirtualCameraWrapper camera)
        {
            if (camera == null)
                return Task.CompletedTask;
            return Task.Run(delegate () { camera.Dispose(); });
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

        // Outcome of the off-thread preview open sequence (Stop / SetVideoPath / Initialize /
        // Play all block on RTSP I/O, so they run on a worker thread).
        private enum PreviewStartStage { Ok, InitFailed, PlayFailed }

        private sealed class PreviewStartResult
        {
            public PreviewStartStage Stage;
            public StreamInfo[] Streams;
        }

        private async Task<bool> StartPreviewFromPathAsync()
        {
            string path = pathTextBox.Text;
            previewStatusLabel.Visible = false;

            Task<PreviewStartResult> openTask = Task.Run(delegate ()
            {
                videoPlayer.Stop();
                videoPlayer.SetVideoPath(path);

                if (!videoPlayer.Initialize())
                    return new PreviewStartResult { Stage = PreviewStartStage.InitFailed };

                // Read stream info after Initialize but before Play, matching the original order.
                StreamInfo[] infos = videoPlayer.GetStreamInfos();
                if (!videoPlayer.Play())
                    return new PreviewStartResult { Stage = PreviewStartStage.PlayFailed, Streams = infos };

                return new PreviewStartResult { Stage = PreviewStartStage.Ok, Streams = infos };
            });

            RunWaitDialog(AppStrings.Get("Wait_Opening"), 0, openTask);
            PreviewStartResult result = await openTask;

            if (result.Stage == PreviewStartStage.InitFailed)
            {
                isPreviewRunning = false;
                ShowFriendlyError(
                    AppStrings.Get("Preview_OpenFail_Title"),
                    AppStrings.Get("Preview_OpenFail_Message"),
                    AppStrings.Get("Preview_OpenFail_Details"));
                return false;
            }

            if (result.Stage == PreviewStartStage.PlayFailed)
            {
                isPreviewRunning = false;
                ShowFriendlyError(
                    AppStrings.Get("Preview_PlayFail_Title"),
                    AppStrings.Get("Preview_PlayFail_Message"),
                    AppStrings.Get("Preview_PlayFail_Details"));
                return false;
            }

            ApplyStreamInfo(result.Streams);
            isPreviewRunning = true;
            return true;
        }

        // Stops a running preview (invoked by the preview button's toggle). The decode thread
        // join runs on a worker thread; afterwards the panel is forced black with the "preview
        // not active" status so no leftover frame lingers on the GDI surface.
        private async Task StopPreviewAsync()
        {
            Task stopTask = Task.Run(delegate ()
            {
                try { videoPlayer.Stop(); }
                catch (Exception ex) { System.Diagnostics.Debug.WriteLine("Error stopping preview: " + ex.Message); }
            });

            RunWaitDialog(AppStrings.Get("Wait_Stopping"), 0, stopTask);
            await stopTask;

            isPreviewRunning = false;
            SetPreviewStatus(AppStrings.Get("Preview_Inactive"));
        }

        private void SetPreviewStatus(string message)
        {
            // The FFmpeg preview blits frames straight onto videoPanel's HWND via GDI; once it
            // stops, the last decoded frame stays on the surface. Force the panel to repaint its
            // black background so no leftover preview image shows behind the status text.
            videoPanel.Invalidate();
            videoPanel.Update();

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

        private async Task<SourceProbeResult> ProbeSourceWithTimeoutAsync(string path)
        {
            string validationError = ValidateSourcePath(path);
            if (!string.IsNullOrEmpty(validationError))
            {
                return FailProbe(validationError, AppStrings.Get("Probe_Validation_Details"));
            }

            IntPtr previewHandle = videoPanel.Handle; // must be read on the UI thread

            Task<SourceProbeResult> probeTask = Task.Run(delegate () { return ProbeSource(path, previewHandle); });

            // Show the modal wait dialog with an 8 s countdown while the probe runs on its
            // worker. It closes the moment the probe finishes; if the countdown elapses first it
            // reports a timeout and we abandon the probe (which disposes its own `using`
            // VideoPlayerWrapper when it eventually returns, so this is safe).
            bool timedOut = RunWaitDialog(AppStrings.Get("Wait_Connecting"), ProbeTimeoutMs / 1000, probeTask);
            if (timedOut)
            {
                return FailProbe(
                    AppStrings.Get("Probe_Timeout_Message"),
                    string.Format(AppStrings.Get("Probe_Timeout_Details"), ProbeTimeoutMs));
            }

            try
            {
                return await probeTask;
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

        // Fills the source ComboBox with the persisted URL history (most recent first) and
        // sets `current` as the shown text. AutoCompleteSource is ListItems, so the type-ahead
        // draws from these same items — no separate collection to keep in sync.
        private void PopulateUrlHistory(string current)
        {
            pathTextBox.BeginUpdate();
            try
            {
                pathTextBox.Items.Clear();
                foreach (string url in Settings.Current.RecentUrls)
                    pathTextBox.Items.Add(url);
            }
            finally
            {
                pathTextBox.EndUpdate();
            }

            pathTextBox.Text = current;
        }

        // Records a source that just connected successfully: moves it to the front of the
        // history, updates the single "last used" URL, persists, and refreshes the dropdown.
        private void RememberSuccessfulUrl(string url)
        {
            if (string.IsNullOrWhiteSpace(url))
                return;

            Settings.Current.AddRecentUrl(url);
            try { Settings.Current.RtspURL = new Uri(url); }
            catch { /* not a valid Uri (e.g. odd local path) — keep the previous last-used */ }
            Settings.Current.Save();

            PopulateUrlHistory(url);
        }

        // File → Clear address history: empties the persisted URL list and the dropdown,
        // keeping whatever is currently typed. RtspURL (autostart target) is left untouched.
        private void clearHistoryToolStripMenuItem_Click(object sender, EventArgs e)
        {
            Settings.Current.RecentUrls.Clear();
            Settings.Current.Save();
            PopulateUrlHistory(pathTextBox.Text);
        }

        private void MainForm_Load(object sender, EventArgs e)
        {
            Settings.Load();
            // Push the persisted FFmpeg engine options (transport, hardware decode, and
            // the numeric tuning params) into RTCamNative before any connection is opened.
            VirtualCameraWrapper.ApplyEngineSettings();
            // All menu/label/header text is applied from AppStrings in ApplyLocalizedTexts
            // (called from the ctor); the form is no longer localized per-culture.
            string lastUrl = Settings.Current.RtspURL?.ToString() ?? "rtsp://example.io:1234/webcam";
            PopulateUrlHistory(lastUrl);
            // Autostart is handled in MainForm_Shown (after the window is visible) on a
            // background thread, so an unreachable source never blocks the UI thread.
        }

        // Runs after the window is first shown. If autostart is configured, probe the source
        // and start the preview through the same async helpers the buttons use — the blocking
        // native work runs on worker threads and the awaits marshal back to the UI thread, so
        // an unreachable source never blocks the UI thread.
        private async void MainForm_Shown(object sender, EventArgs e)
        {
            if (Settings.Current.RtspURL == null || !Settings.Current.AutoStart)
                return;

            string path = pathTextBox.Text;
            if (string.IsNullOrWhiteSpace(path))
                return;

            BeginBusy();
            try
            {
                SourceProbeResult probe = await ProbeSourceWithTimeoutAsync(path);
                if (!probe.Success)
                {
                    SetPreviewStatus(AppStrings.Get("Preview_Inactive"));
                    ShowFriendlyError(AppStrings.Get("Source_Unavailable_Title"), probe.UserMessage, probe.TechnicalDetails);
                    return;
                }

                ApplyStreamInfo(probe.Streams);
                ApplyConnectionInfo(probe);
                if (await StartPreviewFromPathAsync())
                    savedStartVCamEnabled = true; // applied by EndBusy()
            }
            catch (Exception ex)
            {
                ShowFriendlyError(AppStrings.Get("Preview_Error_Title"), AppStrings.Get("Preview_Error_Start"), ex.Message);
            }
            finally
            {
                EndBusy();
            }
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

        private void guideToolStripMenuItem_Click(object sender, EventArgs e)
        {
            using (EngineGuideForm guide = new EngineGuideForm())
            {
                guide.ShowDialog(this);
            }
        }

        private void aboutToolStripMenuItem_Click(object sender, EventArgs e)
        {
            About formAbout = new About();
            formAbout.ShowDialog(this);
        }
    }
}
