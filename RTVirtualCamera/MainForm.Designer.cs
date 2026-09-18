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
            resources.ApplyResources(menuStrip1, "menuStrip1");
            menuStrip1.Items.AddRange(new ToolStripItem[] { fileToolStripMenuItem, settingsToolStripMenuItem, guideToolStripMenuItem, aboutToolStripMenuItem });
            menuStrip1.Name = "menuStrip1";
            // 
            // fileToolStripMenuItem
            // 
            resources.ApplyResources(fileToolStripMenuItem, "fileToolStripMenuItem");
            fileToolStripMenuItem.DropDownItems.AddRange(new ToolStripItem[] { clearHistoryToolStripMenuItem, fileMenuSeparator, exitToolStripMenuItem });
            fileToolStripMenuItem.Name = "fileToolStripMenuItem";
            //
            // clearHistoryToolStripMenuItem
            //
            resources.ApplyResources(clearHistoryToolStripMenuItem, "clearHistoryToolStripMenuItem");
            clearHistoryToolStripMenuItem.Name = "clearHistoryToolStripMenuItem";
            clearHistoryToolStripMenuItem.Click += clearHistoryToolStripMenuItem_Click;
            //
            // fileMenuSeparator
            //
            resources.ApplyResources(fileMenuSeparator, "fileMenuSeparator");
            fileMenuSeparator.Name = "fileMenuSeparator";
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
            // guideToolStripMenuItem
            // 
            resources.ApplyResources(guideToolStripMenuItem, "guideToolStripMenuItem");
            guideToolStripMenuItem.Name = "guideToolStripMenuItem";
            guideToolStripMenuItem.Click += guideToolStripMenuItem_Click;
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
            pathTextBox.FormattingEnabled = true;
            // Editable field + browsable history dropdown, with inline type-ahead over the
            // stored URLs (like a browser address bar). Items are filled at runtime from
            // Settings.RecentUrls (see MainForm.PopulateUrlHistory).
            pathTextBox.DropDownStyle = ComboBoxStyle.DropDown;
            pathTextBox.AutoCompleteMode = AutoCompleteMode.SuggestAppend;
            pathTextBox.AutoCompleteSource = AutoCompleteSource.ListItems;
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
            resources.ApplyResources(statsPanel, "statsPanel");
            statsPanel.Controls.Add(statsLayout);
            statsPanel.Name = "statsPanel";
            // 
            // statsLayout
            // 
            resources.ApplyResources(statsLayout, "statsLayout");
            statsLayout.Controls.Add(connHeader, 0, 0);
            statsLayout.Controls.Add(connList, 0, 1);
            statsLayout.Controls.Add(statsHeader, 0, 2);
            statsLayout.Controls.Add(statsList, 0, 3);
            statsLayout.Name = "statsLayout";
            // 
            // connHeader
            // 
            resources.ApplyResources(connHeader, "connHeader");
            connHeader.Name = "connHeader";
            // 
            // connList
            // 
            resources.ApplyResources(connList, "connList");
            connList.Columns.AddRange(new ColumnHeader[] { connPropCol, connValueCol });
            connList.FullRowSelect = true;
            connList.GridLines = true;
            connList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            connList.Items.AddRange(new ListViewItem[] { (ListViewItem)resources.GetObject("connList.Items"), (ListViewItem)resources.GetObject("connList.Items1"), (ListViewItem)resources.GetObject("connList.Items2"), (ListViewItem)resources.GetObject("connList.Items3"), (ListViewItem)resources.GetObject("connList.Items4"), (ListViewItem)resources.GetObject("connList.Items5"), (ListViewItem)resources.GetObject("connList.Items6"), (ListViewItem)resources.GetObject("connList.Items7"), (ListViewItem)resources.GetObject("connList.Items8"), (ListViewItem)resources.GetObject("connList.Items9"), (ListViewItem)resources.GetObject("connList.Items10"), (ListViewItem)resources.GetObject("connList.Items11"), (ListViewItem)resources.GetObject("connList.Items12"), (ListViewItem)resources.GetObject("connList.Items13") });
            connList.MultiSelect = false;
            connList.Name = "connList";
            connList.UseCompatibleStateImageBehavior = false;
            connList.View = View.Details;
            // 
            // connPropCol
            // 
            resources.ApplyResources(connPropCol, "connPropCol");
            // 
            // connValueCol
            // 
            resources.ApplyResources(connValueCol, "connValueCol");
            // 
            // statsHeader
            // 
            resources.ApplyResources(statsHeader, "statsHeader");
            statsHeader.Name = "statsHeader";
            // 
            // statsList
            // 
            resources.ApplyResources(statsList, "statsList");
            statsList.Columns.AddRange(new ColumnHeader[] { statsPropCol, statsValueCol });
            statsList.FullRowSelect = true;
            statsList.GridLines = true;
            statsList.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            statsList.Items.AddRange(new ListViewItem[] { (ListViewItem)resources.GetObject("statsList.Items"), (ListViewItem)resources.GetObject("statsList.Items1"), (ListViewItem)resources.GetObject("statsList.Items2"), (ListViewItem)resources.GetObject("statsList.Items3"), (ListViewItem)resources.GetObject("statsList.Items4"), (ListViewItem)resources.GetObject("statsList.Items5"), (ListViewItem)resources.GetObject("statsList.Items6"), (ListViewItem)resources.GetObject("statsList.Items7"), (ListViewItem)resources.GetObject("statsList.Items8") });
            statsList.MultiSelect = false;
            statsList.Name = "statsList";
            statsList.UseCompatibleStateImageBehavior = false;
            statsList.View = View.Details;
            // 
            // statsPropCol
            // 
            resources.ApplyResources(statsPropCol, "statsPropCol");
            // 
            // statsValueCol
            // 
            resources.ApplyResources(statsValueCol, "statsValueCol");
            // 
            // MainForm
            // 
            resources.ApplyResources(this, "$this");
            AutoScaleMode = AutoScaleMode.Font;
            Controls.Add(tableLayoutPanel1);
            Controls.Add(statsPanel);
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

