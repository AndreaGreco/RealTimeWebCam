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
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(SettingsForm));
            this.radioLanguage_IT = new System.Windows.Forms.RadioButton();
            this.radioLanguageEn = new System.Windows.Forms.RadioButton();
            this.groupBox1 = new System.Windows.Forms.GroupBox();
            this.radioLanguageSystem = new System.Windows.Forms.RadioButton();
            this.groupBox2 = new System.Windows.Forms.GroupBox();
            this.AutoStartCheckBox = new System.Windows.Forms.CheckBox();
            this.tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
            this.groupBox1.SuspendLayout();
            this.groupBox2.SuspendLayout();
            this.tableLayoutPanel1.SuspendLayout();
            this.SuspendLayout();
            // 
            // radioLanguage_IT
            // 
            resources.ApplyResources(this.radioLanguage_IT, "radioLanguage_IT");
            this.radioLanguage_IT.Name = "radioLanguage_IT";
            this.radioLanguage_IT.TabStop = true;
            this.radioLanguage_IT.UseVisualStyleBackColor = true;
            this.radioLanguage_IT.CheckedChanged += new System.EventHandler(this.radioLanguage_IT_CheckedChanged);
            // 
            // radioLanguageEn
            // 
            resources.ApplyResources(this.radioLanguageEn, "radioLanguageEn");
            this.radioLanguageEn.Name = "radioLanguageEn";
            this.radioLanguageEn.TabStop = true;
            this.radioLanguageEn.UseVisualStyleBackColor = true;
            this.radioLanguageEn.CheckedChanged += new System.EventHandler(this.radioLanguageEn_CheckedChanged);
            // 
            // groupBox1
            // 
            resources.ApplyResources(this.groupBox1, "groupBox1");
            this.groupBox1.Controls.Add(this.radioLanguageSystem);
            this.groupBox1.Controls.Add(this.radioLanguage_IT);
            this.groupBox1.Controls.Add(this.radioLanguageEn);
            this.groupBox1.Name = "groupBox1";
            this.groupBox1.TabStop = false;
            this.groupBox1.Enter += new System.EventHandler(this.groupBox1_Enter);
            // 
            // radioLanguageSystem
            // 
            resources.ApplyResources(this.radioLanguageSystem, "radioLanguageSystem");
            this.radioLanguageSystem.Name = "radioLanguageSystem";
            this.radioLanguageSystem.TabStop = true;
            this.radioLanguageSystem.UseVisualStyleBackColor = true;
            this.radioLanguageSystem.CheckedChanged += new System.EventHandler(this.radioLanguageSystem_CheckedChanged);
            // 
            // groupBox2
            // 
            resources.ApplyResources(this.groupBox2, "groupBox2");
            this.groupBox2.Controls.Add(this.AutoStartCheckBox);
            this.groupBox2.Name = "groupBox2";
            this.groupBox2.TabStop = false;
            // 
            // AutoStartCheckBox
            // 
            resources.ApplyResources(this.AutoStartCheckBox, "AutoStartCheckBox");
            this.AutoStartCheckBox.Name = "AutoStartCheckBox";
            this.AutoStartCheckBox.UseVisualStyleBackColor = true;
            this.AutoStartCheckBox.CheckedChanged += new System.EventHandler(this.AutoStartCheckBox_CheckedChanged);
            // 
            // tableLayoutPanel1
            // 
            resources.ApplyResources(this.tableLayoutPanel1, "tableLayoutPanel1");
            this.tableLayoutPanel1.Controls.Add(this.groupBox1, 0, 0);
            this.tableLayoutPanel1.Controls.Add(this.groupBox2, 0, 1);
            this.tableLayoutPanel1.Name = "tableLayoutPanel1";
            // 
            // SettingsForm
            // 
            resources.ApplyResources(this, "$this");
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.Controls.Add(this.tableLayoutPanel1);
            this.Name = "SettingsForm";
            this.Load += new System.EventHandler(this.SettingsForm_Load);
            this.groupBox1.ResumeLayout(false);
            this.groupBox1.PerformLayout();
            this.groupBox2.ResumeLayout(false);
            this.groupBox2.PerformLayout();
            this.tableLayoutPanel1.ResumeLayout(false);
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.RadioButton radioLanguage_IT;
        private System.Windows.Forms.RadioButton radioLanguageEn;
        private System.Windows.Forms.GroupBox groupBox1;
        private System.Windows.Forms.GroupBox groupBox2;
        private System.Windows.Forms.CheckBox AutoStartCheckBox;
        private System.Windows.Forms.RadioButton radioLanguageSystem;
        private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
    }
}