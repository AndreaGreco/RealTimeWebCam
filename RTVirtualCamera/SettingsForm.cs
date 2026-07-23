using System;
using System.Drawing;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    public partial class SettingsForm : Form
    {
        // Shared tooltip for every "?" help glyph. Longer AutoPopDelay because the tips
        // are full sentences, not one-word hints.
        private readonly ToolTip _helpTip = new ToolTip
        {
            InitialDelay = 300,
            ReshowDelay = 100,
            AutoPopDelay = 20000,
            ShowAlways = true,
        };

        public SettingsForm()
        {
            InitializeComponent();
        }

        private sealed class LanguageOption
        {
            public LanguageOption(string text, string code)
            {
                Text = text;
                Code = code;
            }

            public string Text { get; private set; }
            public string Code { get; private set; }

            public override string ToString()
            {
                return Text;
            }
        }

        private void LanguageChanged(LanguageOption option)
        {
            string currentLanguage = Settings.Current.Language ?? string.Empty;
            if (string.Equals(currentLanguage, option.Code ?? string.Empty, StringComparison.OrdinalIgnoreCase))
            {
                return;
            }

            Settings.Current.Language = option.Code ?? string.Empty;
            Settings.Current.Save();

            MessageBox.Show(
                AppStrings.Get("Language_Updated_Message"),
                AppStrings.Get("Language_Updated_Title"),
                MessageBoxButtons.OK,
                MessageBoxIcon.Information);
        }

        private void LanguageComboBox_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (languageComboBox.SelectedItem is LanguageOption option)
            {
                LanguageChanged(option);
            }
        }

        private sealed class TransportOption
        {
            public TransportOption(string text, RtspTransportMode mode)
            {
                Text = text;
                Mode = mode;
            }

            public string Text { get; private set; }
            public RtspTransportMode Mode { get; private set; }

            public override string ToString()
            {
                return Text;
            }
        }

        // Saves the settings and pushes them to the engine; the change takes effect on
        // the next preview/producer connection.
        private void SaveAndApply()
        {
            Settings.Current.Save();
            VirtualCameraWrapper.ApplyEngineSettings();
        }

        private void TransportComboBox_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (transportComboBox.SelectedItem is TransportOption option)
            {
                Settings.Current.RtspTransport = option.Mode;
                SaveAndApply();
            }
        }

        private void HardwareDecodeCheckBox_CheckedChanged(object sender, EventArgs e)
        {
            Settings.Current.HardwareDecode = HardwareDecodeCheckBox.Checked;
            SaveAndApply();
        }

        private void SocketTimeoutNumeric_ValueChanged(object sender, EventArgs e)
        {
            Settings.Current.SocketTimeoutMs = (int)socketTimeoutNumeric.Value;
            SaveAndApply();
        }

        private void ReorderNumeric_ValueChanged(object sender, EventArgs e)
        {
            Settings.Current.ReorderQueueSize = (int)reorderNumeric.Value;
            SaveAndApply();
        }

        // The control shows kilobytes; the setting is stored in bytes.
        private void BufferSizeNumeric_ValueChanged(object sender, EventArgs e)
        {
            Settings.Current.UdpBufferSize = (int)bufferSizeNumeric.Value * 1024;
            SaveAndApply();
        }

        private void MaxDelayNumeric_ValueChanged(object sender, EventArgs e)
        {
            Settings.Current.MaxDelayMs = (int)maxDelayNumeric.Value;
            SaveAndApply();
        }

        private void LatencyCapNumeric_ValueChanged(object sender, EventArgs e)
        {
            Settings.Current.LatencyCapMs = (int)latencyCapNumeric.Value;
            SaveAndApply();
        }

        // Clamps v into [min,max] so a value persisted outside the control's range (e.g.
        // hand-edited settings.json) doesn't throw when assigned to NumericUpDown.Value.
        private static decimal Clamp(int v, System.Windows.Forms.NumericUpDown ctl)
        {
            if (v < ctl.Minimum) return ctl.Minimum;
            if (v > ctl.Maximum) return ctl.Maximum;
            return v;
        }

        private void SettingsForm_Load(object sender, EventArgs e)
        {
            Text = AppStrings.Get("Settings_Title");
            languageSectionLabel.Text = AppStrings.Get("Menu_Language");
            generalSectionLabel.Text = AppStrings.Get("Settings_GeneralSection");
            AutoStartCheckBox.Text = AppStrings.Get("Settings_AutoStart");
            OverlayCheckBox.Text = AppStrings.Get("Settings_FrameCounterOverlay");
            networkSectionLabel.Text = AppStrings.Get("Settings_NetworkSection");
            transportLabel.Text = AppStrings.Get("Settings_Transport");
            HardwareDecodeCheckBox.Text = AppStrings.Get("Settings_HardwareDecode");
            socketTimeoutLabel.Text = AppStrings.Get("Settings_SocketTimeout");
            reorderLabel.Text = AppStrings.Get("Settings_ReorderQueue");
            bufferSizeLabel.Text = AppStrings.Get("Settings_UdpBufferSize");
            maxDelayLabel.Text = AppStrings.Get("Settings_MaxDelay");
            latencyCapLabel.Text = AppStrings.Get("Settings_LatencyCap");
            closeButton.Text = AppStrings.Get("Button_Close");

            languageComboBox.Items.Clear();
            languageComboBox.Items.Add(new LanguageOption(AppStrings.Get("Language_System"), string.Empty));
            languageComboBox.Items.Add(new LanguageOption("Italiano", "it-IT"));
            languageComboBox.Items.Add(new LanguageOption("English", "en"));
            languageComboBox.Items.Add(new LanguageOption("Español", "es"));
            languageComboBox.Items.Add(new LanguageOption("Deutsch", "de"));

            string currentLanguage = Settings.Current.Language ?? string.Empty;
            int selectedIndex = 0;
            for (int i = 0; i < languageComboBox.Items.Count; i++)
            {
                var option = (LanguageOption)languageComboBox.Items[i];
                if (string.Equals(option.Code, currentLanguage, StringComparison.OrdinalIgnoreCase))
                {
                    selectedIndex = i;
                    break;
                }
            }

            // Set before wiring the event so opening the dialog doesn't itself
            // trigger a "language updated" prompt.
            languageComboBox.SelectedIndex = selectedIndex;
            languageComboBox.SelectedIndexChanged += LanguageComboBox_SelectedIndexChanged;

            transportComboBox.Items.Clear();
            transportComboBox.Items.Add(new TransportOption(AppStrings.Get("Settings_Transport_Auto"), RtspTransportMode.Auto));
            transportComboBox.Items.Add(new TransportOption(AppStrings.Get("Settings_Transport_Udp"), RtspTransportMode.Udp));
            transportComboBox.Items.Add(new TransportOption(AppStrings.Get("Settings_Transport_Tcp"), RtspTransportMode.Tcp));

            int transportIndex = 0;
            for (int i = 0; i < transportComboBox.Items.Count; i++)
            {
                if (((TransportOption)transportComboBox.Items[i]).Mode == Settings.Current.RtspTransport)
                {
                    transportIndex = i;
                    break;
                }
            }

            // Set the selection before wiring the event so loading the dialog doesn't
            // itself re-save/re-apply the value.
            transportComboBox.SelectedIndex = transportIndex;
            transportComboBox.SelectedIndexChanged += TransportComboBox_SelectedIndexChanged;

            AutoStartCheckBox.Checked = Settings.Current.AutoStart;
            OverlayCheckBox.Checked = Settings.Current.FrameCounterOverlay;
            HardwareDecodeCheckBox.Checked = Settings.Current.HardwareDecode;

            // Load the persisted values (clamped to each control's range). The ValueChanged
            // handlers are wired in InitializeComponent, so assigning a value that differs
            // from the control's current one fires SaveAndApply once here — harmless (it is
            // idempotent and shows no dialog, unlike the language combo).
            socketTimeoutNumeric.Value = Clamp(Settings.Current.SocketTimeoutMs, socketTimeoutNumeric);
            reorderNumeric.Value = Clamp(Settings.Current.ReorderQueueSize, reorderNumeric);
            bufferSizeNumeric.Value = Clamp(Settings.Current.UdpBufferSize / 1024, bufferSizeNumeric);
            maxDelayNumeric.Value = Clamp(Settings.Current.MaxDelayMs, maxDelayNumeric);
            latencyCapNumeric.Value = Clamp(Settings.Current.LatencyCapMs, latencyCapNumeric);

            // "?" help glyphs next to each engine option (built after the labels have
            // their final localized text, so anchor.Right is accurate). Hover shows a
            // short tip; click opens the guide anchored to that option's section.
            AddHelpGlyph(transportLabel, "transport");
            AddHelpGlyph(HardwareDecodeCheckBox, "hwdecode");
            AddHelpGlyph(socketTimeoutLabel, "sockettimeout");
            AddHelpGlyph(reorderLabel, "reorder");
            AddHelpGlyph(bufferSizeLabel, "buffersize");
            AddHelpGlyph(maxDelayLabel, "maxdelay");
            AddHelpGlyph(latencyCapLabel, "latencycap");
        }

        // Creates a clickable "?" glyph immediately to the right of an option's label.
        private void AddHelpGlyph(Control anchor, string optionKey)
        {
            EngineOptionHelp help = EngineOptionHelp.Find(optionKey);
            if (help == null)
            {
                return;
            }

            Label glyph = new Label
            {
                AutoSize = true,
                Text = "(?)",
                Font = new Font(Font, FontStyle.Bold),
                ForeColor = Color.FromArgb(0, 102, 204),
                Cursor = Cursors.Hand,
                Location = new Point(anchor.Right + 4, anchor.Top),
                Tag = optionKey,
            };
            _helpTip.SetToolTip(glyph, AppStrings.Get(help.TipKey));
            glyph.Click += HelpGlyph_Click;
            Controls.Add(glyph);
            glyph.BringToFront();
        }

        private void HelpGlyph_Click(object sender, EventArgs e)
        {
            if (sender is Control c && c.Tag is string key)
            {
                using (EngineGuideForm guide = new EngineGuideForm { InitialOptionKey = key })
                {
                    guide.ShowDialog(this);
                }
            }
        }

        private void AutoStartCheckBox_CheckedChanged(object sender, EventArgs e)
        {
            Settings.Current.AutoStart = AutoStartCheckBox.Checked;
            Settings.Current.Save();
        }

        private void OverlayCheckBox_CheckedChanged(object sender, EventArgs e)
        {
            Settings.Current.FrameCounterOverlay = OverlayCheckBox.Checked;
            Settings.Current.Save();
        }
    }
}
