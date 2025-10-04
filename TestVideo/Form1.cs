using System;
using System.Windows.Forms;

namespace TestVideo
{
    public partial class Form1 : Form
    {
        private VideoPlayerWrapper videoPlayer;

        public Form1()
        {
            InitializeComponent();
            InitializeVideoPlayer();
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

                    if (videoPlayer.Initialize())
                    {
                        videoPlayer.Play();
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

        private void PauseButton_Click(object sender, EventArgs e)
        {
            try
            {
                videoPlayer.Pause();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error pausing video: {ex.Message}");
            }
        }

        private void StopButton_Click(object sender, EventArgs e)
        {
            try
            {
                videoPlayer.Stop();
            }
            catch (Exception ex)
            {
                MessageBox.Show($"Error stopping video: {ex.Message}");
            }
        }

        protected override void OnFormClosed(FormClosedEventArgs e)
        {
            videoPlayer?.Dispose();
            base.OnFormClosed(e);
        }
    }
}
