using System;
using System.Windows.Forms;

namespace RTVirtualCamera
{
    public partial class About : Form
    {
        public About()
        {
            InitializeComponent();
        }

        private void About_Load(object sender, EventArgs e)
        {
            this.version_lbl.Text = Application.ProductVersion;
        }
    }
}
