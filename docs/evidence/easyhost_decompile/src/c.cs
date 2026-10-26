using System;
using System.ComponentModel;
using System.Drawing;
using System.IO.Ports;
using System.Text.RegularExpressions;
using System.Windows.Forms;

public class c : Form
{
	private string m_a;

	private uint m_b;

	private string m_c;

	private IContainer m_d;

	private ComboBox e;

	private ComboBox f;

	private Button g;

	private Button h;

	private NumericUpDown i;

	private TextBox j;

	private TabControl k;

	private TabPage l;

	private TabPage m;

	private Label n;

	private Label o;

	private Label p;

	private Label q;

	public c(string A_0)
	{
		a();
		A_0 = A_0.Replace(" ", "");
		string[] array = A_0.Split(',');
		e.Items.Clear();
		ComboBox.ObjectCollection items = e.Items;
		object[] portNames = SerialPort.GetPortNames();
		items.AddRange(portNames);
		if (e.SelectedItem == null && e.Items.Count > 0)
		{
			e.SelectedIndex = 0;
		}
		f.SelectedIndex = 0;
		j.Text = "169.254.1.100";
		i.Text = "1030";
		if (array[0].ToLower() == "serial")
		{
			k.SelectedTab = l;
			for (int num = 0; num < e.Items.Count; num++)
			{
				if (e.Items[num].ToString() == array[1])
				{
					e.SelectedIndex = num;
				}
			}
			f.Text = array[2];
		}
		else if (array[0].ToLower() == "tcp")
		{
			k.SelectedTab = m;
			j.Text = array[1];
			i.Text = array[2];
		}
		else
		{
			k.SelectedTab = l;
		}
	}

	public string b()
	{
		return this.m_a;
	}

	public string c()
	{
		return this.m_c;
	}

	public uint d()
	{
		return this.m_b;
	}

	private void a(object A_0, EventArgs A_1)
	{
		if (k.SelectedTab == l)
		{
			this.m_a = "Serial, " + e.Text + ", " + f.Text;
			this.m_c = "Serial";
			this.m_b = Convert.ToUInt32(e.Text.Substring(3)) * 16777216 + Convert.ToUInt32(f.Text);
			return;
		}
		if (!new Regex("^((25[0-4]|2[0-4]|1?\\d{2}|[1-9]?\\d)(\\.(25[0-4]|2[0-4]|1?\\d{2}|[1-9]?\\d)){3})$").IsMatch(j.Text))
		{
			base.DialogResult = DialogResult.None;
			j.Focus();
		}
		string[] array = j.Text.Split('.');
		if (array.Length != 4)
		{
			base.DialogResult = DialogResult.None;
			j.Focus();
		}
		for (int num = 0; num < 4; num++)
		{
			uint num2 = Convert.ToUInt32(array[num]);
			this.m_b |= num2 << 24 - num * 8;
		}
		this.m_a = "TCP, " + j.Text + ", " + i.Text;
		this.m_c = "TCP";
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing && this.m_d != null)
		{
			this.m_d.Dispose();
		}
		base.Dispose(disposing);
	}

	private void a()
	{
		e = new ComboBox();
		f = new ComboBox();
		g = new Button();
		h = new Button();
		j = new TextBox();
		i = new NumericUpDown();
		k = new TabControl();
		l = new TabPage();
		n = new Label();
		o = new Label();
		m = new TabPage();
		p = new Label();
		q = new Label();
		((ISupportInitialize)i).BeginInit();
		k.SuspendLayout();
		l.SuspendLayout();
		m.SuspendLayout();
		SuspendLayout();
		e.FormattingEnabled = true;
		e.Location = new Point(77, 19);
		e.Margin = new Padding(2);
		e.Name = "ComboBox_PortName";
		e.Size = new Size(185, 20);
		e.TabIndex = 0;
		f.FormattingEnabled = true;
		f.Items.AddRange(new object[6] { "9600", "19200", "38400", "57600", "115200", "230400" });
		f.Location = new Point(77, 54);
		f.Margin = new Padding(2);
		f.Name = "comboBox_BaurdRate";
		f.Size = new Size(185, 20);
		f.TabIndex = 2;
		g.DialogResult = DialogResult.OK;
		g.Location = new Point(27, 151);
		g.Margin = new Padding(2);
		g.Name = "button1";
		g.Size = new Size(86, 28);
		g.TabIndex = 4;
		g.Text = "OK";
		g.UseVisualStyleBackColor = true;
		g.Click += a;
		h.DialogResult = DialogResult.Cancel;
		h.Location = new Point(202, 151);
		h.Margin = new Padding(2);
		h.Name = "button2";
		h.Size = new Size(86, 28);
		h.TabIndex = 5;
		h.Text = "Cancel";
		h.UseVisualStyleBackColor = true;
		j.Location = new Point(77, 19);
		j.Margin = new Padding(2);
		j.Name = "textBox_IP";
		j.Size = new Size(185, 21);
		j.TabIndex = 10;
		i.Location = new Point(77, 54);
		i.Margin = new Padding(2);
		i.Maximum = new decimal(new int[4] { 65535, 0, 0, 0 });
		i.Name = "numericUpDown_ipport";
		i.Size = new Size(185, 21);
		i.TabIndex = 9;
		k.Controls.Add(l);
		k.Controls.Add(m);
		k.Location = new Point(12, 12);
		k.Name = "tabControl_conncfg";
		k.SelectedIndex = 0;
		k.Size = new Size(299, 118);
		k.TabIndex = 10;
		l.Controls.Add(n);
		l.Controls.Add(o);
		l.Controls.Add(e);
		l.Controls.Add(f);
		l.Location = new Point(4, 22);
		l.Name = "tabPage_serial";
		l.Padding = new Padding(3);
		l.Size = new Size(291, 92);
		l.TabIndex = 0;
		l.Text = "Serial";
		l.UseVisualStyleBackColor = true;
		n.AutoSize = true;
		n.Location = new Point(13, 57);
		n.Name = "label_serialbaudrate";
		n.Size = new Size(59, 12);
		n.TabIndex = 4;
		n.Text = "Baudrate:";
		n.TextAlign = ContentAlignment.TopRight;
		o.AutoSize = true;
		o.Location = new Point(43, 22);
		o.Name = "label_serialport";
		o.Size = new Size(29, 12);
		o.TabIndex = 3;
		o.Text = "COM:";
		o.TextAlign = ContentAlignment.TopRight;
		m.Controls.Add(p);
		m.Controls.Add(q);
		m.Controls.Add(i);
		m.Controls.Add(j);
		m.Location = new Point(4, 22);
		m.Name = "tabPage_tcp";
		m.Padding = new Padding(3);
		m.Size = new Size(291, 92);
		m.TabIndex = 1;
		m.Text = "TCP";
		m.UseVisualStyleBackColor = true;
		p.AutoSize = true;
		p.Location = new Point(36, 56);
		p.Name = "label_tcp_port";
		p.Size = new Size(35, 12);
		p.TabIndex = 12;
		p.Text = "Port:";
		p.TextAlign = ContentAlignment.TopRight;
		q.AutoSize = true;
		q.Location = new Point(48, 22);
		q.Name = "label_tcp_ip";
		q.Size = new Size(23, 12);
		q.TabIndex = 11;
		q.Text = "IP:";
		q.TextAlign = ContentAlignment.TopRight;
		base.AutoScaleDimensions = new SizeF(6f, 12f);
		base.AutoScaleMode = AutoScaleMode.Font;
		base.ClientSize = new Size(319, 190);
		base.Controls.Add(k);
		base.Controls.Add(h);
		base.Controls.Add(g);
		base.Margin = new Padding(2);
		base.MaximizeBox = false;
		base.MinimizeBox = false;
		base.Name = "CommucationSettingDlg";
		base.StartPosition = FormStartPosition.CenterScreen;
		Text = "Connection";
		((ISupportInitialize)i).EndInit();
		k.ResumeLayout(performLayout: false);
		l.ResumeLayout(performLayout: false);
		l.PerformLayout();
		m.ResumeLayout(performLayout: false);
		m.PerformLayout();
		ResumeLayout(performLayout: false);
	}
}
