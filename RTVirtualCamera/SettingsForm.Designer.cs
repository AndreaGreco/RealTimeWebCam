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
            OverlayCheckBox = new System.Windows.Forms.CheckBox();
            networkSectionLabel = new System.Windows.Forms.Label();
            transportLabel = new System.Windows.Forms.Label();
            transportComboBox = new System.Windows.Forms.ComboBox();
            HardwareDecodeCheckBox = new System.Windows.Forms.CheckBox();
            socketTimeoutLabel = new System.Windows.Forms.Label();
            socketTimeoutNumeric = new System.Windows.Forms.NumericUpDown();
            reorderLabel = new System.Windows.Forms.Label();
            reorderNumeric = new System.Windows.Forms.NumericUpDown();
            latencyCapLabel = new System.Windows.Forms.Label();
            latencyCapNumeric = new System.Windows.Forms.NumericUpDown();
            closeButton = new System.Windows.Forms.Button();
            ((System.ComponentModel.ISupportInitialize)socketTimeoutNumeric).BeginInit();
            ((System.ComponentModel.ISupportInitialize)reorderNumeric).BeginInit();
            ((System.ComponentModel.ISupportInitialize)latencyCapNumeric).BeginInit();
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
            // OverlayCheckBox
            //
            OverlayCheckBox.AutoSize = true;
            OverlayCheckBox.Location = new System.Drawing.Point(20, 128);
            OverlayCheckBox.Name = "OverlayCheckBox";
            OverlayCheckBox.Size = new System.Drawing.Size(160, 19);
            OverlayCheckBox.TabIndex = 4;
            OverlayCheckBox.Text = "Frame counter overlay";
            OverlayCheckBox.UseVisualStyleBackColor = true;
            OverlayCheckBox.CheckedChanged += OverlayCheckBox_CheckedChanged;
            //
            // networkSectionLabel
            //
            networkSectionLabel.AutoSize = true;
            networkSectionLabel.Font = new System.Drawing.Font("Segoe UI", 9F, System.Drawing.FontStyle.Bold);
            networkSectionLabel.Location = new System.Drawing.Point(20, 164);
            networkSectionLabel.Name = "networkSectionLabel";
            networkSectionLabel.Size = new System.Drawing.Size(60, 15);
            networkSectionLabel.TabIndex = 5;
            networkSectionLabel.Text = "Network";
            //
            // transportLabel
            //
            transportLabel.AutoSize = true;
            transportLabel.Location = new System.Drawing.Point(20, 190);
            transportLabel.Name = "transportLabel";
            transportLabel.Size = new System.Drawing.Size(90, 15);
            transportLabel.TabIndex = 6;
            transportLabel.Text = "RTSP transport";
            //
            // transportComboBox
            //
            transportComboBox.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            transportComboBox.FormattingEnabled = true;
            transportComboBox.Location = new System.Drawing.Point(20, 208);
            transportComboBox.Name = "transportComboBox";
            transportComboBox.Size = new System.Drawing.Size(300, 23);
            transportComboBox.TabIndex = 7;
            //
            // HardwareDecodeCheckBox
            //
            HardwareDecodeCheckBox.AutoSize = true;
            HardwareDecodeCheckBox.Location = new System.Drawing.Point(20, 244);
            HardwareDecodeCheckBox.Name = "HardwareDecodeCheckBox";
            HardwareDecodeCheckBox.Size = new System.Drawing.Size(180, 19);
            HardwareDecodeCheckBox.TabIndex = 8;
            HardwareDecodeCheckBox.Text = "Hardware decode (GPU)";
            HardwareDecodeCheckBox.UseVisualStyleBackColor = true;
            HardwareDecodeCheckBox.CheckedChanged += HardwareDecodeCheckBox_CheckedChanged;
            //
            // socketTimeoutLabel
            //
            socketTimeoutLabel.AutoSize = true;
            socketTimeoutLabel.Location = new System.Drawing.Point(20, 280);
            socketTimeoutLabel.Name = "socketTimeoutLabel";
            socketTimeoutLabel.Size = new System.Drawing.Size(120, 15);
            socketTimeoutLabel.TabIndex = 9;
            socketTimeoutLabel.Text = "Socket timeout (ms)";
            //
            // socketTimeoutNumeric
            //
            socketTimeoutNumeric.Location = new System.Drawing.Point(220, 278);
            socketTimeoutNumeric.Name = "socketTimeoutNumeric";
            socketTimeoutNumeric.Size = new System.Drawing.Size(100, 23);
            socketTimeoutNumeric.TabIndex = 10;
            socketTimeoutNumeric.Minimum = new decimal(new int[] { 500, 0, 0, 0 });
            socketTimeoutNumeric.Maximum = new decimal(new int[] { 60000, 0, 0, 0 });
            socketTimeoutNumeric.Increment = new decimal(new int[] { 500, 0, 0, 0 });
            socketTimeoutNumeric.ValueChanged += SocketTimeoutNumeric_ValueChanged;
            //
            // reorderLabel
            //
            reorderLabel.AutoSize = true;
            reorderLabel.Location = new System.Drawing.Point(20, 312);
            reorderLabel.Name = "reorderLabel";
            reorderLabel.Size = new System.Drawing.Size(120, 15);
            reorderLabel.TabIndex = 11;
            reorderLabel.Text = "RTP reorder (pkt)";
            //
            // reorderNumeric
            //
            reorderNumeric.Location = new System.Drawing.Point(220, 310);
            reorderNumeric.Name = "reorderNumeric";
            reorderNumeric.Size = new System.Drawing.Size(100, 23);
            reorderNumeric.TabIndex = 12;
            reorderNumeric.Minimum = new decimal(new int[] { 0, 0, 0, 0 });
            reorderNumeric.Maximum = new decimal(new int[] { 500, 0, 0, 0 });
            reorderNumeric.Increment = new decimal(new int[] { 1, 0, 0, 0 });
            reorderNumeric.ValueChanged += ReorderNumeric_ValueChanged;
            //
            // latencyCapLabel
            //
            latencyCapLabel.AutoSize = true;
            latencyCapLabel.Location = new System.Drawing.Point(20, 344);
            latencyCapLabel.Name = "latencyCapLabel";
            latencyCapLabel.Size = new System.Drawing.Size(120, 15);
            latencyCapLabel.TabIndex = 13;
            latencyCapLabel.Text = "Latency cap (ms)";
            //
            // latencyCapNumeric
            //
            latencyCapNumeric.Location = new System.Drawing.Point(220, 342);
            latencyCapNumeric.Name = "latencyCapNumeric";
            latencyCapNumeric.Size = new System.Drawing.Size(100, 23);
            latencyCapNumeric.TabIndex = 14;
            latencyCapNumeric.Minimum = new decimal(new int[] { 50, 0, 0, 0 });
            latencyCapNumeric.Maximum = new decimal(new int[] { 5000, 0, 0, 0 });
            latencyCapNumeric.Increment = new decimal(new int[] { 50, 0, 0, 0 });
            latencyCapNumeric.ValueChanged += LatencyCapNumeric_ValueChanged;
            //
            // closeButton
            //
            closeButton.Anchor = System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right;
            closeButton.DialogResult = System.Windows.Forms.DialogResult.OK;
            closeButton.Location = new System.Drawing.Point(244, 388);
            closeButton.Name = "closeButton";
            closeButton.Size = new System.Drawing.Size(76, 26);
            closeButton.TabIndex = 15;
            closeButton.Text = "Close";
            closeButton.UseVisualStyleBackColor = true;
            //
            // SettingsForm
            //
            AcceptButton = closeButton;
            AutoScaleDimensions = new System.Drawing.SizeF(7F, 15F);
            AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            CancelButton = closeButton;
            ClientSize = new System.Drawing.Size(340, 434);
            Controls.Add(closeButton);
            Controls.Add(latencyCapNumeric);
            Controls.Add(latencyCapLabel);
            Controls.Add(reorderNumeric);
            Controls.Add(reorderLabel);
            Controls.Add(socketTimeoutNumeric);
            Controls.Add(socketTimeoutLabel);
            Controls.Add(HardwareDecodeCheckBox);
            Controls.Add(transportComboBox);
            Controls.Add(transportLabel);
            Controls.Add(networkSectionLabel);
            Controls.Add(OverlayCheckBox);
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
            ((System.ComponentModel.ISupportInitialize)socketTimeoutNumeric).EndInit();
            ((System.ComponentModel.ISupportInitialize)reorderNumeric).EndInit();
            ((System.ComponentModel.ISupportInitialize)latencyCapNumeric).EndInit();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private System.Windows.Forms.Label languageSectionLabel;
        private System.Windows.Forms.ComboBox languageComboBox;
        private System.Windows.Forms.Label generalSectionLabel;
        private System.Windows.Forms.CheckBox AutoStartCheckBox;
        private System.Windows.Forms.CheckBox OverlayCheckBox;
        private System.Windows.Forms.Label networkSectionLabel;
        private System.Windows.Forms.Label transportLabel;
        private System.Windows.Forms.ComboBox transportComboBox;
        private System.Windows.Forms.CheckBox HardwareDecodeCheckBox;
        private System.Windows.Forms.Label socketTimeoutLabel;
        private System.Windows.Forms.NumericUpDown socketTimeoutNumeric;
        private System.Windows.Forms.Label reorderLabel;
        private System.Windows.Forms.NumericUpDown reorderNumeric;
        private System.Windows.Forms.Label latencyCapLabel;
        private System.Windows.Forms.NumericUpDown latencyCapNumeric;
        private System.Windows.Forms.Button closeButton;
    }
}
