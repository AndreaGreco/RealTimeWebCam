using System;
using System.Drawing;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    partial class MainForm
    {
        /// <summary>
        /// Variabile di progettazione necessaria.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

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
            menuStrip1 = new MenuStrip();
            fileToolStripMenuItem = new ToolStripMenuItem();
            exitToolStripMenuItem = new ToolStripMenuItem();
            settingsToolStripMenuItem = new ToolStripMenuItem();
            aboutToolStripMenuItem = new ToolStripMenuItem();
            videoPanel = new Panel();
            previewStatusLabel = new Label();
            panel1 = new Panel();
            streamProp_lbl = new Label();
            label2 = new Label();
            pathLabel = new Label();
            pathTextBox = new TextBox();
            playButton = new Button();
            startVCamButton = new Button();
            tableLayoutPanel1 = new TableLayoutPanel();
            menuStrip1.SuspendLayout();
            videoPanel.SuspendLayout();
            panel1.SuspendLayout();
            tableLayoutPanel1.SuspendLayout();
            SuspendLayout();
            // 
            // menuStrip1
            // 
            resources.ApplyResources(menuStrip1, "menuStrip1");
            menuStrip1.Items.AddRange(new ToolStripItem[] { fileToolStripMenuItem, settingsToolStripMenuItem, aboutToolStripMenuItem });
            menuStrip1.Name = "menuStrip1";
            // 
            // fileToolStripMenuItem
            // 
            resources.ApplyResources(fileToolStripMenuItem, "fileToolStripMenuItem");
            fileToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { exitToolStripMenuItem });
            fileToolStripMenuItem.Name = "fileToolStripMenuItem";
            // 
            // exitToolStripMenuItem
            // 
            resources.ApplyResources(exitToolStripMenuItem, "exitToolStripMenuItem");
            exitToolStripMenuItem.Name = "exitToolStripMenuItem";
            exitToolStripMenuItem.Click += exitToolStripMenuItem_Click;
            // 
            // settingsToolStripMenuItem
            // 
            resources.ApplyResources(settingsToolStripMenuItem, "settingsToolStripMenuItem");
            settingsToolStripMenuItem.Name = "settingsToolStripMenuItem";
            settingsToolStripMenuItem.Click += settingsToolStripMenuItem_Click;
            // 
            // aboutToolStripMenuItem
            // 
            resources.ApplyResources(aboutToolStripMenuItem, "aboutToolStripMenuItem");
            aboutToolStripMenuItem.Name = "aboutToolStripMenuItem";
            aboutToolStripMenuItem.Click += aboutToolStripMenuItem_Click;
            // 
            // videoPanel
            // 
            resources.ApplyResources(videoPanel, "videoPanel");
            videoPanel.BackColor = Color.Black;
            videoPanel.Controls.Add(previewStatusLabel);
            videoPanel.Name = "videoPanel";
            // 
            // previewStatusLabel
            // 
            resources.ApplyResources(previewStatusLabel, "previewStatusLabel");
            previewStatusLabel.ForeColor = Color.White;
            previewStatusLabel.Name = "previewStatusLabel";
            // 
            // panel1
            // 
            resources.ApplyResources(panel1, "panel1");
            panel1.Controls.Add(streamProp_lbl);
            panel1.Controls.Add(label2);
            panel1.Controls.Add(pathLabel);
            panel1.Controls.Add(pathTextBox);
            panel1.Controls.Add(playButton);
            panel1.Controls.Add(startVCamButton);
            panel1.Name = "panel1";
            panel1.Paint += panel1_Paint;
            // 
            // streamProp_lbl
            // 
            resources.ApplyResources(streamProp_lbl, "streamProp_lbl");
            streamProp_lbl.Name = "streamProp_lbl";
            // 
            // label2
            // 
            resources.ApplyResources(label2, "label2");
            label2.Name = "label2";
            // 
            // pathLabel
            // 
            resources.ApplyResources(pathLabel, "pathLabel");
            pathLabel.Name = "pathLabel";
            // 
            // pathTextBox
            // 
            resources.ApplyResources(pathTextBox, "pathTextBox");
            pathTextBox.Name = "pathTextBox";
            // 
            // playButton
            // 
            resources.ApplyResources(playButton, "playButton");
            playButton.Name = "playButton";
            playButton.UseVisualStyleBackColor = true;
            playButton.Click += PlayButton_Click;
            // 
            // startVCamButton
            // 
            resources.ApplyResources(startVCamButton, "startVCamButton");
            startVCamButton.BackColor = Color.LightGreen;
            startVCamButton.Name = "startVCamButton";
            startVCamButton.UseVisualStyleBackColor = false;
            startVCamButton.Click += StartVCamButton_Click;
            // 
            // tableLayoutPanel1
            // 
            resources.ApplyResources(tableLayoutPanel1, "tableLayoutPanel1");
            tableLayoutPanel1.Controls.Add(panel1, 0, 1);
            tableLayoutPanel1.Controls.Add(videoPanel, 0, 0);
            tableLayoutPanel1.Name = "tableLayoutPanel1";
            // 
            // MainForm
            // 
            resources.ApplyResources(this, "$this");
            AutoScaleMode = AutoScaleMode.Font;
            Controls.Add(tableLayoutPanel1);
            Controls.Add(menuStrip1);
            MainMenuStrip = menuStrip1;
            Name = "MainForm";
            Load += MainForm_Load;
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            videoPanel.ResumeLayout(false);
            panel1.ResumeLayout(false);
            panel1.PerformLayout();
            tableLayoutPanel1.ResumeLayout(false);
            ResumeLayout(false);
            PerformLayout();

        }

        #endregion
        private MenuStrip menuStrip1;
        private ToolStripMenuItem fileToolStripMenuItem;
        private ToolStripMenuItem settingsToolStripMenuItem;
        private ToolStripMenuItem aboutToolStripMenuItem;
        private ToolStripMenuItem exitToolStripMenuItem;
        private Panel videoPanel;
        private Label previewStatusLabel;
        private Panel panel1;
        private Label streamProp_lbl;
        private Label label2;
        private Label pathLabel;
        private TextBox pathTextBox;
        private Button playButton;
        private Button startVCamButton;
        private TableLayoutPanel tableLayoutPanel1;
    }
}

