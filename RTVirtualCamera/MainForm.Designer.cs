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
            ListViewItem lvContainer = new ListViewItem(new string[] { "Contenitore", "—" });
            lvContainer.Name = "container";
            ListViewItem lvTransport = new ListViewItem(new string[] { "Trasporto", "—" });
            lvTransport.Name = "transport";
            ListViewItem lvCodec = new ListViewItem(new string[] { "Codec", "—" });
            lvCodec.Name = "codec";
            ListViewItem lvPixfmt = new ListViewItem(new string[] { "Formato pixel", "—" });
            lvPixfmt.Name = "pixfmt";
            ListViewItem lvResolution = new ListViewItem(new string[] { "Risoluzione", "—" });
            lvResolution.Name = "resolution";
            ListViewItem lvFps = new ListViewItem(new string[] { "Frame rate", "—" });
            lvFps.Name = "fps";
            ListViewItem lvBitrate = new ListViewItem(new string[] { "Bitrate", "—" });
            lvBitrate.Name = "bitrate";
            ListViewItem lvState = new ListViewItem(new string[] { "Stato", "—" });
            lvState.Name = "state";
            ListViewItem lvEngine = new ListViewItem(new string[] { "Motore", "—" });
            lvEngine.Name = "engine";
            ListViewItem lvDecode = new ListViewItem(new string[] { "Decodifica", "—" });
            lvDecode.Name = "decode";
            ListViewItem lvRx = new ListViewItem(new string[] { "RX (fps)", "—" });
            lvRx.Name = "rx";
            ListViewItem lvRender = new ListViewItem(new string[] { "Render (fps)", "—" });
            lvRender.Name = "render";
            ListViewItem lvDup = new ListViewItem(new string[] { "Duplicati (fps)", "—" });
            lvDup.Name = "dup";
            ListViewItem lvDrop = new ListViewItem(new string[] { "Persi (fps)", "—" });
            lvDrop.Name = "drop";
            ListViewItem lvProc = new ListViewItem(new string[] { "Elaborazione (ms)", "—" });
            lvProc.Name = "proc";
            ListViewItem lvDrift = new ListViewItem(new string[] { "Drift (ms)", "—" });
            lvDrift.Name = "drift";
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
            // statsPanel
            //
            statsPanel.Controls.Add(statsLayout);
            statsPanel.Dock = DockStyle.Right;
            statsPanel.Location = new Point(575, 24);
            statsPanel.Name = "statsPanel";
            statsPanel.Padding = new Padding(6, 4, 6, 6);
            statsPanel.Size = new Size(340, 752);
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
            statsLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 50F));
            statsLayout.RowStyles.Add(new RowStyle());
            statsLayout.RowStyles.Add(new RowStyle(SizeType.Percent, 50F));
            statsLayout.Size = new Size(328, 744);
            statsLayout.TabIndex = 0;
            //
            // connHeader
            //
            connHeader.AutoSize = true;
            connHeader.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            connHeader.Location = new Point(2, 8);
            connHeader.Margin = new Padding(2, 8, 2, 2);
            connHeader.Name = "connHeader";
            connHeader.Size = new Size(140, 15);
            connHeader.TabIndex = 0;
            connHeader.Text = "Connessione (FFmpeg)";
            //
            // connList
            //
            connList.Columns.AddRange(new ColumnHeader[] { connPropCol, connValueCol });
            connList.Dock = DockStyle.Fill;
            connList.FullRowSelect = true;
            connList.GridLines = true;
            connList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            connList.Items.AddRange(new ListViewItem[] { lvContainer, lvTransport, lvCodec, lvPixfmt, lvResolution, lvFps, lvBitrate });
            connList.Location = new Point(3, 28);
            connList.MultiSelect = false;
            connList.Name = "connList";
            connList.Size = new Size(322, 340);
            connList.TabIndex = 1;
            connList.UseCompatibleStateImageBehavior = false;
            connList.View = View.Details;
            //
            // connPropCol
            //
            connPropCol.Text = "Proprietà";
            connPropCol.Width = 130;
            //
            // connValueCol
            //
            connValueCol.Text = "Valore";
            connValueCol.Width = 178;
            //
            // statsHeader
            //
            statsHeader.AutoSize = true;
            statsHeader.Font = new Font("Segoe UI", 9F, FontStyle.Bold);
            statsHeader.Location = new Point(2, 379);
            statsHeader.Margin = new Padding(2, 8, 2, 2);
            statsHeader.Name = "statsHeader";
            statsHeader.Size = new Size(96, 15);
            statsHeader.TabIndex = 2;
            statsHeader.Text = "Statistiche live";
            //
            // statsList
            //
            statsList.Columns.AddRange(new ColumnHeader[] { statsPropCol, statsValueCol });
            statsList.Dock = DockStyle.Fill;
            statsList.FullRowSelect = true;
            statsList.GridLines = true;
            statsList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            statsList.Items.AddRange(new ListViewItem[] { lvState, lvEngine, lvDecode, lvRx, lvRender, lvDup, lvDrop, lvProc, lvDrift });
            statsList.Location = new Point(3, 399);
            statsList.MultiSelect = false;
            statsList.Name = "statsList";
            statsList.Size = new Size(322, 342);
            statsList.TabIndex = 3;
            statsList.UseCompatibleStateImageBehavior = false;
            statsList.View = View.Details;
            //
            // statsPropCol
            //
            statsPropCol.Text = "Metrica";
            statsPropCol.Width = 130;
            //
            // statsValueCol
            //
            statsValueCol.Text = "Valore";
            statsValueCol.Width = 178;
            //
            // MainForm
            //
            resources.ApplyResources(this, "$this");
            AutoScaleMode = AutoScaleMode.Font;
            // Dock priority is by ascending child index: menuStrip (added last, top,
            // full width) > statsPanel (right sidebar, below the menu) > tableLayoutPanel1
            // (added first, fills the remaining area). Keep this add order.
            Controls.Add(tableLayoutPanel1);
            Controls.Add(statsPanel);
            Controls.Add(menuStrip1);
            MainMenuStrip = menuStrip1;
            MinimumSize = new Size(940, 560);
            Name = "MainForm";
            Load += MainForm_Load;
            menuStrip1.ResumeLayout(false);
            menuStrip1.PerformLayout();
            videoPanel.ResumeLayout(false);
            panel1.ResumeLayout(false);
            panel1.PerformLayout();
            tableLayoutPanel1.ResumeLayout(false);
            statsLayout.ResumeLayout(false);
            statsLayout.PerformLayout();
            statsPanel.ResumeLayout(false);
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

