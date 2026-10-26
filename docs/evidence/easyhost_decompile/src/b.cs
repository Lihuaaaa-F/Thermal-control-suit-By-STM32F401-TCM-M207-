using System;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO.Ports;
using System.Linq;
using System.Text.RegularExpressions;
using System.Windows.Forms;

public class b : UserControl
{
	private string m_a = "Serial, COM1, 57600";

	private e m_b;

	private IContainer m_c;

	private ComboBox m_d;

	private Panel m_e;

	private ComboBox f;

	private ComboBox g;

	private Panel h;

	private ComboBox i;

	public b()
	{
		a();
		this.m_b = new e();
		this.m_d.SelectedIndex = 0;
		g.Items.Clear();
		ComboBox.ObjectCollection items = g.Items;
		object[] portNames = SerialPort.GetPortNames();
		items.AddRange(portNames);
		if (g.SelectedItem == null && g.Items.Count > 0)
		{
			g.SelectedIndex = 0;
		}
		f.SelectedIndex = 0;
		e();
		if (i.Items.Count == 0)
		{
			i.Items.Add("192.168.0.1:1030");
		}
		i.SelectedIndex = 0;
	}

	private void b(object A_0, EventArgs A_1)
	{
		if (this.m_d.SelectedIndex == 0)
		{
			this.m_e.Visible = true;
			h.Visible = false;
		}
		else
		{
			this.m_e.Visible = false;
			h.Visible = true;
		}
	}

	public void a(string A_0)
	{
		if (string.IsNullOrEmpty(A_0))
		{
			return;
		}
		string[] array = A_0.Replace(" ", "").Split(',');
		if (array.Length < 2)
		{
			return;
		}
		try
		{
			string text = array[0].ToLower();
			if (!(text == "tcp"))
			{
				if (text == "serial")
				{
					this.m_d.SelectedItem = "Serial";
					if (array.Length > 1)
					{
						g.SelectedItem = array[1];
					}
					if (array.Length > 2)
					{
						f.SelectedItem = array[2];
					}
				}
			}
			else
			{
				this.m_d.SelectedItem = "TCP";
				if (array.Length > 1)
				{
					i.Text = array[1];
				}
			}
		}
		catch (Exception)
		{
		}
	}

	public string b()
	{
		if (this.m_d.SelectedIndex == 0)
		{
			this.m_a = "Serial, " + g.Text + ", " + f.Text;
		}
		else
		{
			if (!new Regex("^((25[0-5]|2[0-4]\\d|[01]?\\d\\d?)\\.){3}(25[0-5]|2[0-4]\\d|[01]?\\d\\d?):([0-9]|[1-9]\\d{1,3}|[1-5]\\d{4}|6[0-5]{2}[0-3][5])$", RegexOptions.Compiled).IsMatch(i.Text))
			{
				return string.Empty;
			}
			this.m_a = "TCP, " + i.Text;
		}
		return this.m_a;
	}

	public string c()
	{
		return this.m_a;
	}

	public void a(string A_0, DateTime A_1)
	{
		this.m_b.a(A_0, A_1);
	}

	public void d()
	{
		this.m_b.b();
	}

	public void e()
	{
		i.Items.Clear();
		foreach (DataRowView item in this.m_b.c("TCP"))
		{
			string[] array = item["Connection"].ToString().Replace(" ", "").Split(',');
			if (!i.Items.Contains(array[1]))
			{
				i.Items.Add(array[1]);
			}
		}
	}

	private void a(object A_0, EventArgs A_1)
	{
		object selectedItem = g.SelectedItem;
		g.Items.Clear();
		string[] portNames = SerialPort.GetPortNames();
		ComboBox.ObjectCollection items = g.Items;
		object[] items2 = portNames;
		items.AddRange(items2);
		try
		{
			if (selectedItem != null && portNames.Contains(selectedItem.ToString()))
			{
				g.SelectedItem = selectedItem;
			}
			else if (g.Items.Count > 0)
			{
				g.SelectedIndex = 0;
			}
		}
		catch
		{
			if (g.Items.Count > 0)
			{
				g.SelectedIndex = 0;
			}
			else
			{
				MessageBox.Show("请检查计算机的硬件串口是否正常！", "没有可用的串口");
			}
		}
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing && this.m_c != null)
		{
			this.m_c.Dispose();
		}
		base.Dispose(disposing);
	}

	private void a()
	{
		this.m_d = new ComboBox();
		this.m_e = new Panel();
		f = new ComboBox();
		g = new ComboBox();
		h = new Panel();
		i = new ComboBox();
		this.m_e.SuspendLayout();
		h.SuspendLayout();
		SuspendLayout();
		this.m_d.DropDownStyle = ComboBoxStyle.DropDownList;
		this.m_d.FormattingEnabled = true;
		this.m_d.Items.AddRange(new object[2] { "Serial", "TCP" });
		this.m_d.Location = new Point(3, 3);
		this.m_d.Name = "comboBox_Type";
		this.m_d.Size = new Size(87, 20);
		this.m_d.TabIndex = 0;
		this.m_d.SelectedIndexChanged += b;
		this.m_e.Controls.Add(f);
		this.m_e.Controls.Add(g);
		this.m_e.Location = new Point(96, 0);
		this.m_e.Name = "panel_serial";
		this.m_e.Size = new Size(255, 26);
		this.m_e.TabIndex = 1;
		f.DropDownStyle = ComboBoxStyle.DropDownList;
		f.FormattingEnabled = true;
		f.Items.AddRange(new object[6] { "9600", "19200", "38400", "57600", "115200", "230400" });
		f.Location = new Point(130, 3);
		f.Name = "comboBox_Baudrate";
		f.Size = new Size(121, 20);
		f.TabIndex = 1;
		g.DropDownStyle = ComboBoxStyle.DropDownList;
		g.FormattingEnabled = true;
		g.Location = new Point(3, 3);
		g.Name = "comboBox_PortName";
		g.Size = new Size(121, 20);
		g.TabIndex = 0;
		g.Click += a;
		h.Controls.Add(i);
		h.Location = new Point(96, 0);
		h.Name = "panel_tcp";
		h.Size = new Size(255, 26);
		h.TabIndex = 2;
		i.FormattingEnabled = true;
		i.Location = new Point(4, 3);
		i.Name = "comboBox_IP";
		i.Size = new Size(247, 20);
		i.TabIndex = 0;
		base.AutoScaleDimensions = new SizeF(6f, 12f);
		base.AutoScaleMode = AutoScaleMode.Font;
		base.Controls.Add(h);
		base.Controls.Add(this.m_e);
		base.Controls.Add(this.m_d);
		base.Name = "CommSelector";
		base.Size = new Size(355, 29);
		this.m_e.ResumeLayout(performLayout: false);
		h.ResumeLayout(performLayout: false);
		ResumeLayout(performLayout: false);
	}
}
