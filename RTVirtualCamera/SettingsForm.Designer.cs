namespace RTVirtualCamera
{
    partial class SettingsForm
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            languageSectionLabel = new System.Windows.Forms.Label();
            languageComboBox = new System.Windows.Forms.ComboBox();
            generalSectionLabel = new System.Windows.Forms.Label();
            AutoStartCheckBox = new System.Windows.Forms.CheckBox();
            closeButton = new System.Windows.Forms.Button();
            SuspendLayout();
            //
            // languageSectionLabel
            //
            languageSectionLabel.AutoSize = true;
            languageSectionLabel.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
            languageSectionLabel.Location = new System.Drawing.Point(20, 16);
            languageSectionLabel.Name = "languageSectionLabel";
            languageSectionLabel.Size = new System.Drawing.Size(60, 15);
            languageSectionLabel.TabIndex = 0;
            languageSectionLabel.Text = "Language";
            //
            // languageComboBox
            //
            languageComboBox.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            languageComboBox.FormattingEnabled = true;
            languageComboBox.Location = new System.Drawing.Point(20, 38);
            languageComboBox.Name = "languageComboBox";
            languageComboBox.Size = new System.Drawing.Size(300, 23);
            languageComboBox.TabIndex = 1;
            //
            // generalSectionLabel
            //
            generalSectionLabel.AutoSize = true;
            generalSectionLabel.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
            generalSectionLabel.Location = new System.Drawing.Point(20, 78);
            generalSectionLabel.Name = "generalSectionLabel";
            generalSectionLabel.Size = new System.Drawing.Size(50, 15);
            generalSectionLabel.TabIndex = 2;
            generalSectionLabel.Text = "General";
            //
            // AutoStartCheckBox
            //
            AutoStartCheckBox.AutoSize = true;
            AutoStartCheckBox.Location = new System.Drawing.Point(20, 102);
            AutoStartCheckBox.Name = "AutoStartCheckBox";
            AutoStartCheckBox.Size = new System.Drawing.Size(80, 19);
            AutoStartCheckBox.TabIndex = 3;
            AutoStartCheckBox.Text = "Auto start";
            AutoStartCheckBox.UseVisualStyleBackColor = true;
            AutoStartCheckBox.CheckedChanged += AutoStartCheckBox_CheckedChanged;
            //
            // closeButton
            //
            closeButton.Anchor = System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right;
            closeButton.DialogResult = System.Windows.Forms.DialogResult.OK;
            closeButton.Location = new System.Drawing.Point(244, 148);
            closeButton.Name = "closeButton";
            closeButton.Size = new System.Drawing.Size(76, 26);
            closeButton.TabIndex = 4;
            closeButton.Text = "Close";
            closeButton.UseVisualStyleBackColor = true;
            //
            // SettingsForm
            //
            AcceptButton = closeButton;
            AutoScaleDimensions = new System.Drawing.SizeF(7F, 15F);
            AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            CancelButton = closeButton;
            ClientSize = new System.Drawing.Size(340, 194);
            Controls.Add(closeButton);
            Controls.Add(AutoStartCheckBox);
            Controls.Add(generalSectionLabel);
            Controls.Add(languageComboBox);
            Controls.Add(languageSectionLabel);
            FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            Name = "SettingsForm";
            ShowIcon = false;
            ShowInTaskbar = false;
            StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
            Text = "Settings";
            Load += SettingsForm_Load;
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private System.Windows.Forms.Label languageSectionLabel;
        private System.Windows.Forms.ComboBox languageComboBox;
        private System.Windows.Forms.Label generalSectionLabel;
        private System.Windows.Forms.CheckBox AutoStartCheckBox;
        private System.Windows.Forms.Button closeButton;
    }
}
