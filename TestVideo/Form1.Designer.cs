using System;
using System.Drawing;
using System.Windows.Forms;

namespace TestVideo
{
    partial class Form1
    {
        /// <summary>
        /// Variabile di progettazione necessaria.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        // Controlli del form
        private Panel videoPanel;
        private Button playButton;
        private Button pauseButton;
        private Button stopButton;
        private TextBox pathTextBox;
        private Button browseButton;
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
            this.videoPanel = new Panel();
            this.playButton = new Button();
            this.pauseButton = new Button();
            this.stopButton = new Button();
            this.pathTextBox = new TextBox();
            this.browseButton = new Button();
            this.pathLabel = new Label();
            this.SuspendLayout();

            // 
            // Form1
            //
            this.AutoScaleDimensions = new SizeF(6F, 13F);
            this.AutoScaleMode = AutoScaleMode.Font;
            this.ClientSize = new Size(800, 600);
            this.Text = "Video Player Test";
            this.StartPosition = FormStartPosition.CenterScreen;

            // 
            // videoPanel
            //
            this.videoPanel.BackColor = Color.Black;
            this.videoPanel.Location = new Point(12, 12);
            this.videoPanel.Name = "videoPanel";
            this.videoPanel.Size = new Size(640, 480);
            this.videoPanel.TabIndex = 0;

            // 
            // pathLabel
            //
            this.pathLabel.AutoSize = true;
            this.pathLabel.Location = new Point(12, 508);
            this.pathLabel.Name = "pathLabel";
            this.pathLabel.Size = new Size(69, 13);
            this.pathLabel.TabIndex = 1;
            this.pathLabel.Text = "Video Path:";

            // 
            // pathTextBox
            //
            this.pathTextBox.Location = new Point(87, 505);
            this.pathTextBox.Name = "pathTextBox";
            this.pathTextBox.Size = new Size(400, 20);
            this.pathTextBox.TabIndex = 2;
            this.pathTextBox.Text = @"rtsp://192.168.178.66:8554/webcam";
            //this.pathTextBox.Text = @"C:\Users\andre\Downloads\sample-5s.mp4";


            // 
            // browseButton
            //
            this.browseButton.Location = new Point(493, 503);
            this.browseButton.Name = "browseButton";
            this.browseButton.Size = new Size(75, 23);
            this.browseButton.TabIndex = 3;
            this.browseButton.Text = "Browse";
            this.browseButton.UseVisualStyleBackColor = true;
            this.browseButton.Click += new EventHandler(this.BrowseButton_Click);

            // 
            // playButton
            //
            this.playButton.Location = new Point(12, 540);
            this.playButton.Name = "playButton";
            this.playButton.Size = new Size(75, 30);
            this.playButton.TabIndex = 4;
            this.playButton.Text = "Play";
            this.playButton.UseVisualStyleBackColor = true;
            this.playButton.Click += new EventHandler(this.PlayButton_Click);

            // 
            // pauseButton
            //
            this.pauseButton.Location = new Point(93, 540);
            this.pauseButton.Name = "pauseButton";
            this.pauseButton.Size = new Size(75, 30);
            this.pauseButton.TabIndex = 5;
            this.pauseButton.Text = "Pause";
            this.pauseButton.UseVisualStyleBackColor = true;
            this.pauseButton.Click += new EventHandler(this.PauseButton_Click);

            // 
            // stopButton
            //
            this.stopButton.Location = new Point(174, 540);
            this.stopButton.Name = "stopButton";
            this.stopButton.Size = new Size(75, 30);
            this.stopButton.TabIndex = 6;
            this.stopButton.Text = "Stop";
            this.stopButton.UseVisualStyleBackColor = true;
            this.stopButton.Click += new EventHandler(this.StopButton_Click);

            // Aggiunta controlli al form
            this.Controls.Add(this.videoPanel);
            this.Controls.Add(this.pathLabel);
            this.Controls.Add(this.pathTextBox);
            this.Controls.Add(this.browseButton);
            this.Controls.Add(this.playButton);
            this.Controls.Add(this.pauseButton);
            this.Controls.Add(this.stopButton);

            this.ResumeLayout(false);
            this.PerformLayout();
        }

        #endregion
    }
}

