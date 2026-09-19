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
            ListViewItem listViewItem1 = new ListViewItem(new string[] { "Contenitore", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem2 = new ListViewItem(new string[] { "Trasporto", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem3 = new ListViewItem(new string[] { "Codec", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem4 = new ListViewItem(new string[] { "Formato pixel", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem5 = new ListViewItem(new string[] { "Risoluzione", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem6 = new ListViewItem(new string[] { "Frame rate", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem7 = new ListViewItem(new string[] { "Bitrate", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem8 = new ListViewItem(new string[] { "Preferenza trasporto", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem9 = new ListViewItem(new string[] { "Decodifica HW", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem10 = new ListViewItem(new string[] { "Timeout socket", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem11 = new ListViewItem(new string[] { "Reorder RTP", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem12 = new ListViewItem(new string[] { "Buffer UDP", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem13 = new ListViewItem(new string[] { "Max delay", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem14 = new ListViewItem(new string[] { "Cap latenza", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem15 = new ListViewItem(new string[] { "Stato", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem16 = new ListViewItem(new string[] { "Motore", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem17 = new ListViewItem(new string[] { "Decodifica", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem18 = new ListViewItem(new string[] { "RX (fps)", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem19 = new ListViewItem(new string[] { "Render (fps)", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem20 = new ListViewItem(new string[] { "Duplicati (fps)", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem21 = new ListViewItem(new string[] { "Persi (fps)", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem22 = new ListViewItem(new string[] { "Elaborazione (ms)", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            ListViewItem listViewItem23 = new ListViewItem(new string[] { "Drift (ms)", "—" }, -1, SystemColors.WindowText, SystemColors.Window, new Font("Segoe UI", 9F));
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(MainForm));
            menuStrip1 = new MenuStrip();
            fileToolStripMenuItem = new ToolStripMenuItem();
            clearHistoryToolStripMenuItem = new ToolStripMenuItem();
            fileMenuSeparator = new ToolStripSeparator();
            exitToolStripMenuItem = new ToolStripMenuItem();
            settingsToolStripMenuItem = new ToolStripMenuItem();
            guideToolStripMenuItem = new ToolStripMenuItem();
            aboutToolStripMenuItem = new ToolStripMenuItem();
            videoPanel = new Panel();
            previewStatusLabel = new Label();
            panel1 = new Panel();
            streamProp_lbl = new Label();
            label2 = new Label();
            pathLabel = new Label();
            pathTextBox = new ComboBox();
            playButton = new Button();
            startVCamButton = new Button();
            tableLayoutPanel1 = new TableLayoutPanel();
            statsPanel = new Panel();
            statsLayout = new TableLayoutPanel();
            connHeader = new Label();
            connList = new ListView();
            connPropCol = new ColumnHeader();
            connValueCol = new ColumnHeader();
            statsHeader = new Label();
            statsList = new ListView();
            statsPropCol = new ColumnHeader();
            statsValueCol = new ColumnHeader();
            menuStrip1.SuspendLayout();
            videoPanel.SuspendLayout();
            panel1.SuspendLayout();
            tableLayoutPanel1.SuspendLayout();
            statsPanel.SuspendLayout();
            statsLayout.SuspendLayout();
            SuspendLayout();
            // 
            // menuStrip1
            // 
            menuStrip1.Items.AddRange(new ToolStripItem[] { fileToolStripMenuItem, settingsToolStripMenuItem, guideToolStripMenuItem, aboutToolStripMenuItem });
            menuStrip1.Location = new Point(0, 0);
            menuStrip1.Name = "menuStrip1";
            menuStrip1.Padding = new Padding(7, 2, 0, 2);
            menuStrip1.Size = new Size(924, 24);
            menuStrip1.TabIndex = 11;
            menuStrip1.Text = "menuStrip1";
            // 
            // fileToolStripMenuItem
            // 
            fileToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { clearHistoryToolStripMenuItem, fileMenuSeparator, exitToolStripMenuItem });
            fileToolStripMenuItem.Name = "fileToolStripMenuItem";
            fileToolStripMenuItem.Size = new Size(37, 20);
            fileToolStripMenuItem.Text = "File";
            // 
            // clearHistoryToolStripMenuItem
            // 
            clearHistoryToolStripMenuItem.Name = "clearHistoryToolStripMenuItem";
            clearHistoryToolStripMenuItem.Size = new Size(92, 22);
            clearHistoryToolStripMenuItem.Click += clearHistoryToolStripMenuItem_Click;
            // 
            // fileMenuSeparator
            // 
            fileMenuSeparator.Name = "fileMenuSeparator";
            fileMenuSeparator.Size = new Size(89, 6);
            // 
            // exitToolStripMenuItem
            // 
            exitToolStripMenuItem.Name = "exitToolStripMenuItem";
            exitToolStripMenuItem.Size = new Size(92, 22);
            exitToolStripMenuItem.Text = "Exit";
            exitToolStripMenuItem.Click += exitToolStripMenuItem_Click;
            // 
            // settingsToolStripMenuItem
            // 
            settingsToolStripMenuItem.Name = "settingsToolStripMenuItem";
            settingsToolStripMenuItem.Size = new Size(61, 20);
            settingsToolStripMenuItem.Text = "Settings";
            settingsToolStripMenuItem.Click += settingsToolStripMenuItem_Click;
            // 
            // guideToolStripMenuItem
            // 
            guideToolStripMenuItem.Name = "guideToolStripMenuItem";
            guideToolStripMenuItem.Size = new Size(12, 20);
            guideToolStripMenuItem.Click += guideToolStripMenuItem_Click;
            // 
            // aboutToolStripMenuItem
            // 
            aboutToolStripMenuItem.Name = "aboutToolStripMenuItem";
            aboutToolStripMenuItem.Size = new Size(52, 20);
            aboutToolStripMenuItem.Text = "About";
            aboutToolStripMenuItem.Click += aboutToolStripMenuItem_Click;
            // 
            // videoPanel
            // 
            videoPanel.BackColor = Color.Black;
            videoPanel.Controls.Add(previewStatusLabel);
            videoPanel.Dock = DockStyle.Fill;
            videoPanel.Location = new Point(4, 3);
            videoPanel.Margin = new Padding(4, 3, 4, 3);
            videoPanel.Name = "videoPanel";
            videoPanel.Padding = new Padding(9);
            videoPanel.Size = new Size(576, 566);
            videoPanel.TabIndex = 0;
            // 
            // previewStatusLabel
            // 
            previewStatusLabel.Dock = DockStyle.Fill;
            previewStatusLabel.Font = new Font("Segoe UI", 26F, FontStyle.Bold);
            previewStatusLabel.ForeColor = Color.White;
            previewStatusLabel.Location = new Point(9, 9);
            previewStatusLabel.Margin = new Padding(4, 0, 4, 0);
            previewStatusLabel.Name = "previewStatusLabel";
            previewStatusLabel.Size = new Size(558, 548);
            previewStatusLabel.TabIndex = 10;
            previewStatusLabel.Text = "Preview Ready";
            previewStatusLabel.TextAlign = ContentAlignment.MiddleCenter;
            previewStatusLabel.Visible = false;
            // 
            // panel1
            // 
            panel1.Controls.Add(streamProp_lbl);
            panel1.Controls.Add(label2);
            panel1.Controls.Add(pathLabel);
            panel1.Controls.Add(pathTextBox);
            panel1.Controls.Add(playButton);
            panel1.Controls.Add(startVCamButton);
            panel1.Dock = DockStyle.Fill;
            panel1.Location = new Point(4, 575);
            panel1.Margin = new Padding(4, 3, 4, 3);
            panel1.MaximumSize = new Size(0, 100);
            panel1.MinimumSize = new Size(120, 90);
            panel1.Name = "panel1";
            panel1.Size = new Size(576, 94);
            panel1.TabIndex = 1;
            panel1.Paint += panel1_Paint;
            // 
            // streamProp_lbl
            // 
            streamProp_lbl.AutoSize = true;
            streamProp_lbl.Location = new Point(85, 37);
            streamProp_lbl.Margin = new Padding(4, 0, 4, 0);
            streamProp_lbl.Name = "streamProp_lbl";
            streamProp_lbl.Size = new Size(0, 15);
            streamProp_lbl.TabIndex = 9;
            // 
            // label2
            // 
            label2.AutoSize = true;
            label2.Location = new Point(4, 37);
            label2.Margin = new Padding(4, 0, 4, 0);
            label2.Name = "label2";
            label2.Size = new Size(54, 15);
            label2.TabIndex = 8;
            label2.Text = "Propelty:";
            // 
            // pathLabel
            // 
            pathLabel.AutoSize = true;
            pathLabel.Location = new Point(4, 7);
            pathLabel.Margin = new Padding(4, 0, 4, 0);
            pathLabel.Name = "pathLabel";
            pathLabel.Size = new Size(60, 15);
            pathLabel.TabIndex = 1;
            pathLabel.Text = "RTSP URL:";
            // 
            // pathTextBox
            // 
            pathTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            pathTextBox.AutoCompleteMode = AutoCompleteMode.SuggestAppend;
            pathTextBox.AutoCompleteSource = AutoCompleteSource.ListItems;
            pathTextBox.FormattingEnabled = true;
            pathTextBox.Location = new Point(72, 3);
            pathTextBox.Margin = new Padding(4, 3, 4, 3);
            pathTextBox.Name = "pathTextBox";
            pathTextBox.Size = new Size(500, 23);
            pathTextBox.TabIndex = 2;
            // 
            // playButton
            // 
            playButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
            playButton.Location = new Point(4, 56);
            playButton.Margin = new Padding(4, 3, 4, 3);
            playButton.Name = "playButton";
            playButton.Size = new Size(200, 35);
            playButton.TabIndex = 4;
            playButton.Text = "Probe";
            playButton.UseVisualStyleBackColor = true;
            playButton.Click += PlayButton_Click;
            // 
            // startVCamButton
            // 
            startVCamButton.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
            startVCamButton.BackColor = Color.LightGreen;
            startVCamButton.Enabled = false;
            startVCamButton.Location = new Point(212, 56);
            startVCamButton.Margin = new Padding(4, 3, 4, 3);
            startVCamButton.Name = "startVCamButton";
            startVCamButton.Size = new Size(183, 35);
            startVCamButton.TabIndex = 7;
            startVCamButton.Text = "Start VCam";
            startVCamButton.UseVisualStyleBackColor = false;
            startVCamButton.Click += StartVCamButton_Click;
            // 
            // tableLayoutPanel1
            // 
            tableLayoutPanel1.ColumnCount = 1;
            tableLayoutPanel1.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            tableLayoutPanel1.Controls.Add(panel1, 0, 1);
            tableLayoutPanel1.Controls.Add(videoPanel, 0, 0);
            tableLayoutPanel1.Dock = DockStyle.Fill;
            tableLayoutPanel1.Location = new Point(0, 24);
            tableLayoutPanel1.Margin = new Padding(4, 3, 4, 3);
            tableLayoutPanel1.Name = "tableLayoutPanel1";
            tableLayoutPanel1.RowCount = 2;
            tableLayoutPanel1.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            tableLayoutPanel1.RowStyles.Add(new RowStyle(SizeType.Absolute, 100F));
            tableLayoutPanel1.Size = new Size(584, 672);
            tableLayoutPanel1.TabIndex = 7;
            // 
            // statsPanel
            // 
            statsPanel.Controls.Add(statsLayout);
            statsPanel.Dock = DockStyle.Right;
            statsPanel.Location = new Point(584, 24);
            statsPanel.Name = "statsPanel";
            statsPanel.Padding = new Padding(6, 4, 6, 6);
            statsPanel.Size = new Size(340, 672);
            statsPanel.TabIndex = 2;
            // 
            // statsLayout
            // 
            statsLayout.ColumnCount = 1;
            statsLayout.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            statsLayout.Controls.Add(connHeader, 0, 0);
            statsLayout.Controls.Add(connList, 0, 1);
            statsLayout.Controls.Add(statsHeader, 0, 2);
            statsLayout.Controls.Add(statsList, 0, 3);
            statsLayout.Dock = DockStyle.Fill;
            statsLayout.Location = new Point(6, 4);
            statsLayout.Name = "statsLayout";
            statsLayout.RowCount = 4;
            statsLayout.RowStyles.Add(new RowStyle());
            statsLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 55F));
            statsLayout.RowStyles.Add(new RowStyle());
            statsLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 45F));
            statsLayout.Size = new Size(328, 662);
            statsLayout.TabIndex = 0;
            // 
            // connHeader
            // 
            connHeader.AutoSize = true;
            connHeader.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            connHeader.Location = new Point(2, 8);
            connHeader.Margin = new Padding(2, 8, 2, 2);
            connHeader.Name = "connHeader";
            connHeader.Size = new Size(0, 15);
            connHeader.TabIndex = 0;
            // 
            // connList
            // 
            connList.Columns.AddRange(new ColumnHeader[] { connPropCol, connValueCol });
            connList.Dock = DockStyle.Fill;
            connList.FullRowSelect = true;
            connList.GridLines = true;
            connList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            connList.Items.AddRange(new ListViewItem[] { listViewItem1, listViewItem2, listViewItem3, listViewItem4, listViewItem5, listViewItem6, listViewItem7, listViewItem8, listViewItem9, listViewItem10, listViewItem11, listViewItem12, listViewItem13, listViewItem14 });
            connList.Location = new Point(3, 28);
            connList.MultiSelect = false;
            connList.Name = "connList";
            connList.Size = new Size(322, 330);
            connList.TabIndex = 1;
            connList.UseCompatibleStateImageBehavior = false;
            connList.View = View.Details;
            // 
            // connPropCol
            // 
            connPropCol.Width = 130;
            // 
            // connValueCol
            // 
            connValueCol.Width = 178;
            // 
            // statsHeader
            // 
            statsHeader.AutoSize = true;
            statsHeader.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            statsHeader.Location = new Point(2, 369);
            statsHeader.Margin = new Padding(2, 8, 2, 2);
            statsHeader.Name = "statsHeader";
            statsHeader.Size = new Size(0, 15);
            statsHeader.TabIndex = 2;
            // 
            // statsList
            // 
            statsList.Columns.AddRange(new ColumnHeader[] { statsPropCol, statsValueCol });
            statsList.Dock = DockStyle.Fill;
            statsList.FullRowSelect = true;
            statsList.GridLines = true;
            statsList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            statsList.Items.AddRange(new ListViewItem[] { listViewItem15, listViewItem16, listViewItem17, listViewItem18, listViewItem19, listViewItem20, listViewItem21, listViewItem22, listViewItem23 });
            statsList.Location = new Point(3, 389);
            statsList.MultiSelect = false;
            statsList.Name = "statsList";
            statsList.Size = new Size(322, 270);
            statsList.TabIndex = 3;
            statsList.UseCompatibleStateImageBehavior = false;
            statsList.View = View.Details;
            // 
            // statsPropCol
            // 
            statsPropCol.Width = 130;
            // 
            // statsValueCol
            // 
            statsValueCol.Width = 178;
            // 
            // MainForm
            // 
            AutoScaleDimensions = new SizeF(7F, 15F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(924, 696);
            Controls.Add(tableLayoutPanel1);
            Controls.Add(statsPanel);
            Controls.Add(menuStrip1);
            Icon = (Icon)resources.GetObject("$this.Icon");
            MainMenuStrip = menuStrip1;
            Margin = new Padding(4, 3, 4, 3);
            MinimumSize = new Size(940, 560);
            Name = "MainForm";
            StartPosition = FormStartPosition.CenterScreen;
            Text = "VirtualCam Config";
            Load += MainForm_Load;
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            videoPanel.ResumeLayout(false);
            panel1.ResumeLayout(false);
            panel1.PerformLayout();
            tableLayoutPanel1.ResumeLayout(false);
            statsPanel.ResumeLayout(false);
            statsLayout.ResumeLayout(false);
            statsLayout.PerformLayout();
            ResumeLayout(false);
            PerformLayout();

        }

        #endregion
        private MenuStrip menuStrip1;
        private ToolStripMenuItem fileToolStripMenuItem;
        private ToolStripMenuItem settingsToolStripMenuItem;
        private ToolStripMenuItem guideToolStripMenuItem;
        private ToolStripMenuItem aboutToolStripMenuItem;
        private ToolStripMenuItem exitToolStripMenuItem;
        private ToolStripMenuItem clearHistoryToolStripMenuItem;
        private ToolStripSeparator fileMenuSeparator;
        private Panel videoPanel;
        private Label previewStatusLabel;
        private Panel panel1;
        private Label streamProp_lbl;
        private Label label2;
        private Label pathLabel;
        private ComboBox pathTextBox;
        private Button playButton;
        private Button startVCamButton;
        private TableLayoutPanel tableLayoutPanel1;
        private Panel statsPanel;
        private TableLayoutPanel statsLayout;
        private Label connHeader;
        private Label statsHeader;
        private ListView connList;
        private ListView statsList;
        private ColumnHeader connPropCol;
        private ColumnHeader connValueCol;
        private ColumnHeader statsPropCol;
        private ColumnHeader statsValueCol;
    }
}

