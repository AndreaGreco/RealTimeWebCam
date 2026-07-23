namespace RTVirtualCamera
{
    partial class EngineGuideForm
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
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
            headerLabel = new System.Windows.Forms.Label();
            introLabel = new System.Windows.Forms.Label();
            contentPanel = new System.Windows.Forms.FlowLayoutPanel();
            closeButton = new System.Windows.Forms.Button();
            SuspendLayout();
            //
            // headerLabel
            //
            headerLabel.AutoSize = true;
            headerLabel.Font = new System.Drawing.Font("Segoe UI", 12F, System.Drawing.FontStyle.Bold);
            headerLabel.Location = new System.Drawing.Point(16, 14);
            headerLabel.Name = "headerLabel";
            headerLabel.Size = new System.Drawing.Size(60, 21);
            headerLabel.TabIndex = 0;
            headerLabel.Text = "Guide";
            //
            // introLabel
            //
            introLabel.Location = new System.Drawing.Point(18, 44);
            introLabel.Name = "introLabel";
            introLabel.Size = new System.Drawing.Size(548, 62);
            introLabel.TabIndex = 1;
            introLabel.Text = "Intro";
            //
            // contentPanel
            //
            contentPanel.Anchor = System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom
                | System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right;
            contentPanel.AutoScroll = true;
            contentPanel.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            contentPanel.FlowDirection = System.Windows.Forms.FlowDirection.TopDown;
            contentPanel.Location = new System.Drawing.Point(16, 112);
            contentPanel.Name = "contentPanel";
            contentPanel.Padding = new System.Windows.Forms.Padding(6);
            contentPanel.Size = new System.Drawing.Size(552, 436);
            contentPanel.TabIndex = 2;
            contentPanel.WrapContents = false;
            //
            // closeButton
            //
            closeButton.Anchor = System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right;
            closeButton.DialogResult = System.Windows.Forms.DialogResult.OK;
            closeButton.Location = new System.Drawing.Point(492, 556);
            closeButton.Name = "closeButton";
            closeButton.Size = new System.Drawing.Size(76, 26);
            closeButton.TabIndex = 3;
            closeButton.Text = "Close";
            closeButton.UseVisualStyleBackColor = true;
            //
            // EngineGuideForm
            //
            AcceptButton = closeButton;
            AutoScaleDimensions = new System.Drawing.SizeF(7F, 15F);
            AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            CancelButton = closeButton;
            ClientSize = new System.Drawing.Size(584, 594);
            Controls.Add(closeButton);
            Controls.Add(contentPanel);
            Controls.Add(introLabel);
            Controls.Add(headerLabel);
            FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            MaximizeBox = false;
            MinimizeBox = false;
            Name = "EngineGuideForm";
            ShowIcon = false;
            ShowInTaskbar = false;
            StartPosition = System.Windows.Forms.FormStartPosition.CenterParent;
            Text = "Guide";
            Load += EngineGuideForm_Load;
            ResumeLayout(false);
            PerformLayout();
        }

        #endregion

        private System.Windows.Forms.Label headerLabel;
        private System.Windows.Forms.Label introLabel;
        private System.Windows.Forms.FlowLayoutPanel contentPanel;
        private System.Windows.Forms.Button closeButton;
    }
}
