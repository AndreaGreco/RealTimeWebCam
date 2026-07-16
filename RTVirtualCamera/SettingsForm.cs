using System;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    public partial class SettingsForm : Form
    {
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

        private void SettingsForm_Load(object sender, EventArgs e)
        {
            Text = AppStrings.Get("Settings_Title");
            languageSectionLabel.Text = AppStrings.Get("Menu_Language");
            generalSectionLabel.Text = AppStrings.Get("Settings_GeneralSection");
            AutoStartCheckBox.Text = AppStrings.Get("Settings_AutoStart");
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

            AutoStartCheckBox.Checked = Settings.Current.AutoStart;
        }

        private void AutoStartCheckBox_CheckedChanged(object sender, EventArgs e)
        {
            Settings.Current.AutoStart = AutoStartCheckBox.Checked;
            Settings.Current.Save();
        }
    }
}
