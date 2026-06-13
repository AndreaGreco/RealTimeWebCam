using System;
using System.CodeDom.Compiler;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    public partial class SettingsForm : Form
    {
        private ComboBox languageComboBox;

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

        private void radioLanguage_IT_CheckedChanged(object sender, EventArgs e)
        {
            LanguageChanged(new LanguageOption("Italiano", "it-IT"));
        }

        private void radioLanguageEn_CheckedChanged(object sender, EventArgs e)
        {
            LanguageChanged(new LanguageOption("English", "en"));
        }

        private void radioLanguageSystem_CheckedChanged(object sender, EventArgs e)
        {
            LanguageChanged(new LanguageOption(AppStrings.Get("Language_System"), string.Empty));
        }

        private void groupBox1_Enter(object sender, EventArgs e)
        {

        }

        private void SettingsForm_Load(object sender, EventArgs e)
        {
            string currentLanguage = Settings.Current.Language;
            switch (Settings.Current.Language)
            {
                case "it-IT":
                    this.radioLanguage_IT.Checked = true;
                    break;

                case "en":
                    this.radioLanguageEn.Checked = true;
                    break;

                default:
                    this.radioLanguageSystem.Checked = true;
                    break;
            }
        }

        private void AutoStartCheckBox_CheckedChanged(object sender, EventArgs e)
        {
            Settings.Current.AutoStart = AutoStartCheckBox.Checked;
            Settings.Current.Save();
        }
    }
}
