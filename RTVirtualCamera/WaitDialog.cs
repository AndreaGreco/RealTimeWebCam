using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    /// <summary>
    /// A small modal "please wait" dialog shown while a blocking operation runs on a worker
    /// thread. It watches a <see cref="Task"/> and closes itself the moment that task finishes.
    ///
    /// With a positive countdown it shows a determinate progress bar filling toward a timeout,
    /// a "Timeout in N s" label ticking down, and — if the countdown elapses before the task
    /// completes — it closes with <see cref="TimedOut"/> set (the caller then abandons the work,
    /// exactly the old ProbeTimeout behaviour, now made visible). With countdown &lt;= 0 it shows
    /// a marquee (indeterminate) bar and no countdown, and only closes when the task completes.
    ///
    /// Everything runs on the UI thread: while ShowDialog() pumps its nested message loop, a
    /// WinForms timer polls the task and updates the bar. No cross-thread Invoke is needed.
    /// </summary>
    internal sealed class WaitDialog : Form
    {
        private const int ProgressResolution = 1000; // progress bar 0..1000 for smooth fill

        private readonly Task work;
        private readonly int timeoutMs; // 0 => no countdown / no timeout (marquee only)
        private readonly System.Windows.Forms.Timer pollTimer;
        private readonly ProgressBar progressBar;
        private readonly Label countdownLabel;

        private long shownTick;

        /// <summary>True if the countdown elapsed before <c>work</c> completed.</summary>
        public bool TimedOut { get; private set; }

        public WaitDialog(string message, int timeoutSeconds, Task work)
        {
            this.work = work ?? throw new ArgumentNullException(nameof(work));
            this.timeoutMs = timeoutSeconds > 0 ? timeoutSeconds * 1000 : 0;

            // A fixed, chrome-less dialog: no close/min/max box, not in the taskbar, centered
            // on the owner — the user cannot dismiss it; it goes away on its own.
            Text = AppStrings.Get("Wait_Title");
            FormBorderStyle = FormBorderStyle.FixedDialog;
            ControlBox = false;
            MinimizeBox = false;
            MaximizeBox = false;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.CenterParent;
            ClientSize = new Size(380, 118);

            Label messageLabel = new Label
            {
                Text = message,
                AutoSize = false,
                TextAlign = ContentAlignment.MiddleLeft,
                Location = new Point(16, 16),
                Size = new Size(348, 24),
            };

            progressBar = new ProgressBar
            {
                Location = new Point(16, 48),
                Size = new Size(348, 22),
            };

            countdownLabel = new Label
            {
                AutoSize = false,
                TextAlign = ContentAlignment.MiddleRight,
                Location = new Point(16, 78),
                Size = new Size(348, 22),
            };

            if (timeoutMs > 0)
            {
                progressBar.Style = ProgressBarStyle.Continuous;
                progressBar.Minimum = 0;
                progressBar.Maximum = ProgressResolution;
            }
            else
            {
                progressBar.Style = ProgressBarStyle.Marquee;
                progressBar.MarqueeAnimationSpeed = 30;
                countdownLabel.Visible = false;
            }

            Controls.Add(messageLabel);
            Controls.Add(progressBar);
            Controls.Add(countdownLabel);

            pollTimer = new System.Windows.Forms.Timer { Interval = 100 };
            pollTimer.Tick += PollTimer_Tick;
        }

        protected override void OnShown(EventArgs e)
        {
            base.OnShown(e);
            shownTick = Environment.TickCount64;
            pollTimer.Start();
        }

        private void PollTimer_Tick(object sender, EventArgs e)
        {
            // The work finished (completed, faulted, or cancelled) — the caller re-awaits the
            // task to observe the result/exception, so here we just dismiss the dialog.
            if (work.IsCompleted)
            {
                CloseWith(false);
                return;
            }

            if (timeoutMs <= 0)
                return; // marquee-only: nothing to count down

            long elapsed = Environment.TickCount64 - shownTick;
            int remainingMs = timeoutMs - (int)elapsed;
            if (remainingMs <= 0)
            {
                CloseWith(true);
                return;
            }

            int value = (int)(elapsed * ProgressResolution / timeoutMs);
            progressBar.Value = value < 0 ? 0 : (value > ProgressResolution ? ProgressResolution : value);

            int remainingSec = (remainingMs + 999) / 1000; // ceil so it reads 8..1, never 0 while waiting
            countdownLabel.Text = string.Format(AppStrings.Get("Wait_TimeoutIn"), remainingSec);
        }

        private void CloseWith(bool timedOut)
        {
            TimedOut = timedOut;
            pollTimer.Stop();
            Close();
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                pollTimer.Stop();
                pollTimer.Dispose();
            }
            base.Dispose(disposing);
        }
    }
}
