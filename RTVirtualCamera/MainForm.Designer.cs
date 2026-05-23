using System;
using System.Drawing;
using System.Windows.Forms;

namespace TestVideo
{
    partial class MainForm
    {
        /// <summary>
        /// Variabile di progettazione necessaria.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        // Controlli del form
        private Panel videoPanel;
        private Button playButton;
        private Button startVCamButton;
        private TextBox pathTextBox;
        private Label pathLabel;

        /// <summary>
        /// Pulire le risorse in uso.
        /// </summary>
        /// <param name="disposing">ha valore true se le risorse gestite devono essere eliminate, false in caso contrario.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Codice generato da Progettazione Windows Form

        /// <summary>
        /// Metodo necessario per il supporto della finestra di progettazione. Non modificare
        /// il contenuto del metodo con l'editor di codice.
        /// </summary>
        private void InitializeComponent()
        {
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(MainForm));
            this.videoPanel = new System.Windows.Forms.Panel();
            this.playButton = new System.Windows.Forms.Button();
            this.startVCamButton = new System.Windows.Forms.Button();
            this.pathTextBox = new System.Windows.Forms.TextBox();
            this.pathLabel = new System.Windows.Forms.Label();
            this.tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
            this.panel1 = new System.Windows.Forms.Panel();
            this.label2 = new System.Windows.Forms.Label();
            this.streamProp_lbl = new System.Windows.Forms.Label();
            this.tableLayoutPanel1.SuspendLayout();
            this.panel1.SuspendLayout();
            this.SuspendLayout();
            // 
            // videoPanel
            // 
            this.videoPanel.BackColor = System.Drawing.Color.Black;
            this.videoPanel.Dock = System.Windows.Forms.DockStyle.Fill;
            this.videoPanel.Location = new System.Drawing.Point(3, 3);
            this.videoPanel.Name = "videoPanel";
            this.videoPanel.Padding = new System.Windows.Forms.Padding(8);
            this.videoPanel.Size = new System.Drawing.Size(909, 667);
            this.videoPanel.TabIndex = 0;
            // 
            // playButton
            // 
            this.playButton.Location = new System.Drawing.Point(3, 61);
            this.playButton.Name = "playButton";
            this.playButton.Size = new System.Drawing.Size(75, 30);
            this.playButton.TabIndex = 4;
            this.playButton.Text = "Probe";
            this.playButton.UseVisualStyleBackColor = true;
            this.playButton.Click += new System.EventHandler(this.PlayButton_Click);
            // 
            // startVCamButton
            // 
            this.startVCamButton.BackColor = System.Drawing.Color.LightGreen;
            this.startVCamButton.Enabled = false;
            this.startVCamButton.Location = new System.Drawing.Point(84, 61);
            this.startVCamButton.Name = "startVCamButton";
            this.startVCamButton.Size = new System.Drawing.Size(100, 30);
            this.startVCamButton.TabIndex = 7;
            this.startVCamButton.Text = "Start VCam";
            this.startVCamButton.UseVisualStyleBackColor = false;
            this.startVCamButton.Click += new System.EventHandler(this.StartVCamButton_Click);
            // 
            // pathTextBox
            // 
            this.pathTextBox.Location = new System.Drawing.Point(73, 3);
            this.pathTextBox.Name = "pathTextBox";
            this.pathTextBox.Size = new System.Drawing.Size(833, 20);
            this.pathTextBox.TabIndex = 2;
            this.pathTextBox.Text = "rtsp://192.168.178.66:8554/webcam";
            // 
            // pathLabel
            // 
            this.pathLabel.AutoSize = true;
            this.pathLabel.Location = new System.Drawing.Point(3, 6);
            this.pathLabel.Name = "pathLabel";
            this.pathLabel.Size = new System.Drawing.Size(64, 13);
            this.pathLabel.TabIndex = 1;
            this.pathLabel.Text = "RTSP URL:";
            // 
            // tableLayoutPanel1
            // 
            this.tableLayoutPanel1.ColumnCount = 1;
            this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 50F));
            this.tableLayoutPanel1.Controls.Add(this.videoPanel, 0, 0);
            this.tableLayoutPanel1.Controls.Add(this.panel1, 0, 1);
            this.tableLayoutPanel1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.tableLayoutPanel1.Location = new System.Drawing.Point(0, 0);
            this.tableLayoutPanel1.Name = "tableLayoutPanel1";
            this.tableLayoutPanel1.RowCount = 2;
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 86.72681F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 13.2732F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 20F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 20F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 20F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 20F));
            this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 20F));
            this.tableLayoutPanel1.Size = new System.Drawing.Size(915, 776);
            this.tableLayoutPanel1.TabIndex = 7;
            // 
            // panel1
            // 
            this.panel1.Controls.Add(this.streamProp_lbl);
            this.panel1.Controls.Add(this.label2);
            this.panel1.Controls.Add(this.pathLabel);
            this.panel1.Controls.Add(this.pathTextBox);
            this.panel1.Controls.Add(this.playButton);
            this.panel1.Controls.Add(this.startVCamButton);
            this.panel1.Dock = System.Windows.Forms.DockStyle.Fill;
            this.panel1.Location = new System.Drawing.Point(3, 676);
            this.panel1.MinimumSize = new System.Drawing.Size(500, 90);
            this.panel1.Name = "panel1";
            this.panel1.Size = new System.Drawing.Size(909, 97);
            this.panel1.TabIndex = 1;
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(3, 32);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(48, 13);
            this.label2.TabIndex = 8;
            this.label2.Text = "Propelty:";
            // 
            // streamProp_lbl
            // 
            this.streamProp_lbl.AutoSize = true;
            this.streamProp_lbl.Location = new System.Drawing.Point(73, 32);
            this.streamProp_lbl.Name = "streamProp_lbl";
            this.streamProp_lbl.Size = new System.Drawing.Size(0, 13);
            this.streamProp_lbl.TabIndex = 9;
            // 
            // MainForm
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(915, 776);
            this.Controls.Add(this.tableLayoutPanel1);
            this.Icon = ((System.Drawing.Icon)(resources.GetObject("$this.Icon")));
            this.Name = "MainForm";
            this.StartPosition = System.Windows.Forms.FormStartPosition.CenterScreen;
            this.Text = "Video Player Test";
            this.tableLayoutPanel1.ResumeLayout(false);
            this.panel1.ResumeLayout(false);
            this.panel1.PerformLayout();
            this.ResumeLayout(false);

        }

        #endregion

        private TableLayoutPanel tableLayoutPanel1;
        private Panel panel1;
        private Label label2;
        private Label streamProp_lbl;
    }
}

