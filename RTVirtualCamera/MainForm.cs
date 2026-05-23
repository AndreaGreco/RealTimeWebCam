using System;
using System.Drawing;
using System.Windows.Forms;

namespace TestVideo
{
    public partial class MainForm : Form
    {
        private VideoPlayerWrapper videoPlayer;
        private VirtualCameraWrapper virtualCamera;
        private bool isVCamRunning = false;
        private StreamInfo streamInfo;

        public MainForm()
        {
            InitializeComponent();
            InitializeVideoPlayer();

            // Handle resize events to update video position
            videoPanel.Resize += VideoPanel_Resize;
        }

        private void VideoPanel_Resize(object sender, EventArgs e)
        {
            try
            {
                videoPlayer?.SetWindowHandle(videoPanel);
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine($"Error updating video position: {ex.Message}");
            }
        }

        private void InitializeVideoPlayer()
        {
            try
            {
                videoPlayer = new VideoPlayerWrapper();
                videoPlayer.SetWindowHandle(videoPanel);
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Failed to initialize video player: {ex.Message}");
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
                if (!string.IsNullOrEmpty(pathTextBox.Text))
                {
                    videoPlayer.SetVideoPath(pathTextBox.Text);

                    bool init = videoPlayer.Initialize();
                    if (init)
                    {
                        StreamInfo[] infos = videoPlayer.GetStreamInfos();
                        videoPlayer.Play();

                        foreach (var info in infos)
                        {
                            string msg;

                            msg = string.Format($"Resolution:{info.width}x{info.height}@{info.getFSP()}");

                            this.streamProp_lbl.Text = msg;
                        }

                        this.streamInfo = infos[0];
                        this.startVCamButton.Enabled = true;
                    }
                    else
                    {
                        MessageBox.Show("Initialize failed");
                    }
                }
                else
                {
                    MessageBox.Show("Please select a video file first");
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error playing video: {ex.Message}");
            }
        }

        private void StartVCamButton_Click(object sender, EventArgs e)
        {
            try
            {
                if (!isVCamRunning)
                {
                    // Create and start virtual camera
                    virtualCamera = new VirtualCameraWrapper();
                    virtualCamera.SetCameraName("RTSP Virtual Camera");

                    // Build config from the path text box; resolution/fps from probed info if available
                    var config = new VCamConfig
                    {
                        RtspUrl = pathTextBox.Text ?? string.Empty,
                        Width = this.streamInfo.width,
                        Height = this.streamInfo.height,
                        FpsNum = this.streamInfo.fpsNum,
                        FpsDen = this.streamInfo.fpsDen,
                        Format = this.streamInfo.subtype,
                    };
                    virtualCamera.SetConfig(config);

                    if (virtualCamera.Register())
                    {
                        System.Diagnostics.Debug.WriteLine("Virtual camera registered");

                        if (virtualCamera.Start())
                        {
                            isVCamRunning = true;
                            startVCamButton.Text = "Stop VCam";
                            startVCamButton.BackColor = Color.LightCoral;
                        }
                        else
                        {
                            MessageBox.Show("Failed to start virtual camera", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                            virtualCamera?.Dispose();
                            virtualCamera = null;
                        }
                    }
                    else
                    {
                        MessageBox.Show(
                            "Failed to register virtual camera.\n\n" +
                            "Make sure:\n" +
                            "1. VCamSampleSource.dll is registered (regsvr32)\n" +
                            "2. You are running Windows 11 21H2 or later\n" +
                            "3. Media Foundation is properly initialized",
                            "Error",
                            MessageBoxButtons.OK,
                            MessageBoxIcon.Error);
                        virtualCamera?.Dispose();
                        virtualCamera = null;
                    }
                }
                else
                {
                    // Stop virtual camera
                    if (virtualCamera != null)
                    {
                        virtualCamera.Stop();
                        virtualCamera.Unregister();
                        virtualCamera.Dispose();
                        virtualCamera = null;
                    }

                    isVCamRunning = false;
                    startVCamButton.Text = "Start VCam";
                    startVCamButton.BackColor = Color.LightGreen;

                    System.Diagnostics.Debug.WriteLine("Virtual camera stopped");
                    MessageBox.Show("Virtual Camera stopped", "Info", MessageBoxButtons.OK, MessageBoxIcon.Information);
                }
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error managing virtual camera: {ex.Message}\n\n{ex.StackTrace}", "Error", MessageBoxButtons.OK, MessageBoxIcon.Error);

                // Cleanup on error
                virtualCamera?.Dispose();
                virtualCamera = null;
                isVCamRunning = false;
                startVCamButton.Text = "Start VCam";
                startVCamButton.BackColor = Color.LightGreen;
            }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            // Stop virtual camera if running
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
                    System.Diagnostics.Debug.WriteLine($"Error disposing virtual camera: {ex.Message}");
                }
            }

            videoPlayer?.Dispose();
            base.OnFormClosed(e);
        }
    }
}
