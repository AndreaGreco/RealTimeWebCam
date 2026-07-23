using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    /// <summary>
    /// In-app mini guide to the tunable FFmpeg engine options. One scrollable section per
    /// option, each with a fuller explanation and a link to the relevant FFmpeg
    /// documentation. Built from the shared <see cref="EngineOptionHelp"/> table so it
    /// stays in sync with the settings dialog. Opened standalone from the main menu, or
    /// anchored to a specific option via the "?" glyph in the settings dialog
    /// (<see cref="InitialOptionKey"/>).
    /// </summary>
    public partial class EngineGuideForm : Form
    {
        // Set before ShowDialog to scroll straight to one option's section on open.
        // Runtime-only, never designer-serialized (it's a Form property).
        [System.ComponentModel.Browsable(false)]
        [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
        public string InitialOptionKey { get; set; }

        // Maps an option key to its section header control, for scroll-to-section.
        private readonly Dictionary<string, Control> _sections = new Dictionary<string, Control>();

        public EngineGuideForm()
        {
            InitializeComponent();
        }

        private void EngineGuideForm_Load(object sender, EventArgs e)
        {
            Text = AppStrings.Get("Guide_Title");
            headerLabel.Text = AppStrings.Get("Guide_Title");
            introLabel.Text = AppStrings.Get("Guide_Intro");
            closeButton.Text = AppStrings.Get("Button_Close");

            BuildSections();

            if (!string.IsNullOrEmpty(InitialOptionKey))
            {
                ScrollToOption(InitialOptionKey);
            }
        }

        // Text wraps at this width (panel width minus padding and the vertical scrollbar).
        private const int TextWidth = 508;

        private void BuildSections()
        {
            contentPanel.SuspendLayout();
            foreach (EngineOptionHelp help in EngineOptionHelp.All)
            {
                Label title = new Label
                {
                    AutoSize = true,
                    Font = new Font("Segoe UI", 9.75F, FontStyle.Bold),
                    Margin = new Padding(3, 14, 3, 2),
                    MaximumSize = new Size(TextWidth, 0),
                    Text = AppStrings.Get(help.TitleKey),
                };
                contentPanel.Controls.Add(title);
                _sections[help.Key] = title;

                Label body = new Label
                {
                    AutoSize = true,
                    Margin = new Padding(3, 0, 3, 2),
                    MaximumSize = new Size(TextWidth, 0),
                    Text = AppStrings.Get(help.BodyKey),
                };
                contentPanel.Controls.Add(body);

                if (!string.IsNullOrEmpty(help.DocUrl))
                {
                    LinkLabel link = new LinkLabel
                    {
                        AutoSize = true,
                        Margin = new Padding(3, 0, 3, 6),
                        Text = AppStrings.Get("Guide_FfmpegDocLink"),
                        Tag = help.DocUrl,
                    };
                    link.LinkClicked += DocLink_LinkClicked;
                    contentPanel.Controls.Add(link);
                }
            }
            contentPanel.ResumeLayout(true);
        }

        private void ScrollToOption(string key)
        {
            if (_sections.TryGetValue(key, out Control section))
            {
                contentPanel.ScrollControlIntoView(section);
            }
        }

        private void DocLink_LinkClicked(object sender, LinkLabelLinkClickedEventArgs e)
        {
            if (sender is LinkLabel link && link.Tag is string url && !string.IsNullOrEmpty(url))
            {
                try
                {
                    Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
                }
                catch
                {
                    // Non-critical: if no browser can be launched there is nothing more
                    // useful to do than leave the link showing.
                }
            }
        }
    }
}
