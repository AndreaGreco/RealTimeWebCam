namespace RTVirtualCamera
{
    partial class About
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
            appIconBox = new System.Windows.Forms.PictureBox();
            titleLabel = new System.Windows.Forms.Label();
            versionLabel = new System.Windows.Forms.Label();
            taglineLabel = new System.Windows.Forms.Label();
            separatorLabel = new System.Windows.Forms.Label();
            creditsLabel = new System.Windows.Forms.Label();
            githubLink = new System.Windows.Forms.LinkLabel();
            closeButton = new System.Windows.Forms.Button();
            ((System.ComponentModel.ISupportInitialize)appIconBox).BeginInit();
            SuspendLayout();
            //
            // appIconBox
            //
            appIconBox.Location = new System.Drawing.Point(20, 20);
            appIconBox.Name = "appIconBox";
            appIconBox.Size = new System.Drawing.Size(48, 48);
            appIconBox.SizeMode = System.Windows.Forms.PictureBoxSizeMode.Zoom;
            appIconBox.TabIndex = 0;
            appIconBox.TabStop = false;
            //
            // titleLabel
            //
            titleLabel.AutoSize = true;
            titleLabel.Font = new System.Drawing.Font("Segoe UI", 12F, System.Drawing.FontStyle.Bold);
            titleLabel.Location = new System.Drawing.Point(80, 18);
            titleLabel.Name = "titleLabel";
            titleLabel.Size = new System.Drawing.Size(150, 21);
            titleLabel.TabIndex = 1;
            titleLabel.Text = "RT Virtual Camera";
            //
            // versionLabel
            //
            versionLabel.AutoSize = true;
            versionLabel.ForeColor = System.Drawing.SystemColors.GrayText;
            versionLabel.Location = new System.Drawing.Point(82, 44);
            versionLabel.Name = "versionLabel";
            versionLabel.Size = new System.Drawing.Size(60, 15);
            versionLabel.TabIndex = 2;
            versionLabel.Text = "Version";
            //
            // taglineLabel
            //
            taglineLabel.Location = new System.Drawing.Point(20, 80);
            taglineLabel.Name = "taglineLabel";
            taglineLabel.Size = new System.Drawing.Size(400, 32);
            taglineLabel.TabIndex = 3;
            taglineLabel.Text = "Tagline";
            //
            // separatorLabel
            //
            separatorLabel.BorderStyle = System.Windows.Forms.BorderStyle.Fixed3D;
            separatorLabel.Location = new System.Drawing.Point(20, 114);
            separatorLabel.Name = "separatorLabel";
            separatorLabel.Size = new System.Drawing.Size(400, 2);
            separatorLabel.TabIndex = 4;
            //
            // creditsLabel
            //
            creditsLabel.Font = new System.Drawing.Font("Segoe UI", 8.25F);
            creditsLabel.ForeColor = System.Drawing.SystemColors.GrayText;
            creditsLabel.Location = new System.Drawing.Point(20, 124);
            creditsLabel.Name = "creditsLabel";
            creditsLabel.Size = new System.Drawing.Size(400, 34);
            creditsLabel.TabIndex = 5;
            creditsLabel.Text = "Credits";
            //
            // githubLink
            //
            githubLink.AutoSize = true;
            githubLink.LinkColor = System.Drawing.SystemColors.HotTrack;
            githubLink.Location = new System.Drawing.Point(20, 162);
            githubLink.Name = "githubLink";
            githubLink.Size = new System.Drawing.Size(220, 15);
            githubLink.TabIndex = 6;
            githubLink.TabStop = true;
            githubLink.Text = "github.com/AndreaGreco/RealTimeWebCam";
            githubLink.LinkClicked += GithubLink_LinkClicked;
            //
            // closeButton
            //
            closeButton.Anchor = System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right;
            closeButton.DialogResult = System.Windows.Forms.DialogResult.OK;
            closeButton.Location = new System.Drawing.Point(344, 190);
            closeButton.Name = "closeButton";
            closeButton.Size = new System.Drawing.Size(76, 26);
            closeButton.TabIndex = 7;
            closeButton.Text = "Close";
            closeButton.UseVisualStyleBackColor = true;
            //
            // About
            //
            AcceptButton = closeButton;
            AutoScaleDimensions = new System.Drawing.SizeF(7F, 15F);
            AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            CancelButton = closeButton;
            ClientSize = new System.Drawing.Size(440, 236);
            Controls.Add(closeButton);
            Controls.Add(githubLink);
            Controls.Add(creditsLabel);
            Controls.Add(separatorLabel);
            Controls.Add(taglineLabel);
            Controls.Add(versionLabel);
            Controls.Add(titleLabel);
            Controls.Add(appIconBox);
            FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            Name = "About";
            ShowIcon = false;
            ShowInTaskbar = false;
            StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
            Text = "About";
            Load += About_Load;
            ((System.ComponentModel.ISupportInitialize)appIconBox).EndInit();
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private System.Windows.Forms.PictureBox appIconBox;
        private System.Windows.Forms.Label titleLabel;
        private System.Windows.Forms.Label versionLabel;
        private System.Windows.Forms.Label taglineLabel;
        private System.Windows.Forms.Label separatorLabel;
        private System.Windows.Forms.Label creditsLabel;
        private System.Windows.Forms.LinkLabel githubLink;
        private System.Windows.Forms.Button closeButton;
    }
}
