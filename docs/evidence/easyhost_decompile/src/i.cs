using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Reflection;
using System.Windows.Forms;

public class i : Form
{
	private h m_a;

	private IContainer m_b;

	private TextBox m_c;

	private Label d;

	private Label e;

	private Label f;

	private Button g;

	private Button h;

	private ListBox m_i;

	private ListBox j;

	private TextBox k;

	private PictureBox l;

	internal i(string A_0, string A_1, string A_2, string A_3, string A_4, h A_5)
	{
		a();
		Text = A_0;
		d.Text = A_1;
		e.Text = A_2;
		f.Text = A_3;
		this.m_c.Text = A_4;
		m_i.Items.Add(A_2);
		this.m_a = A_5;
		this.m_a.h("");
		a(k.Text);
		Bitmap image = new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.search.png"));
		l.Image = image;
	}

	public string b()
	{
		return m_i.Text;
	}

	public string c()
	{
		return this.m_c.Text;
	}

	private void a(string A_0)
	{
		j.Items.Clear();
		List<string> list = this.m_a.h(A_0);
		for (int num = 0; num < list.Count; num++)
		{
			j.Items.Add(list[num]);
		}
		if (j.Items.Count > 0)
		{
			j.SelectedIndex = 0;
		}
		else
		{
			m_i.Items.Clear();
		}
	}

	private void b(object A_0, EventArgs A_1)
	{
		a(k.Text);
	}

	private void a(object A_0, EventArgs A_1)
	{
		m_i.Items.Clear();
		if (j.SelectedItems.Count > 0)
		{
			List<string> list = this.m_a.i(j.SelectedItems[0].ToString());
			for (int num = 0; num < list.Count; num++)
			{
				m_i.Items.Add(list[num]);
			}
			if (m_i.Items.Count > 0)
			{
				m_i.SelectedIndex = 0;
			}
		}
	}

	private void a(object A_0, MouseEventArgs A_1)
	{
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing && this.m_b != null)
		{
			this.m_b.Dispose();
		}
		base.Dispose(disposing);
	}

	private void a()
	{
		this.m_c = new TextBox();
		d = new Label();
		e = new Label();
		f = new Label();
		g = new Button();
		h = new Button();
		m_i = new ListBox();
		j = new ListBox();
		k = new TextBox();
		l = new PictureBox();
		((ISupportInitialize)l).BeginInit();
		SuspendLayout();
		this.m_c.Location = new Point(30, 494);
		this.m_c.Margin = new Padding(4);
		this.m_c.Name = "textBox1";
		this.m_c.Size = new Size(937, 35);
		this.m_c.TabIndex = 3;
		d.Location = new Point(26, 41);
		d.Margin = new Padding(4, 0, 4, 0);
		d.Name = "label1";
		d.Size = new Size(164, 24);
		d.TabIndex = 3;
		d.Text = "label1";
		e.Location = new Point(401, 41);
		e.Margin = new Padding(4, 0, 4, 0);
		e.Name = "label2";
		e.Size = new Size(164, 24);
		e.TabIndex = 4;
		e.Text = "label2";
		f.Location = new Point(26, 461);
		f.Margin = new Padding(4, 0, 4, 0);
		f.Name = "label3";
		f.Size = new Size(160, 24);
		f.TabIndex = 5;
		f.Text = "label3";
		g.DialogResult = DialogResult.OK;
		g.Location = new Point(148, 587);
		g.Margin = new Padding(4);
		g.Name = "button1";
		g.Size = new Size(200, 50);
		g.TabIndex = 6;
		g.Text = "OK";
		g.UseVisualStyleBackColor = true;
		h.DialogResult = DialogResult.Cancel;
		h.Location = new Point(628, 587);
		h.Margin = new Padding(4);
		h.Name = "button2";
		h.Size = new Size(200, 50);
		h.TabIndex = 7;
		h.Text = "Cancel";
		h.UseVisualStyleBackColor = true;
		m_i.FormattingEnabled = true;
		m_i.ItemHeight = 24;
		m_i.Location = new Point(405, 73);
		m_i.Margin = new Padding(6);
		m_i.Name = "listBox1";
		m_i.Size = new Size(562, 340);
		m_i.TabIndex = 2;
		j.FormattingEnabled = true;
		j.ItemHeight = 24;
		j.Location = new Point(30, 121);
		j.Name = "listBoxModel";
		j.Size = new Size(339, 292);
		j.TabIndex = 1;
		j.MouseClick += a;
		j.SelectedIndexChanged += a;
		k.Location = new Point(30, 73);
		k.Name = "textBoxKeyWord";
		k.Size = new Size(339, 35);
		k.TabIndex = 0;
		k.TextChanged += b;
		l.BackColor = SystemColors.HighlightText;
		l.Location = new Point(333, 75);
		l.Name = "pictureBoxSearch";
		l.Padding = new Padding(2);
		l.Size = new Size(34, 34);
		l.SizeMode = PictureBoxSizeMode.StretchImage;
		l.TabIndex = 11;
		l.TabStop = false;
		base.AutoScaleDimensions = new SizeF(12f, 24f);
		base.AutoScaleMode = AutoScaleMode.Font;
		base.ClientSize = new Size(997, 668);
		base.Controls.Add(l);
		base.Controls.Add(k);
		base.Controls.Add(j);
		base.Controls.Add(m_i);
		base.Controls.Add(h);
		base.Controls.Add(g);
		base.Controls.Add(f);
		base.Controls.Add(e);
		base.Controls.Add(d);
		base.Controls.Add(this.m_c);
		base.FormBorderStyle = FormBorderStyle.FixedDialog;
		base.Margin = new Padding(4);
		base.MaximizeBox = false;
		base.MinimizeBox = false;
		base.Name = "ModelSelectDlg";
		base.StartPosition = FormStartPosition.CenterScreen;
		Text = "ModelSelectDlg";
		((ISupportInitialize)l).EndInit();
		ResumeLayout(performLayout: false);
		PerformLayout();
	}
}
