using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Timers;
using System.Windows.Forms;
using System.Windows.Forms.DataVisualization.Charting;
using System.Windows.Forms.VisualStyles;
using System.Xml;
using System.Xml.Linq;
using JoyD.Windows.CS;

public class f : Form
{
	private enum a
	{
		a,
		b,
		c,
		d,
		e,
		f
	}

	private enum b
	{
		a,
		b,
		c,
		d,
		e,
		f,
		g
	}

	private delegate bool c();

	private class d
	{
		[CompilerGenerated]
		private b m_a;

		[CompilerGenerated]
		private string m_b;

		[CompilerGenerated]
		private string m_c;

		[CompilerGenerated]
		private bool m_d;

		[CompilerGenerated]
		private c m_e;

		[SpecialName]
		[CompilerGenerated]
		public b a()
		{
			return this.m_a;
		}

		[SpecialName]
		[CompilerGenerated]
		public void a(b A_0)
		{
			this.m_a = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public string b()
		{
			return this.m_b;
		}

		[SpecialName]
		[CompilerGenerated]
		public void a(string A_0)
		{
			this.m_b = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public string c()
		{
			return this.m_c;
		}

		[SpecialName]
		[CompilerGenerated]
		public void b(string A_0)
		{
			this.m_c = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public bool d()
		{
			return this.m_d;
		}

		[SpecialName]
		[CompilerGenerated]
		public void a(bool A_0)
		{
			this.m_d = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public c e()
		{
			return this.m_e;
		}

		[SpecialName]
		[CompilerGenerated]
		public void a(c A_0)
		{
			this.m_e = A_0;
		}
	}

	private class e : TabControl
	{
		protected override void OnHandleCreated(EventArgs e)
		{
			SetWindowTheme(base.Handle, "", "");
			base.OnHandleCreated(e);
		}

		[DllImport("uxtheme.dll")]
		private static extern int SetWindowTheme(IntPtr A_0, string A_1, string A_2);
	}

	[Serializable]
	[CompilerGenerated]
	private sealed class f
	{
		public static readonly f a = new f();

		public static Func<XElement, XElement> b;

		internal XElement a(XElement A_0)
		{
			return A_0;
		}
	}

	[CompilerGenerated]
	private sealed class g
	{
		public ListViewItem a;

		public global::f b;

		internal void c()
		{
			b.fm.Items.Add(a);
		}
	}

	[CompilerGenerated]
	private sealed class h
	{
		public string a;

		public global::f b;

		internal void c()
		{
			b.m_u = global::f.a.a;
			b.fq.Enabled = true;
			b.fu.Enabled = true;
			b.fr.Enabled = true;
			b.fs.Enabled = true;
			string text = b.m_w.b();
			if (string.IsNullOrWhiteSpace(text))
			{
				b.b($"更新CRC失败：未指定文件名", Color.Red);
				return;
			}
			foreach (ListViewItem item in b.fm.Items)
			{
				if (item.SubItems[0].Text == text)
				{
					item.SubItems[2].Text = a;
					break;
				}
			}
			if (!b.m_v)
			{
				b.b($"文件 '{text}'的CRC: {a}", Color.Green);
			}
			b.v();
		}
	}

	[CompilerGenerated]
	private sealed class i
	{
		public global::f a;

		public string b;

		public int c;

		public string d;

		internal void e()
		{
			int num = a.di.e(b);
			if (num == 0)
			{
				a.a("Error: Not find the right model file！", Color.Red);
			}
			else if (num == 1)
			{
				a.a(a.m_bc + a.m_bd + a.m_bi + a.di.f(b) + ".xml", A_1: false, c);
			}
			else if (num > 1)
			{
				a.di.c(b);
				if (a.di.d(b) > 5)
				{
					a.a(a.m_bc + a.m_bd + a.m_bi + a.di.f(b) + ".xml", A_1: false, c);
				}
				else
				{
					a.b6.d();
					a.cp.Enqueue("SYSTEM:SYSVERSION?@0");
				}
			}
		}

		internal void f()
		{
			a.a(a.m_bc + a.m_bd + a.m_bi + a.di.g(b) + ".xml", A_1: false, c);
		}

		internal void g()
		{
			try
			{
				DataRow[] array = a.m_g.Select("Addr = " + c + " AND ParamName = '" + d + "'");
				foreach (DataRow dataRow in array)
				{
					string text = a.a(dataRow["ParamType"].ToString(), c);
					if (text == "HSTR" || text.EndsWith("[HSTR]"))
					{
						if (text == "HSTR")
						{
							text = "NUM";
						}
						else if (text.EndsWith("[HSTR]"))
						{
							text = text.Substring(0, text.Length - 6);
						}
						string[] array2 = b.Split('[');
						b = array2[0];
						if (array2.Count() > 1)
						{
							string[] array3 = array2[1].Split(']')[0].Split(',');
							if (array3.Count() >= 3 && array3[2].Count() > 0)
							{
								text = array3[2];
							}
							if (array3.Count() >= 2 && array3[1].Count() > 0)
							{
								dataRow["ParamText"] = array3[1];
							}
							if (array3.Count() >= 1 && array3[0].Count() > 0)
							{
								dataRow["Unit"] = array3[0];
							}
						}
					}
					if (text == "NUM")
					{
						if (b.Length > 0)
						{
							double num2 = Convert.ToDouble(b);
							dataRow["Paramvalue"] = num2;
							dataRow["ValueText"] = num2;
						}
						else
						{
							dataRow["Paramvalue"] = 0;
							dataRow["ValueText"] = 0;
						}
					}
					else if (text.Substring(0, 3) == "STR")
					{
						dataRow["ParamValue"] = b;
						dataRow["ValueText"] = b;
					}
					else if (b.Length > 0)
					{
						dataRow["ParamValue"] = b;
						dataRow["ValueText"] = a.e(b, text);
					}
					else
					{
						dataRow["ParamValue"] = b;
						dataRow["ValueText"] = b;
					}
				}
			}
			catch (Exception ex)
			{
				a.a("Error: when analyzing received parameter data！" + ex.Message, Color.Red);
			}
		}
	}

	[CompilerGenerated]
	private sealed class j
	{
		public string a;

		public global::f b;

		public int c;

		internal void d()
		{
			try
			{
				int num = 0;
				int num2 = 0;
				string value = a.Substring(0, 3);
				string value2 = a.Substring(6, 3);
				num = Convert.ToInt32(value);
				num2 = Convert.ToInt32(value2);
				b.a(num, num2, c);
				string value3 = a.Substring(12, 3);
				string value4 = a.Substring(15, 3);
				string value5 = a.Substring(18, 3);
				string a_ = a.Substring(21, a.Length - 21);
				int num3 = Convert.ToInt32(value3);
				int num4 = Convert.ToInt32(value4);
				int num5 = Convert.ToInt32(value5);
				string value6 = b.c(num5);
				string value7 = b.m(a_);
				b.m_av.RowFilter = "Addr = " + c;
				b.m_av.Sort = "Index ASC";
				if (num3 > b.m_av.Count)
				{
					DataRow dataRow = b.m_l.NewRow();
					dataRow["Addr"] = c;
					dataRow["Count"] = num3;
					dataRow["Index"] = num4;
					dataRow["ErrorCode"] = num5;
					dataRow["ErrorText"] = value6;
					dataRow["ModuleText"] = value7;
					b.m_l.Rows.Add(dataRow);
				}
				else if (num3 < b.m_av.Count)
				{
					while (num3 < b.m_av.Count)
					{
						b.m_av.Delete(b.m_av.Count - 1);
						b.m_l.AcceptChanges();
					}
				}
				if (num3 > 0 && num3 == b.m_av.Count)
				{
					b.m_av[num4 - 1]["Addr"] = c;
					b.m_av[num4 - 1]["Count"] = num3;
					b.m_av[num4 - 1]["Index"] = num4;
					b.m_av[num4 - 1]["ErrorCode"] = num5;
					b.m_av[num4 - 1]["ErrorText"] = value6;
					b.m_av[num4 - 1]["ModuleText"] = value7;
				}
			}
			catch (Exception ex)
			{
				b.p("Error when analy menu:info, " + ex.Message);
			}
		}
	}

	[CompilerGenerated]
	private sealed class k
	{
		public string a;

		public global::f b;

		public int c;

		internal void d()
		{
			try
			{
				string value = a.Substring(0, 3);
				string value2 = a.Substring(3, 3);
				string value3 = a.Substring(6, 3);
				string a_ = a.Substring(9, a.Length - 9);
				int num = Convert.ToInt32(value);
				int num2 = Convert.ToInt32(value2);
				int num3 = Convert.ToInt32(value3);
				b.m_av.RowFilter = "Addr = " + c;
				b.m_av.Sort = "Index ASC";
				if (num > b.m_av.Count)
				{
					DataRow dataRow = b.m_l.NewRow();
					dataRow["Addr"] = c;
					dataRow["Count"] = num;
					dataRow["Index"] = num2;
					dataRow["ErrorCode"] = num3;
					dataRow["ErrorText"] = b.c(num3);
					dataRow["ModuleText"] = b.m(a_);
					b.m_l.Rows.Add(dataRow);
				}
				else if (num < b.m_av.Count)
				{
					b.m_av.Delete(b.m_av.Count - 1);
					b.m_l.AcceptChanges();
				}
				if (num > 0 && num == b.m_av.Count)
				{
					b.m_av[num2 - 1]["Addr"] = c;
					b.m_av[num2 - 1]["Count"] = num;
					b.m_av[num2 - 1]["Index"] = num2;
					b.m_av[num2 - 1]["ErrorCode"] = num3;
					b.m_av[num2 - 1]["ErrorText"] = b.c(num3);
					b.m_av[num2 - 1]["ModuleText"] = b.m(a_);
				}
			}
			catch (Exception ex)
			{
				b.p("Error when analy error report, " + ex.Message + a);
			}
		}
	}

	[CompilerGenerated]
	private sealed class l
	{
		public global::f a;

		public string b;

		internal void c()
		{
			a.ef.Text = "Log: " + b + " (" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ")";
			a.b0 = DateTime.Now;
			if (a.bx != null && a.bz)
			{
				a.by.Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ": " + b);
				a.bx.WriteLine(a.by.ToString());
				a.by.Clear();
			}
		}
	}

	[CompilerGenerated]
	private sealed class m
	{
		public global::f a;

		public string b;

		public Color c;

		internal void d()
		{
			a.ef.Text = b + "(" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ")";
			a.ef.ForeColor = c;
			a.b0 = DateTime.Now;
			if (a.bx != null && a.bz)
			{
				a.by.Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ": " + b);
				a.bx.WriteLine(a.by.ToString());
				a.by.Clear();
			}
		}
	}

	[CompilerGenerated]
	private sealed class n
	{
		public string a;

		public global::f b;

		public Color c;

		internal void d()
		{
			string text = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ": " + a + "\n";
			b.fy.SelectionStart = b.fy.TextLength;
			b.fy.SelectionColor = c;
			b.fy.AppendText(text);
			string[] lines = b.fy.Lines;
			if (lines.Length > 500)
			{
				string[] array = new string[100];
				Array.Copy(lines, lines.Length - 100, array, 0, 100);
				b.fy.Clear();
				for (int num = 0; num < array.Length; num++)
				{
					b.fy.AppendText(array[num] + "\n");
				}
			}
			b.fy.ScrollToCaret();
			if (b.bx != null && b.bz)
			{
				b.by.Append(text);
				b.bx.WriteLine(text);
				b.by.Clear();
			}
		}
	}

	[CompilerGenerated]
	private sealed class o
	{
		public global::f a;

		public int b;

		public int c;

		internal void d()
		{
			int num = (int)(a.ca * (double)a.m_at.Count * 3.0);
			if (num >= a.cc)
			{
				a.cc = num;
			}
			for (int num2 = 0; num2 < a.m_h.Rows.Count; num2++)
			{
				if (Convert.ToInt32(a.m_h.Rows[num2]["Addr"]) != b)
				{
					continue;
				}
				try
				{
					if (new TimeSpan(DateTime.Now.Ticks - Convert.ToDateTime(a.m_h.Rows[num2]["lastcomtime"]).Ticks).TotalSeconds > (double)a.cc)
					{
						a.m_h.Rows[num2]["constatus"] = 1;
						a.m_h.Rows[num2]["ledsw"] = 0;
						a.m_h.Rows[num2]["lederr"] = 0;
					}
					c = Convert.ToInt32(a.m_h.Rows[num2]["constatus"]);
				}
				catch
				{
				}
			}
		}
	}

	[CompilerGenerated]
	private sealed class p
	{
		public int a;

		public global::f b;

		internal void c()
		{
			b.fw.Value = a;
		}
	}

	[CompilerGenerated]
	private sealed class q
	{
		public int a;

		public global::f b;

		internal void c()
		{
			b.fw.Value = a;
		}
	}

	private const int m_a = 120;

	private string m_b = "zh-CN";

	private string m_c = "微软雅黑";

	private Hashtable m_d = new Hashtable();

	private Process m_e = Process.GetCurrentProcess();

	private DataTable m_f = new DataTable("Table_Model");

	private DataTable m_g = new DataTable("Table_Project");

	private DataTable m_h = new DataTable("Table_LowerPC");

	private DataTable m_i = new DataTable("Table_Button");

	private DataTable m_j = new DataTable("Table_DataRecord");

	private DataTable m_k = new DataTable("Table_ModuleName");

	private DataTable m_l = new DataTable("Table_ErrorReport");

	private DataTable m_m = new DataTable("Table_ErrorCode");

	private DataTable m_n = new DataTable("Table_ReplyCode");

	private DataTable m_o = new DataTable("Table_SWHelp");

	private DataTable m_p = new DataTable("Table_Script");

	private DataTable m_q = new DataTable("Table_Script");

	private DataTable m_r = new DataTable("Table_ScriptCMD");

	private DataTable m_s = new DataTable("Table_DataLevel");

	private DataTable m_t = new DataTable("Table_PrivateOption");

	private a m_u;

	private bool m_v;

	private d m_w;

	private ConcurrentQueue<d> m_x = new ConcurrentQueue<d>();

	private c m_y;

	private string m_z = string.Empty;

	private string m_aa = string.Empty;

	private long m_ab;

	private long m_ac;

	private int m_ad;

	private FileStream m_ae;

	private string m_af;

	private string m_ag;

	private long m_ah;

	private long m_ai;

	private int m_aj;

	private MemoryStream m_ak;

	private uint m_al;

	private DataView m_am;

	private DataView m_an;

	private DataView m_ao;

	private DataView m_ap;

	private DataView m_aq;

	private DataView m_ar;

	private DataView m_as;

	private DataView m_at;

	private DataView m_au;

	private DataView m_av;

	private DataView m_aw;

	private DataView m_ax;

	private DataView m_ay;

	private string m_az = "";

	private ToolStripButton[] m_a0 = new ToolStripButton[40];

	private ToolStripSeparator[] m_a1 = new ToolStripSeparator[40];

	private bool m_a2 = true;

	private string m_a3 = "project";

	private string m_a4 = "";

	private string m_a5 = "";

	private bool m_a6 = true;

	private string m_a7 = "EasyHost";

	private string m_a8 = "6.8";

	private string m_a9 = "newproject";

	private int m_ba;

	private bool m_bb = true;

	private string m_bc = Application.StartupPath + "\\";

	private string m_bd = "Lang\\zh-CN\\";

	private string m_be = Application.StartupPath + "\\ini.xml";

	private string m_bf = Application.StartupPath + "\\ProjectFile\\";

	private string m_bg = Application.StartupPath + "\\ScriptFile\\";

	private string m_bh = Application.StartupPath + "\\UserData\\";

	private string m_bi = "ModelFile\\";

	private string m_bj = "Public\\";

	private string m_bk = "helppic\\";

	private string m_bl = "modulename.xml";

	private string m_bm = "swhelp.xml";

	private string m_bn = "errorcode.xml";

	private string m_bo = "replycode.xml";

	private bool m_bp;

	private string bq = "1.txt";

	private StreamWriter br;

	private StringBuilder bs = new StringBuilder();

	private string bt = "AutoSaveData";

	private bool bu;

	private string bv = "logfile.log";

	private string bw = "LogFile";

	private StreamWriter bx;

	private StringBuilder by = new StringBuilder();

	private bool bz;

	private DateTime b0 = DateTime.Now;

	private int b1;

	private bool b2;

	private bool b3;

	private string b4 = "";

	private global::g b5;

	private global::m b6 = new global::m();

	private System.Timers.Timer b7 = new System.Timers.Timer(10.0);

	private DateTime b8 = DateTime.Now;

	private DateTime b9 = DateTime.Now;

	private double ca = 0.05;

	private double cb = 1.0;

	private int cc = 1;

	private int cd;

	private object ce = new object();

	private bool cf;

	private int cg;

	private System.Timers.Timer ch = new System.Timers.Timer(500.0);

	private int ci;

	private double cj = 0.5;

	private double ck = Math.Pow(10.0, 14.0);

	private string[] cl = new string[10];

	private uint cm;

	private int cn;

	private string co = "";

	private ConcurrentQueue<string> cp = new ConcurrentQueue<string>();

	private string cq = "";

	private string cr = "";

	private int cs;

	private ToolTip ct = new ToolTip();

	private ToolTip cu = new ToolTip();

	private int cv = -1;

	private Bitmap cw;

	private Bitmap cx;

	private Bitmap cy;

	private Bitmap cz;

	private int c0 = 6;

	private int c1;

	private int c2;

	private int c3;

	private Color[] c4 = new Color[8]
	{
		Color.GreenYellow,
		Color.Red,
		Color.Magenta,
		Color.Aqua,
		Color.Gray,
		Color.Blue,
		Color.Brown,
		Color.Yellow
	};

	private double c5;

	private double c6 = 10.0;

	private DateTimeIntervalType c7;

	private bool c8;

	private string c9 = "RecordTime";

	private ChartValueType da = ChartValueType.DateTime;

	private double db = 1.0;

	private int dc = 2;

	private ImageList dd = new ImageList();

	private ImageList de = new ImageList();

	private int df = 1;

	private global::k dg;

	private global::j dh;

	private global::h di;

	private ListViewItem dj;

	private IContainer dk;

	private MenuStrip dl;

	private DataGridView dm;

	private Button dn;

	private ToolStrip @do;

	private PictureBox dp;

	private PictureBox dq;

	private PictureBox dr;

	private Label ds;

	private Label dt;

	private Label du;

	private Panel dv;

	private TextBox dw;

	private Button dx;

	private Button dy;

	private ComboBox dz;

	private e d0;

	private TabPage d1;

	private TabPage d2;

	private StatusStrip d3;

	private DataGridView d4;

	private DataGridView d5;

	private ContextMenuStrip d6;

	private ToolStripMenuItem d7;

	private ToolStripMenuItem d8;

	private Label d9;

	private NumericUpDown ea;

	private Button eb;

	private Button ec;

	private Button ed;

	private Chart ee;

	private ToolStripStatusLabel ef;

	private ToolStripStatusLabel eg;

	private ToolStripStatusLabel eh;

	private TabPage ei;

	private ToolStripMenuItem ej;

	private CheckBox ek;

	private CheckBox el;

	private TextBox em;

	private MaskedTextBox en;

	private GroupBox eo;

	private GroupBox ep;

	private TreeView eq;

	private ToolStripMenuItem er;

	private ToolStripMenuItem es;

	private ToolStripMenuItem et;

	private ToolStripMenuItem eu;

	private ToolStripMenuItem ev;

	private ToolStripMenuItem ew;

	private ToolStripMenuItem ex;

	private ToolStripMenuItem ey;

	private ToolStripMenuItem ez;

	private ToolStripMenuItem e0;

	private ToolStripMenuItem e1;

	private ToolStripMenuItem e2;

	private ToolStripMenuItem e3;

	private SaveFileDialog e4;

	private OpenFileDialog e5;

	private DataGridView e6;

	private DataGridViewTextBoxColumn e7;

	private DataGridViewTextBoxColumn e8;

	private Panel e9;

	private RichTextBox fa;

	private Button fb;

	private ToolStripMenuItem fc;

	private ToolStripMenuItem fd;

	private ToolStripMenuItem fe;

	private GroupBox ff;

	private GroupBox fg;

	private Label fh;

	private GroupBox fi;

	private TabPage fj;

	private TabPage fk;

	private GroupBox fl;

	private ListView fm;

	private ColumnHeader fn;

	private ColumnHeader fo;

	private ColumnHeader fp;

	private Button fq;

	private Button fr;

	private Button fs;

	private GroupBox ft;

	private Button fu;

	private Button fv;

	private ProgressBar fw;

	private Label fx;

	private RichTextBox fy;

	private Label fz;

	private DataGridView f0;

	private Button f1;

	private Button f2;

	private Button f3;

	private Button f4;

	private Label f5;

	private ToolStripMenuItem f6;

	private OpenFileDialog f7;

	private ToolStripMenuItem f8;

	private Button f9;

	private ToolStripSeparator ga;

	private ToolStripMenuItem gb;

	private BackgroundWorker gc;

	private ComboBox gd;

	private ComboBox ge;

	private NumericUpDown gf;

	private NumericUpDown gg;

	private CheckBox gh;

	private GroupBox gi;

	private CheckBox gj;

	private CheckBox gk;

	private TextBox gl;

	private TextBox gm;

	private TextBox gn;

	private CheckBox go;

	private Label gp;

	private Label gq;

	private GroupBox gr;

	private GroupBox gs;

	private ToolTip gt;

	private ContextMenuStrip gu;

	private ToolStripMenuItem gv;

	private ToolStripMenuItem gw;

	private ToolStripMenuItem gx;

	private ToolStripMenuItem gy;

	private ToolStripMenuItem gz;

	private ToolStripMenuItem g0;

	private ToolStripMenuItem g1;

	private ToolStripMenuItem g2;

	private Label g3;

	private NumericUpDown g4;

	private RichTextBox g5;

	private ContextMenuStrip g6;

	private ToolStripMenuItem g7;

	private ToolStripMenuItem g8;

	private ToolStripMenuItem g9;

	private ToolStripMenuItem ha;

	private ToolStripMenuItem hb;

	private ToolStripMenuItem hc;

	private ToolStripMenuItem hd;

	private ToolStripStatusLabel he;

	private ToolStripMenuItem hf;

	private Button hg;

	private Button hh;

	private Button hi;

	private TextBox hj;

	private ToolStripMenuItem hk;

	private Button hl;

	private ToolStripMenuItem hm;

	private ToolStripMenuItem hn;

	private global::b ho;

	private ListView hp;

	private ToolStripMenuItem hq;

	private ToolStripMenuItem hr;

	public f()
	{
		s();
	}

	[DllImport("user32.dll")]
	internal static extern bool SetProcessDPIAware();

	private void bp(object A_0, EventArgs A_1)
	{
		DoubleBuffered = true;
		try
		{
			foreach (XElement item in from result in XDocument.Load("ini.xml").Descendants("EasyHost")
				select (result))
			{
				b4 = Convert.ToString(item.Element("ConnCfg").Attribute("value").Value);
				this.m_b = Convert.ToString(item.Element("Language").Attribute("value").Value);
				df = Convert.ToInt32(item.Element("StepRate").Attribute("value").Value.ToString());
				cn = Convert.ToInt32(item.Element("DefaultAddr").Attribute("value").Value.ToString());
				ca = Convert.ToDouble(item.Element("SendInterval").Attribute("value").Value);
				cb = Convert.ToDouble(item.Element("RecordInterval").Attribute("value").Value);
				cj = Convert.ToDouble(item.Element("ScriptInterval").Attribute("value").Value);
				bz = Convert.ToBoolean(item.Element("LogFileFlag").Attribute("value").Value);
				this.m_bb = Convert.ToBoolean(item.Element("AutoLoadFlag").Attribute("value").Value);
				b2 = Convert.ToBoolean(item.Element("AutoCheckFlag").Attribute("value").Value);
				if (item.Element("FileTransferFlag") != null)
				{
					b3 = Convert.ToBoolean(item.Element("FileTransferFlag").Attribute("value").Value);
				}
				else
				{
					b3 = false;
				}
				this.m_a5 = Convert.ToString(item.Element("ProjectFileName").Attribute("value").Value);
			}
			if (bz)
			{
				ax();
			}
		}
		catch (Exception ex)
		{
			MessageBox.Show("Error: when read ini file: " + ex.Message, "Error");
		}
		this.m_bd = "Lang\\" + this.m_b + "\\";
		f(base.Name, this.m_b);
		ad();
		af();
		aa();
		ab();
		ae();
		dg = new global::k(this.m_bc + this.m_bd + this.m_bj);
		ah();
		ag();
		dh = new global::j(this.m_bc + this.m_bd + this.m_bj);
		aj();
		ai();
		di = new global::h(this.m_bc + this.m_bd + this.m_bi);
		ak();
		al();
		at();
		aq();
		an();
		am();
		ap();
		au();
		e9.Visible = false;
		fa.BackColor = Color.FromArgb(255, 199, 237, 204);
		dn.Font = new Font(this.m_c, 18f, FontStyle.Bold);
		cw = new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.red-led-off-md.png"));
		cx = new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.red-led-on-md.png"));
		cy = new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.green-led-on-md.png"));
		cz = new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.green-led-on-md.png"));
		dp.Image = cw;
		dq.Image = cw;
		dr.Image = cw;
		ho.a(b4);
		b7.Elapsed += a;
		b7.AutoReset = true;
		b7.Enabled = true;
		ea.Value = Convert.ToDecimal(cb);
		gd.Items.Add(g("record_comboBox_option0", "Do not Record"));
		gd.Items.Add(g("record_comboBox_option1", "Always Record"));
		gd.Items.Add(g("record_comboBox_option2", "Record Once"));
		gd.SelectedIndex = 1;
		ec.Enabled = false;
		g4.Value = Convert.ToDecimal(cj);
		ch.Elapsed += b;
		ch.Interval = cj * 1000.0;
		ch.AutoReset = true;
		ch.Enabled = false;
		dd.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.gearpage.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led000.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led001.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led010.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led011.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led100.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led101.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led110.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led111.png")));
		de.Images.Add(new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.led00r.png")));
		de.ImageSize = new Size(54, 18);
		this.m_ar = new DataView(this.m_j);
		this.m_as = new DataView(this.m_j);
		a(da);
		d0.SelectedTab = ei;
		if (this.m_a5 != "" && this.m_bb)
		{
			try
			{
				a(this.m_bf + this.m_a5, A_1: true, 0);
			}
			catch
			{
			}
		}
		else
		{
			a0();
			a1();
		}
		a5();
		base.WindowState = FormWindowState.Maximized;
		base.ImeMode = ImeMode.OnHalf;
		d0.Location = new Point(hg.Location.X + hp.Width, hg.Location.Y);
		d0.Width = base.Width - d0.Location.X;
		d0.Height = base.Height - d0.Location.Y;
		hp.Height = base.Height - hp.Location.Y;
		base.WindowState = FormWindowState.Minimized;
		base.WindowState = FormWindowState.Maximized;
		z();
		if (b3)
		{
			if (!d0.TabPages.Contains(fk))
			{
				d0.TabPages.Add(fk);
			}
		}
		else if (d0.TabPages.Contains(fk))
		{
			d0.TabPages.Remove(fk);
		}
	}

	private void a(object A_0, FormClosingEventArgs A_1)
	{
		try
		{
			XDocument xDocument = XDocument.Load(this.m_be);
			xDocument.Element("EasyHost").Element("ConnCfg").Attribute("value")
				.SetValue(b4);
			xDocument.Save(this.m_be);
		}
		catch (Exception)
		{
		}
		string value = "";
		if (!this.m_a6)
		{
			switch (MessageBox.Show(g("project_text_saveChanges", "Save project changes?"), g("text_confirm", "Confirm"), MessageBoxButtons.YesNoCancel, MessageBoxIcon.Question))
			{
			case DialogResult.Yes:
				A_1.Cancel = true;
				try
				{
					e4.Filter = "XML Document(*.xml)|*.xml";
					e4.InitialDirectory = this.m_bf;
					e4.FileName = this.m_a4;
					e4.ShowDialog();
				}
				catch
				{
				}
				break;
			default:
				A_1.Cancel = true;
				break;
			case DialogResult.No:
				break;
			}
		}
		else
		{
			value = this.m_a4;
		}
		if (A_1.Cancel)
		{
			return;
		}
		try
		{
			XDocument xDocument2 = XDocument.Load(this.m_be);
			xDocument2.Element("EasyHost").Element("ProjectFileName").Attribute("value")
				.SetValue(value);
			xDocument2.Save(this.m_be);
		}
		catch
		{
		}
		try
		{
			if (b5 != null)
			{
				b5.b();
			}
		}
		catch (Exception ex2)
		{
			p("Error when closing serial port, " + ex2.Message);
		}
		ho.d();
	}

	public void z()
	{
		ev.Text = g("project_menu", "Project");
		e0.Text = g("project_menu_open", "Open Existing Project File");
		e1.Text = g("project_menu_new", "Create New Project");
		e2.Text = g("project_menu_save", "Save Current Project");
		e3.Text = g("project_menu_close", "Close Current Project");
		ew.Text = g("device_menu", "Device");
		ey.Text = g("device_menu_new", "Add New Device");
		ez.Text = g("device_menu_remove", "Remove Selected Device");
		hf.Text = g("device_menu_export_cmdlist", "Export All Cmd List");
		hk.Text = g("device_menu_export_cmdscript", "Export Save Cmd to Script");
		hq.Text = g("device_menu_update_savedvalue", "Update Saved Value");
		hr.Text = g("device_menu_export_savedvalue", "Export Saved Value");
		ej.Text = g("option_menu", "Option");
		fc.Text = g("language_menu", "Language");
		er.Text = g("help_menu", "Help");
		ex.Text = g("help_menu_file", "Open Help File");
		et.Text = g("help_menu_webSite", "Visit WebSite");
		eu.Text = g("help_menu_onlineStore", "Visit Online Store");
		es.Text = g("help_menu_about", "About EasyHost");
		gs.Text = g("record_groupbox_setting", "Setting");
		ed.Text = g("record_btn_clear", "Clear Record");
		ec.Text = g("record_btn_deleteSelection", "Delete Selected Records");
		eb.Text = g("record_btn_save", "Export Record to File");
		d9.Text = g("record_label_interval", "Record Interval:");
		fi.Text = g("record_groupbox_help", "About parameters");
		g5.Text = g("record_label_help", "If there is no parameters in left table, please right click the target parameter row in TabPage 'Parameters'!");
		fg.Text = g("record_groupbox_memoryProtect", "Memory Protect");
		el.Text = g("record_checkbox_memoryProtect", "Memory Protect");
		fh.Text = g("record_label_memoryLimited", "Limit in quantity of Data: ");
		ff.Text = g("record_groupbox_autoSave", "Auto Save Record");
		ek.Text = g("record_checkbox_autoSave", "Auto Export Record to File");
		g7.Text = g("datalevel_autoUpdate", "Auto Update");
		g8.Text = g("datalevel_alwaysUpdate", "Always Update");
		g9.Text = g("datalevel_record", "Record Data");
		ha.Text = g("datalevel_curve", "Draw Curve");
		hd.Text = g("datastatistics", "Statistics");
		gh.Text = g("curve_checkBox_seperateCurve", "Only display Selected Device");
		gi.Text = g("curve_groupBox_axisXSelect", "Horizontal axis Parameter");
		fk.Text = g("filetransfer_tab", "File Transfer");
		fl.Text = g("filetransfer_groupbox_fileoperations", "File List & Operations");
		ft.Text = g("filetransfer_groupbox_log", "Transfer Log");
		fq.Text = g("filetransfer_btn_refresh", "Refresh List");
		fr.Text = g("filetransfer_btn_delete", "Delete File");
		fs.Text = g("filetransfer_btn_calculatecrc", "Calculate CRC");
		fu.Text = g("filetransfer_btn_upload", "Upload File");
		fv.Text = g("filetransfer_btn_download", "Download File");
		fx.Text = g("filetransfer_label_progress", "Transfer Progress:");
		gr.Text = g("curve_groupBox_axisXOption", "Horizontal axis(When numberical)");
		gq.Text = g("curve_label_minGridCount", "Min. Grid Count");
		gp.Text = g("curve_label_maxGridCount", "Max. Grid Count");
		gk.Text = g("curve_checkBox_fixedInterval", "Fix interval");
		gj.Text = g("curve_checkBox_fixedStart", "Fix Start Point");
		go.Text = g("curve_checkBox_fixedEnd", "Fix End Point");
		fb.Text = g("info_btn_closePanel", "Close Help Panel");
		d0.TabPages[0].Text = g("tabcontrol_data_pages_param", "Parameters");
		d0.TabPages[1].Text = g("tabcontrol_data_pages_record", "Data Record");
		d0.TabPages[2].Text = g("tabcontrol_data_pages_curve", "Data Curve");
		d0.TabPages[3].Text = g("tabcontrol_data_pages_script", "Script");
		d0.TabPages[4].Text = g("tabcontrol_data_pages_filetransfer", "File Transfer");
		fq.Text = g("filetransfer_btn_refresh", "Refresh List");
		fr.Text = g("filetransfer_btn_delete", "Delete File");
		fs.Text = g("filetransfer_btn_calculatecrc", "Calculate CRC");
		fu.Text = g("filetransfer_btn_upload", "Upload File");
		ds.Text = g("led_text_sw", "Output");
		dt.Text = g("led_text_err", "Error");
		du.Text = g("led_text_con", "Connection");
		gb.Text = g("contextmenu_autoUpdate", "Auto Update: Update Parameter when necessary");
		f8.Text = g("contextmenu_alwaysUpdate", "Always Update: Always update this Parameter");
		d8.Text = g("contextmenu_curve", "Draw Curve: Record Data and Draw Curve of this Parameter");
		d7.Text = g("contextmenu_record", "Record Data: Always Update and Record Data of this Parameter");
		hm.Text = g("contextmenu_mark", "Mark: Mark this Parameter");
		hn.Text = g("contextmenu_unmark", "Unmark: Unmark this Parameter");
		f6.Text = g("contextmenu_script", "Add this Parameter into Script List");
		hl.Text = g("btn_display_marked", "Dispaly Marked Parameters");
		dy.Text = g("text_ok", "OK");
		dx.Text = g("text_cancel", "Cancel");
		f5.Text = g("script_filename", "Script file name");
		f2.Text = g("script_btn_start", "Start");
		f1.Text = g("script_btn_stop", "Stop");
		f4.Text = g("script_btn_open", "Open script file");
		if (fn != null)
		{
			fn.Text = g("filetransfer_column_filename", "Filename");
		}
		if (fo != null)
		{
			fo.Text = g("filetransfer_column_size", "Size");
		}
		if (fp != null)
		{
			fp.Text = g("filetransfer_column_crc", "CRC");
		}
		f3.Text = g("script_btn_save", "Save this Script file");
		f9.Text = g("script_btn_new", "New Script");
		gw.Text = g("script_btn_rowUp", "Move Up");
		gx.Text = g("script_btn_rowDown", "Move Down");
		gv.Text = g("script_btn_rowRemove", "Remove");
		g1.Text = g("script_btn_addDelay", "Add 'Delay'");
		g0.Text = g("script_btn_addGo", "Add 'Go'");
		g2.Text = g("script_btn_addBlank", "Add Blank");
		gy.Text = g("script_btn_copy", "Copy");
		gz.Text = g("script_btn_paste", "Paste");
		hb.Text = g("script_btn_startfromhere", "Start from this Row");
		hc.Text = g("script_btn_continuefromhere", "Continue from this Row");
		g3.Text = g("script_label_interval", "Interval");
		ef.Text = g("status_text_default", "Please read the help file of software at first!");
		dn.Text = g("conn_btn_connect", "Connect");
		hg.Text = g("device_btn_add", "Add DEV");
		hh.Text = g("device_btn_remove", "Remove DEV");
		hi.Text = g("cmd_button", "Send CMD");
	}

	public void aa()
	{
		this.m_f.Columns.Add("Model", Type.GetType("System.String"));
		this.m_f.Columns.Add("ParamName", Type.GetType("System.String"));
		this.m_f.Columns.Add("ParamText", Type.GetType("System.String"));
		this.m_f.Columns.Add("ParamValue", Type.GetType("System.String"));
		this.m_f.Columns.Add("ValueText", Type.GetType("System.String"));
		this.m_f.Columns.Add("ParamType", Type.GetType("System.String"));
		this.m_f.Columns.Add("Unit", Type.GetType("System.String"));
		this.m_f.Columns.Add("Page", typeof(int));
		this.m_f.Columns.Add("PageName", Type.GetType("System.String"));
		this.m_f.Columns.Add("FolderName", Type.GetType("System.String"));
		this.m_f.Columns.Add("ParamLevel", typeof(int));
		this.m_f.Columns.Add("ToolTips", Type.GetType("System.String"));
		this.m_f.Columns.Add("IsButton", typeof(bool));
	}

	public void ab()
	{
		this.m_g.Columns.Add("Index", typeof(uint));
		this.m_g.Columns[0].AutoIncrement = true;
		this.m_g.Columns[0].AutoIncrementSeed = 1L;
		this.m_g.Columns[0].AutoIncrementStep = 1L;
		this.m_g.Columns.Add("Addr", typeof(int));
		this.m_g.Columns.Add("Model", Type.GetType("System.String"));
		this.m_g.Columns.Add("ParamName", Type.GetType("System.String"));
		this.m_g.Columns.Add("ParamText", Type.GetType("System.String"));
		this.m_g.Columns.Add("ParamValue", Type.GetType("System.String"));
		this.m_g.Columns.Add("ValueText", Type.GetType("System.String"));
		this.m_g.Columns.Add("ParamType", Type.GetType("System.String"));
		this.m_g.Columns.Add("Unit", Type.GetType("System.String"));
		this.m_g.Columns.Add("Page", typeof(int));
		this.m_g.Columns.Add("PageName", Type.GetType("System.String"));
		this.m_g.Columns.Add("FolderName", Type.GetType("System.String"));
		this.m_g.Columns.Add("ParamLevel", typeof(int));
		this.m_g.Columns.Add("DataLevel", typeof(int));
		this.m_g.Columns.Add("ToolTips", Type.GetType("System.String"));
		this.m_g.Columns.Add("IsButton", typeof(bool));
		this.m_g.Columns.Add("Mark", typeof(int));
		this.m_am = new DataView(this.m_g);
		this.m_an = new DataView(this.m_g);
		this.m_ap = new DataView(this.m_g);
		this.m_aq = new DataView(this.m_g);
		this.m_at = new DataView(this.m_g);
		this.m_at.RowFilter = "Page = 0";
		this.m_ao = new DataView(this.m_g);
		this.m_au = new DataView(this.m_g);
		this.m_au.RowFilter = "IsButton = 'true'";
		this.m_t.Columns.Add("ParamType", Type.GetType("System.String"));
		this.m_t.Columns.Add("ParamValue", typeof(int));
		this.m_t.Columns.Add("ValueText", Type.GetType("System.String"));
		this.m_t.Columns.Add("CmdText", Type.GetType("System.String"));
		this.m_t.Columns.Add("Addr", typeof(int));
	}

	public void ac()
	{
		for (int num = 0; num < this.m_g.Rows.Count; num++)
		{
			if (Convert.ToInt32(this.m_g.Rows[num]["ParamLevel"]) > 0)
			{
				this.m_g.Rows[num]["ParamValue"] = "";
				this.m_g.Rows[num]["ValueText"] = "";
			}
		}
	}

	public void ad()
	{
		this.m_s.Columns.Add("DataLevelText", Type.GetType("System.String"));
		this.m_s.Columns.Add("DataLevel", typeof(int));
		DataRow dataRow = this.m_s.NewRow();
		dataRow["DataLevelText"] = g("datalevel_autoUpdate", "Auto Update");
		dataRow["DataLevel"] = "0";
		this.m_s.Rows.Add(dataRow);
		DataRow dataRow2 = this.m_s.NewRow();
		dataRow2["DataLevelText"] = g("datalevel_alwaysUpdate", "Always Update");
		dataRow2["DataLevel"] = "1";
		this.m_s.Rows.Add(dataRow2);
		DataRow dataRow3 = this.m_s.NewRow();
		dataRow3["DataLevelText"] = g("datalevel_record", "Record Data");
		dataRow3["DataLevel"] = "2";
		this.m_s.Rows.Add(dataRow3);
		DataRow dataRow4 = this.m_s.NewRow();
		dataRow4["DataLevelText"] = g("datalevel_curve", "Draw Curve");
		dataRow4["DataLevel"] = "3";
		this.m_s.Rows.Add(dataRow4);
	}

	public void ae()
	{
		this.m_h.Columns.Add("Addr", typeof(int));
		this.m_h.Columns.Add("model", Type.GetType("System.String"));
		this.m_h.Columns.Add("constatus", typeof(int));
		this.m_h.Columns.Add("tabpagename", Type.GetType("System.String"));
		this.m_h.Columns.Add("lastcomtime", Type.GetType("System.DateTime"));
		this.m_h.Columns.Add("ledsw", typeof(int));
		this.m_h.Columns.Add("lederr", typeof(int));
	}

	public void af()
	{
		this.m_i.Columns.Add("ButtonName", Type.GetType("System.String"));
		this.m_i.Columns.Add("ButtonCmdString", Type.GetType("System.String"));
		this.m_i.Columns.Add("ButtonText", Type.GetType("System.String"));
		DataColumn[] primaryKey = new DataColumn[1] { this.m_i.Columns["ButtonName"] };
		this.m_i.PrimaryKey = primaryKey;
	}

	public void ag()
	{
		this.m_l.Columns.Add("Addr", typeof(int));
		this.m_l.Columns.Add("Count", typeof(int));
		this.m_l.Columns.Add("Index", typeof(int));
		this.m_l.Columns.Add("ErrorCode", typeof(int));
		this.m_l.Columns.Add("ErrorText", Type.GetType("System.String"));
		this.m_l.Columns.Add("ModuleText", Type.GetType("System.String"));
		this.m_av = new DataView(this.m_l);
		this.m_ay = new DataView(this.m_l);
		this.m_m.Columns.Add("ErrorCode", typeof(int));
		this.m_m.Columns.Add("ErrorText", Type.GetType("System.String"));
		this.m_m.ReadXml(this.m_bc + this.m_bd + this.m_bj + this.m_bn);
		this.m_n.Columns.Add("ReplyCode", typeof(int));
		this.m_n.Columns.Add("ReplyText", Type.GetType("System.String"));
		this.m_n.ReadXml(this.m_bc + this.m_bd + this.m_bj + this.m_bo);
	}

	public void ah()
	{
		this.m_k.Columns.Add("ModuleName", Type.GetType("System.String"));
		this.m_k.Columns.Add("ModuleText", Type.GetType("System.String"));
		DataColumn[] primaryKey = new DataColumn[1] { this.m_k.Columns["ModuleName"] };
		this.m_k.PrimaryKey = primaryKey;
		this.m_k.ReadXml(this.m_bc + this.m_bd + this.m_bj + this.m_bl);
	}

	public void ai()
	{
		this.m_p.Columns.Add("Addr", typeof(int));
		this.m_p.Columns.Add("ModuleName", Type.GetType("System.String"));
		this.m_p.Columns.Add("ParamName", Type.GetType("System.String"));
		this.m_p.Columns.Add("ParamText", Type.GetType("System.String"));
		this.m_p.Columns.Add("Cmd", Type.GetType("System.String"));
		this.m_p.Columns.Add("ValueText", Type.GetType("System.String"));
		this.m_p.Columns.Add("Var", typeof(double));
		this.m_p.Columns.Add("Count", typeof(double));
		this.m_p.Columns.Add("Reg", typeof(int));
		this.m_p.Columns.Add("Index", typeof(int));
		this.m_aw = new DataView(this.m_p);
		this.m_ax = new DataView(this.m_g);
		this.m_q = this.m_p.Clone();
		this.m_r.Columns.Add("Text", Type.GetType("System.String"));
		this.m_r.Columns.Add("Cmd", Type.GetType("System.String"));
		DataRow dataRow = this.m_r.NewRow();
		dataRow["Text"] = "=";
		dataRow["Cmd"] = "=";
		this.m_r.Rows.Add(dataRow);
		DataRow dataRow2 = this.m_r.NewRow();
		dataRow2["Text"] = "?";
		dataRow2["Cmd"] = "?";
		this.m_r.Rows.Add(dataRow2);
		DataRow dataRow3 = this.m_r.NewRow();
		dataRow3["Text"] = "^";
		dataRow3["Cmd"] = "^";
		this.m_r.Rows.Add(dataRow3);
		DataRow dataRow4 = this.m_r.NewRow();
		dataRow4["Text"] = "!";
		dataRow4["Cmd"] = "!";
		this.m_r.Rows.Add(dataRow4);
		DataRow dataRow5 = this.m_r.NewRow();
		dataRow5["Text"] = "+=";
		dataRow5["Cmd"] = "+=";
		this.m_r.Rows.Add(dataRow5);
		DataRow dataRow6 = this.m_r.NewRow();
		dataRow6["Text"] = ">>";
		dataRow6["Cmd"] = ">>";
		this.m_r.Rows.Add(dataRow6);
		DataRow dataRow7 = this.m_r.NewRow();
		dataRow7["Text"] = "??";
		dataRow7["Cmd"] = "??";
		this.m_r.Rows.Add(dataRow7);
		DataRow dataRow8 = this.m_r.NewRow();
		dataRow8["Text"] = "?=";
		dataRow8["Cmd"] = "?=";
		this.m_r.Rows.Add(dataRow8);
		DataRow dataRow9 = this.m_r.NewRow();
		dataRow9["Text"] = "?>";
		dataRow9["Cmd"] = "?>";
		this.m_r.Rows.Add(dataRow9);
		DataRow dataRow10 = this.m_r.NewRow();
		dataRow10["Text"] = "?<";
		dataRow10["Cmd"] = "?<";
		this.m_r.Rows.Add(dataRow10);
	}

	public void aj()
	{
		this.m_o.Columns.Add("HelpText", Type.GetType("System.String"));
		this.m_o.ReadXml(this.m_bc + this.m_bd + this.m_bj + this.m_bm);
	}

	public void ak()
	{
		dm.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.AllCells;
		dm.AutoGenerateColumns = false;
		dm.DataSource = null;
		int count = dm.Columns.Count;
		for (int num = 0; num < count; num++)
		{
			dm.Columns.RemoveAt(0);
		}
		dm.Columns.Add("ParamText", g("param_dgv_name", "Param"));
		dm.Columns[0].DataPropertyName = "ParamText";
		dm.Columns[0].ReadOnly = true;
		dm.Columns[0].SortMode = DataGridViewColumnSortMode.NotSortable;
		dm.Columns.Add("ValueText", g("param_dgv_value", "Value"));
		dm.Columns[1].DataPropertyName = "ValueText";
		dm.Columns[1].ReadOnly = true;
		dm.Columns[1].SortMode = DataGridViewColumnSortMode.NotSortable;
		dm.Columns.Add("Unit", g("param_dgv_unit", "Unit"));
		dm.Columns[2].DataPropertyName = "Unit";
		dm.Columns[2].ReadOnly = true;
		dm.Columns[2].SortMode = DataGridViewColumnSortMode.NotSortable;
		DataGridViewButtonColumn dataGridViewButtonColumn = new DataGridViewButtonColumn();
		dataGridViewButtonColumn.ReadOnly = true;
		dataGridViewButtonColumn.HeaderText = g("param_dgv_up", "Up");
		dataGridViewButtonColumn.Name = "up";
		dataGridViewButtonColumn.Text = g("param_dgv_up", "Up");
		dataGridViewButtonColumn.UseColumnTextForButtonValue = true;
		dm.Columns.Add(dataGridViewButtonColumn);
		dm.Columns[3].SortMode = DataGridViewColumnSortMode.NotSortable;
		DataGridViewButtonColumn dataGridViewButtonColumn2 = new DataGridViewButtonColumn();
		dataGridViewButtonColumn2.ReadOnly = true;
		dataGridViewButtonColumn2.HeaderText = g("param_dgv_down", "Down");
		dataGridViewButtonColumn2.Name = "down";
		dataGridViewButtonColumn2.Text = g("param_dgv_down", "Down");
		dataGridViewButtonColumn2.UseColumnTextForButtonValue = true;
		dm.Columns.Add(dataGridViewButtonColumn2);
		dm.Columns[4].SortMode = DataGridViewColumnSortMode.NotSortable;
		DataGridViewButtonColumn dataGridViewButtonColumn3 = new DataGridViewButtonColumn();
		dataGridViewButtonColumn3.ReadOnly = true;
		dataGridViewButtonColumn3.HeaderText = g("param_dgv_save", "Save");
		dataGridViewButtonColumn3.Name = "save";
		dataGridViewButtonColumn3.Text = g("param_dgv_save", "Save");
		dataGridViewButtonColumn3.UseColumnTextForButtonValue = true;
		dm.Columns.Add(dataGridViewButtonColumn3);
		dm.Columns[5].SortMode = DataGridViewColumnSortMode.NotSortable;
		DataGridViewButtonColumn dataGridViewButtonColumn4 = new DataGridViewButtonColumn();
		dataGridViewButtonColumn4.ReadOnly = true;
		dataGridViewButtonColumn4.HeaderText = g("param_dgv_info", "Help");
		dataGridViewButtonColumn4.Name = "help";
		dataGridViewButtonColumn4.Text = g("param_dgv_info", "Help");
		dataGridViewButtonColumn4.UseColumnTextForButtonValue = true;
		dm.Columns.Add(dataGridViewButtonColumn4);
		dm.Columns[6].SortMode = DataGridViewColumnSortMode.NotSortable;
		DataGridViewComboBoxColumn dataGridViewComboBoxColumn = new DataGridViewComboBoxColumn();
		dataGridViewComboBoxColumn.ReadOnly = false;
		dataGridViewComboBoxColumn.HeaderText = g("datalevel_text", "Data Processing");
		dataGridViewComboBoxColumn.Name = "DataLevel";
		dataGridViewComboBoxColumn.DataPropertyName = "DataLevel";
		dataGridViewComboBoxColumn.DataSource = this.m_s;
		dataGridViewComboBoxColumn.ValueMember = "DataLevel";
		dataGridViewComboBoxColumn.DisplayMember = "DataLevelText";
		dataGridViewComboBoxColumn.ReadOnly = true;
		dataGridViewComboBoxColumn.DisplayStyle = DataGridViewComboBoxDisplayStyle.Nothing;
		dm.Columns.Add(dataGridViewComboBoxColumn);
		dm.Columns[7].SortMode = DataGridViewColumnSortMode.NotSortable;
		dm.Columns.Add("foldername", g("param_dgv_module", "Module"));
		dm.Columns[8].DataPropertyName = "foldername";
		dm.Columns[8].ReadOnly = true;
		dm.Columns[8].SortMode = DataGridViewColumnSortMode.NotSortable;
		dm.Columns.Add("addr", g("param_dgv_addr", "Addr"));
		dm.Columns[9].DataPropertyName = "Addr";
		dm.Columns[9].ReadOnly = true;
		dm.Columns[9].SortMode = DataGridViewColumnSortMode.NotSortable;
		dm.AllowUserToAddRows = false;
		dm.AllowUserToResizeRows = false;
		ao();
		dm.AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.None;
		this.m_an.RowFilter = "page = 0";
		dm.DataSource = this.m_an;
		dm.DataError += b;
	}

	public void al()
	{
		e6.Columns.Add("Addr", g("errorreport_dgv_addr", "Addr"));
		e6.Columns[0].Width = 150;
		e6.Columns[0].DataPropertyName = "Addr";
		e6.Columns[0].ReadOnly = true;
		e6.Columns.Add("ModuleText", g("errorreport_dgv_module", "Module"));
		e6.Columns[1].DataPropertyName = "ModuleText";
		e6.Columns[1].ReadOnly = true;
		e6.Columns.Add("ErrorText", g("errorreport_dgv_error", "Error"));
		e6.Columns[2].DataPropertyName = "ErrorText";
		e6.Columns[2].ReadOnly = true;
		e6.AllowUserToAddRows = false;
		e6.AllowUserToResizeRows = false;
		e6.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.AllCells;
		e6.AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.AllCells;
		e6.AutoGenerateColumns = false;
		e6.DataSource = this.m_ay;
		e6.DataError += b;
		e6.Visible = false;
		e6.Width = 350;
		e6.Height = 456;
	}

	public void am()
	{
		ep.Text = g("record_dgvdatalevel_groupbox", "Selected Parameters");
		d5.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.AllCells;
		d5.AutoGenerateColumns = false;
		d5.DataSource = null;
		int count = d5.Columns.Count;
		for (int num = 0; num < count; num++)
		{
			d5.Columns.RemoveAt(0);
		}
		d5.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.DisableResizing;
		d5.ColumnHeadersHeight = 30;
		d5.ColumnHeadersDefaultCellStyle.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		d5.Columns.Add("Model", g("record_dgvdatalevel_model", "Model"));
		d5.Columns[0].DataPropertyName = "Model";
		d5.Columns[0].ReadOnly = true;
		d5.Columns[0].SortMode = DataGridViewColumnSortMode.NotSortable;
		d5.Columns.Add("Addr", g("record_dgvdatalevel_address", "Addr"));
		d5.Columns[1].DataPropertyName = "Addr";
		d5.Columns[1].ReadOnly = true;
		d5.Columns[1].SortMode = DataGridViewColumnSortMode.NotSortable;
		d5.Columns.Add("FolderName", g("record_dgvdatalevel_module", "Module"));
		d5.Columns[2].DataPropertyName = "FolderName";
		d5.Columns[2].ReadOnly = true;
		d5.Columns[2].SortMode = DataGridViewColumnSortMode.NotSortable;
		d5.Columns.Add("ParamText", g("record_dgvdatalevel_name", "Param"));
		d5.Columns[3].DataPropertyName = "ParamText";
		d5.Columns[3].ReadOnly = true;
		d5.Columns[3].SortMode = DataGridViewColumnSortMode.NotSortable;
		d5.Columns.Add("ValueText", g("record_dgvdatalevel_value", "Value"));
		d5.Columns[4].DataPropertyName = "ValueText";
		d5.Columns[4].ReadOnly = true;
		d5.Columns[4].SortMode = DataGridViewColumnSortMode.NotSortable;
		d5.Columns.Add("Unit", g("record_dgvdatalevel_unit", "Unit"));
		d5.Columns[5].DataPropertyName = "Unit";
		d5.Columns[5].ReadOnly = true;
		d5.Columns[5].SortMode = DataGridViewColumnSortMode.NotSortable;
		DataGridViewComboBoxColumn dataGridViewComboBoxColumn = new DataGridViewComboBoxColumn();
		dataGridViewComboBoxColumn.HeaderText = g("datalevel_text", "Data Processing");
		dataGridViewComboBoxColumn.Name = "DataLevel";
		dataGridViewComboBoxColumn.DataPropertyName = "DataLevel";
		dataGridViewComboBoxColumn.DataSource = this.m_s;
		dataGridViewComboBoxColumn.ValueMember = "DataLevel";
		dataGridViewComboBoxColumn.DisplayMember = "DataLevelText";
		dataGridViewComboBoxColumn.ReadOnly = true;
		dataGridViewComboBoxColumn.DisplayStyle = DataGridViewComboBoxDisplayStyle.Nothing;
		d5.Columns.Add(dataGridViewComboBoxColumn);
		d5.Columns[6].SortMode = DataGridViewColumnSortMode.NotSortable;
		d5.AllowUserToAddRows = false;
		d5.AllowUserToResizeRows = false;
		d5.DefaultCellStyle.BackColor = Color.White;
		d5.DefaultCellStyle.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		d5.AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.None;
		d5.RowTemplate.Height = 30;
		d5.AlternatingRowsDefaultCellStyle.BackColor = Color.White;
		this.m_ap.RowFilter = "datalevel > 1";
		this.m_ap.Sort = "addr ASC";
		this.m_aq.RowFilter = "datalevel > 1";
		this.m_aq.Sort = "addr ASC";
		this.m_aq.ListChanged += a;
		d5.DataSource = this.m_ap;
		d5.DataError += b;
	}

	public void an()
	{
		eo.Text = g("record_dgv_groupbox", "Data Record");
		this.m_j.Columns.Add("DataIndex", typeof(uint));
		this.m_j.Columns[0].Caption = g("record_dgv_index", "Index");
		this.m_j.Columns.Add("RecordTime", Type.GetType("System.DateTime"));
		this.m_j.Columns[1].Caption = g("record_dgv_recordtime", "Time");
		this.m_j.Clear();
		d4.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.DisableResizing;
		d4.ColumnHeadersHeight = 30;
		d4.ColumnHeadersDefaultCellStyle.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		d4.AllowUserToAddRows = false;
		d4.AllowUserToResizeRows = false;
		d4.DefaultCellStyle.BackColor = Color.LightYellow;
		d4.DefaultCellStyle.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		d4.AlternatingRowsDefaultCellStyle.BackColor = Color.LightCyan;
		d4.Columns[0].DataPropertyName = "DataIndex";
		d4.Columns[0].HeaderText = g("record_dgv_index", "Index");
		d4.Columns[0].SortMode = DataGridViewColumnSortMode.NotSortable;
		d4.Columns[0].AutoSizeMode = DataGridViewAutoSizeColumnMode.None;
		d4.Columns[0].ReadOnly = true;
		d4.Columns[1].DataPropertyName = "RecordTime";
		d4.Columns[1].HeaderText = g("record_dgv_recordtime", "Time");
		d4.Columns[1].ReadOnly = true;
		d4.Columns[1].SortMode = DataGridViewColumnSortMode.NotSortable;
		d4.Columns[1].AutoSizeMode = DataGridViewAutoSizeColumnMode.AllCells;
		d4.Columns[1].DefaultCellStyle.Format = "yyyy-MM-dd HH:mm:ss.fff";
		d4.VirtualMode = true;
		d4.DataError += b;
		d4.GetType().GetProperty("DoubleBuffered", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(d4, true, null);
	}

	public void ao()
	{
		dm.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.DisableResizing;
		dm.ColumnHeadersHeight = 30;
		dm.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		dm.RowTemplate.DefaultCellStyle.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		dm.DefaultCellStyle.ForeColor = Color.Black;
		dm.DefaultCellStyle.BackColor = Color.LightYellow;
		dm.GridColor = Color.LightGray;
		dm.RowTemplate.Height = 35;
		dm.AlternatingRowsDefaultCellStyle.BackColor = Color.LightCyan;
	}

	public void ap()
	{
		f0.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.AllCells;
		f0.AutoGenerateColumns = false;
		f0.AllowUserToAddRows = false;
		f0.AllowUserToResizeRows = false;
		f0.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.DisableResizing;
		f0.ColumnHeadersHeight = 30;
		f0.ColumnHeadersDefaultCellStyle.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		f0.Columns.Add("Addr", g("script_dgv_addr", "Addr"));
		f0.Columns[0].DataPropertyName = "Addr";
		f0.Columns[0].ReadOnly = false;
		f0.Columns[0].SortMode = DataGridViewColumnSortMode.NotSortable;
		f0.Columns.Add("ModuleName", g("script_dgv_moduleName", "Module"));
		f0.Columns[1].DataPropertyName = "ModuleName";
		f0.Columns[1].ReadOnly = false;
		f0.Columns[1].SortMode = DataGridViewColumnSortMode.NotSortable;
		f0.Columns.Add("ParamName", g("script_dgv_paramName", "Param"));
		f0.Columns[2].DataPropertyName = "ParamName";
		f0.Columns[2].ReadOnly = false;
		f0.Columns[2].SortMode = DataGridViewColumnSortMode.NotSortable;
		DataGridViewComboBoxColumn dataGridViewComboBoxColumn = new DataGridViewComboBoxColumn();
		dataGridViewComboBoxColumn.HeaderText = g("script_dgv_cmd", "Operate");
		dataGridViewComboBoxColumn.Name = "Cmd";
		dataGridViewComboBoxColumn.DataPropertyName = "Cmd";
		dataGridViewComboBoxColumn.DataSource = this.m_r;
		dataGridViewComboBoxColumn.ValueMember = "Cmd";
		dataGridViewComboBoxColumn.DisplayMember = "Text";
		f0.Columns.Add(dataGridViewComboBoxColumn);
		f0.Columns[3].SortMode = DataGridViewColumnSortMode.NotSortable;
		f0.Columns.Add("ParamValue", g("script_dgv_value", "Value"));
		f0.Columns[4].DataPropertyName = "ValueText";
		f0.Columns[4].ReadOnly = false;
		f0.Columns[4].SortMode = DataGridViewColumnSortMode.NotSortable;
		f0.Columns.Add("Var", g("script_dgv_var", "Variable"));
		f0.Columns[5].DataPropertyName = "Var";
		f0.Columns[5].Name = "Var";
		f0.Columns[5].SortMode = DataGridViewColumnSortMode.NotSortable;
		f0.Columns.Add("Count", g("script_dgv_count", "Count"));
		f0.Columns[6].DataPropertyName = "Count";
		f0.Columns[6].ReadOnly = true;
		f0.Columns[6].SortMode = DataGridViewColumnSortMode.NotSortable;
		f0.Columns.Add("Reg", g("script_dgv_reg", "REG index"));
		f0.Columns[7].DataPropertyName = "Reg";
		f0.Columns[7].ReadOnly = false;
		f0.Columns[7].SortMode = DataGridViewColumnSortMode.NotSortable;
		f0.Columns.Add("Note", g("script_dgv_note", "Description"));
		f0.Columns[8].DataPropertyName = "ParamText";
		f0.Columns[8].ReadOnly = false;
		f0.Columns[8].SortMode = DataGridViewColumnSortMode.NotSortable;
		f0.DefaultCellStyle.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		f0.DefaultCellStyle.ForeColor = Color.Black;
		f0.AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.None;
		f0.DataSource = this.m_aw;
		f0.DataError += a;
		f0.ImeMode = ImeMode.OnHalf;
	}

	private void b(object A_0, DataGridViewDataErrorEventArgs A_1)
	{
		A_1.ThrowException = false;
		a("Error: " + A_1.Exception.Message, Color.Red);
	}

	private void a(object A_0, DataGridViewDataErrorEventArgs A_1)
	{
		A_1.ThrowException = false;
		MessageBox.Show(A_1.Exception.Message);
	}

	public void aq()
	{
		DataView dataSource = dg.b("OPT_PIDTYPE");
		dz.DisplayMember = "ValueText";
		dz.ValueMember = "paramvalue";
		dz.DataSource = dataSource;
	}

	private void bo(object A_0, EventArgs A_1)
	{
		if (b5 == null || !b5.c())
		{
			dn.BackColor = Color.LightYellow;
			string text = ho.b();
			if (text == string.Empty)
			{
				MessageBox.Show("Wrong Config!", "ERROR");
				return;
			}
			b5 = global::d.a(text, b6);
			if (b5.c())
			{
				b(1);
				this.m_bp = false;
				b4 = ho.c();
				ho.a(b4, DateTime.Now);
			}
			else
			{
				b(0);
				MessageBox.Show("Connect Failed!", "ERROR");
			}
		}
		else
		{
			try
			{
				b5.b();
			}
			catch (Exception ex)
			{
				a(g("text_failed", "Failed") + ": " + ex.Message, Color.Red);
			}
			if (!b5.c())
			{
				b(0);
			}
			else
			{
				MessageBox.Show("Disconnect Failed!", "ERROR");
			}
		}
	}

	public void b(int A_0)
	{
		if (A_0 == 0)
		{
			dn.Text = g("conn_btn_connect", "Connect");
			dn.BackColor = Color.Empty;
			co = "";
			a3();
		}
		else
		{
			dn.Text = g("conn_btn_disconnect", "Disconnect");
			dn.BackColor = Color.GreenYellow;
		}
	}

	public void a(object A_0, ElapsedEventArgs A_1)
	{
		lock (ce)
		{
			y();
			if ((DateTime.Now - b9).TotalSeconds > cb)
			{
				@as();
				b9 = DateTime.Now;
			}
			TimeSpan timeSpan = DateTime.Now - b8;
			double num = ((this.m_u == global::f.a.a) ? ca : 0.2);
			if (timeSpan.TotalSeconds > num)
			{
				ar();
				b8 = DateTime.Now;
			}
		}
	}

	private void l(string A_0)
	{
		if (!A_0.StartsWith("OK(") && !A_0.StartsWith("ERR("))
		{
			return;
		}
		try
		{
			string text = (A_0.StartsWith("OK(") ? "OK(" : "ERR(");
			string[] array = A_0.Substring(text.Length, A_0.Length - text.Length - 1).Split(',');
			if (array.Length < 3)
			{
				return;
			}
			string text2 = array[1].Trim();
			string text3 = "";
			text3 = ((!(text2 == "LFMGR:FILELIST") || array.Length != 4) ? string.Join(",", array.Skip(2)).Trim() : string.Join(",", array[2].Substring(1, array[2].Length - 2), array[3]));
			if (text3.StartsWith("\""))
			{
				text3 = text3.Substring(1);
			}
			if (text3.EndsWith("\""))
			{
				text3 = text3.Substring(0, text3.Length - 1);
			}
			if (text == "ERR(")
			{
				b("执行失败: " + text2 + "->" + text3, Color.Red);
				u();
				return;
			}
			switch (text2)
			{
			case "LFMGR:FILELIST":
			{
				string[] array4 = text3.Split(',');
				string text6 = array4[0].Trim();
				string text7 = string.Empty;
				long result2 = 0L;
				if (array4.Length > 1 && long.TryParse(array4[1].Trim(), out result2))
				{
					text7 = ((result2 < 1024) ? (result2 + " B") : ((result2 < 1048576) ? (((double)result2 / 1024.0).ToString("F2") + " KB") : ((result2 >= 1073741824) ? (((double)result2 / 1073741824.0).ToString("F2") + " GB") : (((double)result2 / 1048576.0).ToString("F2") + " MB"))));
				}
				if (string.IsNullOrWhiteSpace(text6))
				{
					fm.BeginInvoke((MethodInvoker)delegate
					{
						this.m_u = global::f.a.a;
						fq.Enabled = true;
						fu.Enabled = true;
						fr.Enabled = true;
						fs.Enabled = true;
						fv.Enabled = fm.Items.Count > 0;
						if (this.m_y != null)
						{
							this.m_y();
							this.m_y = null;
						}
						v();
					});
				}
				else if (text6 == "." || text6 == "..")
				{
					if (this.m_u == global::f.a.b)
					{
						cp.Enqueue("get(LFMGR:FILELIST)");
					}
				}
				else
				{
					ListViewItem a2 = new ListViewItem(text6);
					a2.SubItems.Add(text7);
					a2.SubItems.Add("");
					a2.Tag = result2;
					fm.BeginInvoke((MethodInvoker)delegate
					{
						fm.Items.Add(a2);
					});
					if (!this.m_v)
					{
						b("收到文件名: " + text6 + ", 大小: " + text7, Color.Green);
					}
					if (this.m_u == global::f.a.b)
					{
						cp.Enqueue("get(LFMGR:FILELIST)");
					}
				}
				break;
			}
			case "LFMGR:FILEDELETE":
				v();
				break;
			case "LFMGR:FILECRC":
			{
				string a3 = text3.Trim();
				fm.BeginInvoke((MethodInvoker)delegate
				{
					this.m_u = global::f.a.a;
					fq.Enabled = true;
					fu.Enabled = true;
					fr.Enabled = true;
					fs.Enabled = true;
					string text8 = this.m_w.b();
					if (string.IsNullOrWhiteSpace(text8))
					{
						b($"更新CRC失败：未指定文件名", Color.Red);
					}
					else
					{
						foreach (ListViewItem item in fm.Items)
						{
							if (item.SubItems[0].Text == text8)
							{
								item.SubItems[2].Text = a3;
								break;
							}
						}
						if (!this.m_v)
						{
							b($"文件 '{text8}'的CRC: {a3}", Color.Green);
						}
						v();
					}
				});
				break;
			}
			case "LFMGR:FTPC":
				if (this.m_u != global::f.a.e && this.m_u != global::f.a.f)
				{
					break;
				}
				try
				{
					byte[] array2 = Utils.Base64ToByteArray(text3);
					if (array2.Length < 3)
					{
						b($"FTPC响应格式错误：字节数不足3，实际={array2.Length}", Color.Red);
						if (this.m_u == global::f.a.e)
						{
							u();
						}
						else if (this.m_u == global::f.a.f)
						{
							t();
						}
						break;
					}
					ushort num = (ushort)((array2[array2.Length - 2] << 8) | array2[array2.Length - 1]);
					ushort num2 = Utils.CalculateCRC16(array2, 0, array2.Length - 2);
					if (num != num2)
					{
						b($"FTPC响应CRC16验证失败：接收={num:X4}, 计算={num2:X4}", Color.Red);
						if (this.m_u == global::f.a.e)
						{
							u();
						}
						else if (this.m_u == global::f.a.f)
						{
							t();
						}
						break;
					}
					char c2 = (char)array2[0];
					switch (c2)
					{
					case 'T':
						if (array2.Length >= 6)
						{
							long a_3 = (array2[1] << 16) | (array2[2] << 8) | array2[3];
							int num3 = array2.Length - 6;
							byte[] array3 = new byte[num3];
							Array.Copy(array2, 4, array3, 0, num3);
							a(a_3, array3);
						}
						else if (array2.Length == 3)
						{
							if (!this.m_v)
							{
								b("下载数据发送完成，正在结束传输...", Color.Green);
							}
							a(this.m_ai, new byte[0]);
						}
						else
						{
							b($"无效的T响应格式，长度={array2.Length}", Color.Red);
							t();
						}
						break;
					case 'R':
						if (array2.Length >= 8)
						{
							int a_ = (array2[1] << 16) | (array2[2] << 8) | array2[3];
							int a_2 = (array2[4] << 8) | array2[5];
							a(a_, a_2);
						}
						else
						{
							b($"无效的R响应格式，长度={array2.Length}", Color.Red);
							u();
						}
						break;
					case '\0':
						if (this.m_u == global::f.a.e)
						{
							if (!this.m_v)
							{
								b("文件传输完成，正在验证CRC...", Color.Green);
							}
							if (this.m_ae != null)
							{
								this.m_ae.Close();
								this.m_ae = null;
							}
						}
						else if (this.m_u == global::f.a.f && !this.m_v)
						{
							b("下载最终完成，正在验证CRC...", Color.Green);
						}
						if (this.m_u == global::f.a.e)
						{
							u();
						}
						else if (this.m_u == global::f.a.f)
						{
							t();
						}
						v();
						break;
					case 'V':
					{
						string text4 = Encoding.ASCII.GetString(array2);
						if (!text4.StartsWith("V "))
						{
							break;
						}
						string text5 = text4.Substring(2).Trim();
						uint result = 0u;
						if (uint.TryParse(text5, NumberStyles.HexNumber, null, out result))
						{
							if (this.m_u == global::f.a.e)
							{
								if (result == this.m_al)
								{
									b("CRC验证成功，文件上传完成！", Color.Green);
								}
								else
								{
									b($"CRC验证失败！本地: {this.m_al:X8}, 远程: {text5}", Color.Red);
								}
							}
							else if (this.m_u == global::f.a.f)
							{
								if (result == this.m_al)
								{
									b("CRC验证成功，文件下载完成！", Color.Green);
								}
								else
								{
									b($"CRC验证失败！本地: {this.m_al:X8}, 远程: {text5}", Color.Red);
								}
							}
						}
						else
						{
							b($"CRC格式错误: {text5}", Color.Red);
						}
						break;
					}
					default:
						b($"未知的FTPC响应类型: {c2}", Color.Red);
						if (this.m_u == global::f.a.e)
						{
							u();
						}
						else if (this.m_u == global::f.a.f)
						{
							t();
						}
						break;
					}
					break;
				}
				catch (Exception ex)
				{
					b($"FTPC响应处理错误: {ex.Message}", Color.Red);
					if (this.m_u == global::f.a.e)
					{
						u();
					}
					else if (this.m_u == global::f.a.f)
					{
						t();
					}
					break;
				}
			default:
				b("不支持的命令:" + text2, Color.Red);
				break;
			}
		}
		catch (Exception ex2)
		{
			b($"解析响应失败: {A_0}, 错误: {ex2.Message}", Color.Red);
			BeginInvoke((MethodInvoker)delegate
			{
				this.m_u = global::f.a.a;
				fq.Enabled = true;
				fu.Enabled = true;
				fr.Enabled = true;
				fs.Enabled = true;
			});
		}
	}

	private string a(string A_0, int A_1)
	{
		string[] array = A_0.Split(new char[2] { '(', ')' }, StringSplitOptions.RemoveEmptyEntries);
		if (array.Count() > 1)
		{
			int a_ = 65535;
			int result2;
			if (array[1][0] == '{' && array[1][array[1].Length - 1] == '}')
			{
				string text = array[1].Substring(1, array[1].Length - 2);
				DataRow[] array2 = this.m_g.Select("Addr = " + A_1 + " AND ParamName = '" + text + "'");
				int num = 0;
				if (num < array2.Length && int.TryParse(array2[num]["Paramvalue"].ToString(), out var result))
				{
					a_ = result;
				}
			}
			else if (int.TryParse(array[1], out result2))
			{
				a_ = result2;
			}
			string text2 = dg.b(array[0], a_, A_1);
			A_0 = ((!(text2 == a_.ToString())) ? text2 : "HSTR");
		}
		int num2 = A_0.IndexOf('{');
		if (num2 >= 0)
		{
			int num3 = A_0.IndexOf('}', num2);
			if (num3 >= 0 && num2 < num3)
			{
				string text3 = A_0.Substring(num2 + 1, num3 - num2 - 1);
				DataRow[] array2 = this.m_g.Select("Addr = " + A_1 + " AND ParamName = '" + text3 + "'");
				int num = 0;
				if (num < array2.Length)
				{
					DataRow dataRow = array2[num];
					A_0 = A_0.Remove(num2, num3 - num2 + 1).Insert(num2, dataRow["Paramvalue"].ToString());
				}
			}
		}
		return A_0;
	}

	private void y()
	{
		string text = b6.c();
		if (text == null)
		{
			return;
		}
		if (text.StartsWith("OK(") || text.StartsWith("ERR("))
		{
			l(text);
			return;
		}
		string d2 = " ";
		string b2 = " ";
		string text2 = "";
		string text3 = "";
		int c2 = 0;
		try
		{
			if (!string.Equals(text.Substring(0, 10), "MENU:INFO="))
			{
				c(text, Color.Black);
			}
			string[] array = text.Split('=');
			d2 = array[0];
			string[] array2 = array[1].Split('@');
			b2 = array2[0];
			if (array2.Count() >= 2)
			{
				string[] array3 = array2[1].Split('#');
				text2 = array3[0];
				c2 = Convert.ToInt32(text2);
				text3 = ((array3.Count() < 2) ? "" : array3[1]);
			}
			else
			{
				c2 = cn;
			}
			string a_ = text.Remove(text.Length - text3.Length, text3.Length);
			if (text3 != "" && Convert.ToByte(text3, 16) != o(a_))
			{
				a("Error: When Check Sum, " + text + " != " + o(a_), Color.Red);
				return;
			}
			a(DateTime.Now, c2);
			if (d2 == "MENU:INFO")
			{
				b(b2, c2);
				return;
			}
			if (d2 == "CMD:REPLY")
			{
				try
				{
					int num = Convert.ToInt32(b2);
					string a_2 = d(num, c2);
					if (num != 1 && num != 8)
					{
						a(a_2, Color.Red);
					}
					else
					{
						a(a_2, Color.Black);
					}
					return;
				}
				catch
				{
					a("Error: when analyzing reply code!", Color.Red);
					return;
				}
			}
			if (d2 == "SYSTEM:SYSMODEL")
			{
				if (this.m_g.Rows.Count > 0)
				{
					d(b2, c2);
				}
				else if (c2 == 0)
				{
					dm.Invoke((MethodInvoker)delegate
					{
						int num2 = di.e(b2);
						if (num2 == 0)
						{
							a("Error: Not find the right model file！", Color.Red);
						}
						else if (num2 == 1)
						{
							a(this.m_bc + this.m_bd + this.m_bi + di.f(b2) + ".xml", A_1: false, c2);
						}
						else if (num2 > 1)
						{
							di.c(b2);
							if (di.d(b2) > 5)
							{
								a(this.m_bc + this.m_bd + this.m_bi + di.f(b2) + ".xml", A_1: false, c2);
							}
							else
							{
								b6.d();
								cp.Enqueue("SYSTEM:SYSVERSION?@0");
							}
						}
					});
				}
			}
			else if (d2 == "SYSTEM:SYSVERSION" && this.m_g.Rows.Count == 0 && c2 == 0)
			{
				dm.BeginInvoke((MethodInvoker)delegate
				{
					a(this.m_bc + this.m_bd + this.m_bi + di.g(b2) + ".xml", A_1: false, c2);
				});
			}
			if (cf)
			{
				return;
			}
			if (ch.Enabled && ci < this.m_p.Rows.Count && this.m_p.Rows[ci]["ModuleName"].ToString() + ":" + this.m_p.Rows[ci]["ParamName"].ToString() == d2 && Convert.ToInt32(this.m_p.Rows[ci]["Addr"]) == c2 && (this.m_p.Rows[ci]["Cmd"].ToString() == "?" || this.m_p.Rows[ci]["Cmd"].ToString() == "+=" || this.m_p.Rows[ci]["Cmd"].ToString() == "?=" || this.m_p.Rows[ci]["Cmd"].ToString() == "?>" || this.m_p.Rows[ci]["Cmd"].ToString() == "?<" || this.m_p.Rows[ci]["Cmd"].ToString() == "??"))
			{
				ck = Convert.ToDouble(b2);
			}
			dm.BeginInvoke((MethodInvoker)delegate
			{
				try
				{
					DataRow[] array4 = this.m_g.Select("Addr = " + c2 + " AND ParamName = '" + d2 + "'");
					foreach (DataRow dataRow in array4)
					{
						string text4 = a(dataRow["ParamType"].ToString(), c2);
						if (text4 == "HSTR" || text4.EndsWith("[HSTR]"))
						{
							if (text4 == "HSTR")
							{
								text4 = "NUM";
							}
							else if (text4.EndsWith("[HSTR]"))
							{
								text4 = text4.Substring(0, text4.Length - 6);
							}
							string[] array5 = b2.Split('[');
							b2 = array5[0];
							if (array5.Count() > 1)
							{
								string[] array6 = array5[1].Split(']')[0].Split(',');
								if (array6.Count() >= 3 && array6[2].Count() > 0)
								{
									text4 = array6[2];
								}
								if (array6.Count() >= 2 && array6[1].Count() > 0)
								{
									dataRow["ParamText"] = array6[1];
								}
								if (array6.Count() >= 1 && array6[0].Count() > 0)
								{
									dataRow["Unit"] = array6[0];
								}
							}
						}
						if (text4 == "NUM")
						{
							if (b2.Length > 0)
							{
								double num3 = Convert.ToDouble(b2);
								dataRow["Paramvalue"] = num3;
								dataRow["ValueText"] = num3;
							}
							else
							{
								dataRow["Paramvalue"] = 0;
								dataRow["ValueText"] = 0;
							}
						}
						else if (text4.Substring(0, 3) == "STR")
						{
							dataRow["ParamValue"] = b2;
							dataRow["ValueText"] = b2;
						}
						else if (b2.Length > 0)
						{
							dataRow["ParamValue"] = b2;
							dataRow["ValueText"] = e(b2, text4);
						}
						else
						{
							dataRow["ParamValue"] = b2;
							dataRow["ValueText"] = b2;
						}
					}
				}
				catch (Exception ex2)
				{
					a("Error: when analyzing received parameter data！" + ex2.Message, Color.Red);
				}
			});
		}
		catch (Exception ex)
		{
			a("Error: Wrong Syntax, " + ex.Message, Color.Red);
		}
	}

	public void b(string A_0, int A_1)
	{
		e6.BeginInvoke((MethodInvoker)delegate
		{
			try
			{
				int num = 0;
				int num2 = 0;
				string value = A_0.Substring(0, 3);
				string value2 = A_0.Substring(6, 3);
				num = Convert.ToInt32(value);
				num2 = Convert.ToInt32(value2);
				a(num, num2, A_1);
				string value3 = A_0.Substring(12, 3);
				string value4 = A_0.Substring(15, 3);
				string value5 = A_0.Substring(18, 3);
				string a_ = A_0.Substring(21, A_0.Length - 21);
				int num3 = Convert.ToInt32(value3);
				int num4 = Convert.ToInt32(value4);
				int num5 = Convert.ToInt32(value5);
				string value6 = c(num5);
				string value7 = m(a_);
				this.m_av.RowFilter = "Addr = " + A_1;
				this.m_av.Sort = "Index ASC";
				if (num3 > this.m_av.Count)
				{
					DataRow dataRow = this.m_l.NewRow();
					dataRow["Addr"] = A_1;
					dataRow["Count"] = num3;
					dataRow["Index"] = num4;
					dataRow["ErrorCode"] = num5;
					dataRow["ErrorText"] = value6;
					dataRow["ModuleText"] = value7;
					this.m_l.Rows.Add(dataRow);
				}
				else if (num3 < this.m_av.Count)
				{
					while (num3 < this.m_av.Count)
					{
						this.m_av.Delete(this.m_av.Count - 1);
						this.m_l.AcceptChanges();
					}
				}
				if (num3 > 0 && num3 == this.m_av.Count)
				{
					this.m_av[num4 - 1]["Addr"] = A_1;
					this.m_av[num4 - 1]["Count"] = num3;
					this.m_av[num4 - 1]["Index"] = num4;
					this.m_av[num4 - 1]["ErrorCode"] = num5;
					this.m_av[num4 - 1]["ErrorText"] = value6;
					this.m_av[num4 - 1]["ModuleText"] = value7;
				}
			}
			catch (Exception ex)
			{
				p("Error when analy menu:info, " + ex.Message);
			}
		});
	}

	public void c(string A_0, int A_1)
	{
		e6.Invoke((MethodInvoker)delegate
		{
			try
			{
				string value = A_0.Substring(0, 3);
				string value2 = A_0.Substring(3, 3);
				string value3 = A_0.Substring(6, 3);
				string a_ = A_0.Substring(9, A_0.Length - 9);
				int num = Convert.ToInt32(value);
				int num2 = Convert.ToInt32(value2);
				int num3 = Convert.ToInt32(value3);
				this.m_av.RowFilter = "Addr = " + A_1;
				this.m_av.Sort = "Index ASC";
				if (num > this.m_av.Count)
				{
					DataRow dataRow = this.m_l.NewRow();
					dataRow["Addr"] = A_1;
					dataRow["Count"] = num;
					dataRow["Index"] = num2;
					dataRow["ErrorCode"] = num3;
					dataRow["ErrorText"] = c(num3);
					dataRow["ModuleText"] = m(a_);
					this.m_l.Rows.Add(dataRow);
				}
				else if (num < this.m_av.Count)
				{
					this.m_av.Delete(this.m_av.Count - 1);
					this.m_l.AcceptChanges();
				}
				if (num > 0 && num == this.m_av.Count)
				{
					this.m_av[num2 - 1]["Addr"] = A_1;
					this.m_av[num2 - 1]["Count"] = num;
					this.m_av[num2 - 1]["Index"] = num2;
					this.m_av[num2 - 1]["ErrorCode"] = num3;
					this.m_av[num2 - 1]["ErrorText"] = c(num3);
					this.m_av[num2 - 1]["ModuleText"] = m(a_);
				}
			}
			catch (Exception ex)
			{
				p("Error when analy error report, " + ex.Message + A_0);
			}
		});
	}

	public string c(int A_0)
	{
		string result = A_0.ToString();
		DataRow[] array = this.m_m.Select("ErrorCode = " + A_0);
		int num = 0;
		if (num < array.Length)
		{
			result = array[num]["ErrorText"].ToString();
		}
		return result;
	}

	public string d(int A_0, int A_1)
	{
		string result = "Wrong Reply Code!";
		DataRow[] array = this.m_n.Select("ReplyCode = " + A_0);
		int num = 0;
		if (num < array.Length)
		{
			DataRow dataRow = array[num];
			result = g("device_menu", "Device") + "@" + A_1 + ": " + dataRow["ReplyText"].ToString();
		}
		return result;
	}

	public string d(int A_0)
	{
		string text = "";
		byte num = (byte)A_0;
		if ((num & 1) == 0)
		{
			text = text + c(0) + "; ";
		}
		if (((num >> 1) & 1) == 0)
		{
			text = text + c(1) + "; ";
		}
		if (((num >> 2) & 1) == 0)
		{
			text = text + c(2) + "; ";
		}
		if (((num >> 3) & 1) == 0)
		{
			text = text + c(3) + "; ";
		}
		if (((num >> 4) & 1) == 0)
		{
			text = text + c(4) + "; ";
		}
		if (((num >> 5) & 1) == 0)
		{
			text = text + c(5) + "; ";
		}
		if (((num >> 6) & 1) == 0)
		{
			text = text + c(6) + "; ";
		}
		if (((num >> 7) & 1) == 0)
		{
			text = text + c(7) + "; ";
		}
		if (text == "")
		{
			text = "";
		}
		return text;
	}

	public string e(int A_0)
	{
		return Convert.ToString(A_0, 2).PadLeft(8, '0');
	}

	public string e(string A_0, string A_1)
	{
		string result = A_0;
		try
		{
			int num = Convert.ToInt32(A_0);
			result = ((A_1 == "ERRORCODE") ? d(num) : ((!(A_1 == "BINARYCODE")) ? dg.a(A_1, num, 255) : e(num)));
		}
		catch
		{
			a("Error: when analzing parameter value text！", Color.Red);
		}
		return result;
	}

	public string m(string A_0)
	{
		string result = A_0;
		DataRow[] array = this.m_k.Select("ModuleName = '" + A_0 + "'");
		int num = 0;
		if (num < array.Length)
		{
			result = array[num]["ModuleText"].ToString();
		}
		return result;
	}

	public int n(string A_0)
	{
		int result = 255;
		try
		{
			result = Convert.ToInt32(A_0.Split('@')[1]);
		}
		catch
		{
			a("Error: Manual command miss address!", Color.Red);
		}
		return result;
	}

	public byte o(string A_0)
	{
		int num = 0;
		byte b2 = 0;
		byte[] bytes = Encoding.UTF8.GetBytes(A_0);
		for (num = 0; num < bytes.Length; num++)
		{
			b2 ^= bytes[num];
		}
		return b2;
	}

	public void ar()
	{
		try
		{
			if (b5 != null && b5.c())
			{
				if (cp.Count == 0)
				{
					if (this.m_g.Rows.Count > 0)
					{
						if (cd >= this.m_at.Count)
						{
							cd = -1;
							cd++;
						}
						else
						{
							try
							{
								int a_ = Convert.ToInt32(this.m_at[cd]["Addr"]);
								if (g(a_) == 3)
								{
									if (Convert.ToInt32(this.m_at[cd].Row["ParamLevel"]) > 0)
									{
										cp.Enqueue(Convert.ToString(this.m_at[cd].Row["ParamName"]) + "?@" + a_);
									}
									cd++;
								}
								else
								{
									cp.Enqueue("SYSTEM:SYSMODEL?@" + a_);
									cd++;
								}
							}
							catch (Exception ex)
							{
								a("Error: when adding query cmd, " + ex.Message, Color.Red);
							}
						}
					}
					else if (this.m_g.Rows.Count == 0)
					{
						cp.Enqueue("SYSTEM:SYSMODEL?@0");
					}
				}
				if (b5.d() && cp.Count > 0 && cp.TryDequeue(out var result))
				{
					try
					{
						if (b2)
						{
							result = result + "#" + o(result + "#").ToString("X2");
						}
						b5.e(result);
					}
					catch (Exception ex2)
					{
						a("Error: when sending cmd, " + ex2.Message, Color.Red);
					}
				}
			}
			BeginInvoke((MethodInvoker)delegate
			{
				av();
				aw();
				a4();
				try
				{
					eg.Text = co + q(a8());
				}
				catch
				{
				}
				az();
			});
		}
		catch (Exception ex3)
		{
			a("Error: when sending data, " + ex3.Message, Color.Red);
		}
	}

	public void @as()
	{
		try
		{
			Invoke((MethodInvoker)delegate
			{
				if (b5 != null && b5.c() && this.m_aq.Count > 0 && gd.SelectedIndex > 0)
				{
					int num = 0;
					DataRow dataRow = this.m_j.NewRow();
					dataRow[1] = DateTime.Now;
					try
					{
						for (num = 2; num < Math.Min(this.m_aq.Count + 2, this.m_j.Columns.Count); num++)
						{
							dataRow[num] = this.m_aq[num - 2].Row["ParamValue"];
						}
					}
					catch
					{
						a("Error: when recording data！", Color.Red);
					}
					try
					{
						this.m_j.Rows.Add(dataRow);
						if (this.m_j.Rows.Count > 1)
						{
							this.m_j.Rows[this.m_j.Rows.Count - 1][0] = Convert.ToInt32(this.m_j.Rows[this.m_j.Rows.Count - 2][0]) + 1;
						}
						else
						{
							this.m_j.Rows[this.m_j.Rows.Count - 1][0] = 0;
						}
					}
					catch (Exception)
					{
					}
					if (br != null && bu)
					{
						try
						{
							for (int num2 = 0; num2 < this.m_j.Columns.Count; num2++)
							{
								bs.Append(dataRow[num2].ToString() + "\t");
							}
							br.WriteLine(bs.ToString());
							bs.Clear();
						}
						catch (Exception ex3)
						{
							a("Error: when autosave or autoremove！" + ex3.Message, Color.Red);
						}
					}
					if (el.Checked && (double)this.m_j.Rows.Count > Convert.ToDouble(en.Text))
					{
						d4.RowCount = 0;
						for (int num3 = 0; num3 < ee.Series.Count; num3++)
						{
							ee.Series[num3].Points.Clear();
						}
						try
						{
							int val = this.m_j.Rows.Count - 1;
							for (int num4 = 0; num4 < Math.Min(1000, val); num4++)
							{
								this.m_j.Rows[0].Delete();
							}
							this.m_j.AcceptChanges();
						}
						catch (Exception ex4)
						{
							a("Error: when autosave or autoremove！" + ex4.Message, Color.Red);
						}
					}
					eh.Text = g("status_text_datacount", "Data Count") + ": " + Convert.ToString(this.m_j.Rows.Count);
					if (gd.SelectedIndex == 2)
					{
						gd.SelectedIndex = 0;
					}
				}
				d4.RowCount = this.m_j.Rows.Count;
				if (this.m_a2 && d0.SelectedTab == d1)
				{
					if (!c8 && this.m_ar.Count > 0)
					{
						a7();
						int num5 = 0;
						ee.ChartAreas[0].AxisY.ScaleView.SmallScrollMinSize = ee.ChartAreas[0].AxisY.ScaleView.Size / 10.0;
						ee.ChartAreas[0].AxisX.ScaleView.SmallScrollMinSize = ee.ChartAreas[0].AxisX.ScaleView.Size / 10.0;
						for (int num6 = 0; num6 < this.m_j.Columns.Count - 2; num6++)
						{
							try
							{
								if (Convert.ToInt32(this.m_aq[num6].Row["DataLevel"]) == 3 && d4.Columns[num6 + 2].DataPropertyName != c9 && (Convert.ToInt32(this.m_aq[num6].Row["Addr"]) == a2() || !gh.Checked))
								{
									ee.Series[num5].Points.DataBind(this.m_ar, c9, d4.Columns[num6 + 2].DataPropertyName, "");
									num5++;
								}
							}
							catch (Exception)
							{
							}
						}
					}
					else
					{
						a(g("status_text_help", "Tips") + ": " + g("curve_text_help", "If No Curve, please right click the parameter row in Parameters Tab Page at first"), Color.Red);
					}
				}
			});
		}
		catch (Exception ex)
		{
			a("Error: when record data, " + ex.Message, Color.Red);
		}
	}

	public void b(object A_0, ElapsedEventArgs A_1)
	{
		if (b5 == null || !b5.c() || Interlocked.CompareExchange(ref cg, 1, 0) != 0)
		{
			return;
		}
		f0.Invoke((MethodInvoker)delegate
		{
			if (ci < this.m_p.Rows.Count)
			{
				if (f0.SelectedRows.Count != 0)
				{
					int index = f0.SelectedRows[0].Index;
					f0.Rows[index].Selected = false;
				}
				if (ci <= this.m_p.Rows.Count)
				{
					f0.Rows[ci].Selected = true;
					f0.CurrentCell = f0.Rows[ci].Cells[1];
				}
				if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "=")
				{
					double num = 0.0;
					string text = "";
					try
					{
						num = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
						text = this.m_p.Rows[ci]["ValueText"].ToString();
					}
					catch
					{
						text = Convert.ToString(this.m_p.Rows[ci]["ValueText"]);
					}
					if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTDELAY")
					{
						if (Convert.ToDouble(this.m_p.Rows[ci]["Count"]) < num)
						{
							this.m_p.Rows[ci]["Count"] = Convert.ToDouble(this.m_p.Rows[ci]["Count"]) + cj;
						}
						else
						{
							this.m_p.Rows[ci]["Count"] = 0;
							ci++;
						}
					}
					else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTGO")
					{
						if (Convert.ToInt32(this.m_p.Rows[ci]["Count"]) < Convert.ToInt32(this.m_p.Rows[ci]["Var"]))
						{
							this.m_p.Rows[ci]["Count"] = Convert.ToInt32(this.m_p.Rows[ci]["Count"]) + 1;
							ci = Convert.ToInt32(num);
						}
						else
						{
							this.m_p.Rows[ci]["Count"] = 0;
							ci++;
						}
					}
					else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTCLEARDATA")
					{
						if (Convert.ToInt32(num) == 1)
						{
							d4.RowCount = 0;
							this.m_j.Clear();
						}
						ci++;
					}
					else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTRECORDDATA")
					{
						int num2 = Convert.ToInt32(num);
						if (num2 <= 2 && num2 >= 0)
						{
							gd.SelectedIndex = num2;
						}
						ci++;
					}
					else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTCONFIRM")
					{
						ch.Enabled = false;
						MessageBox.Show(text, "Confirm");
						_ = 1;
						ch.Enabled = true;
						ci++;
					}
					else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTREG")
					{
						int num3 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
						if (num3 >= 0)
						{
							cl[num3] = text;
						}
						ci++;
					}
					else
					{
						int num4 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
						if (num4 >= 0)
						{
							cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "=" + Convert.ToString(cl[num4]) + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
						}
						else
						{
							cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "=" + text + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
						}
						ci++;
					}
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "?")
				{
					int num5 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					double num6 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
					if (num5 >= 0)
					{
						try
						{
							num6 = Convert.ToDouble(cl[num5]);
						}
						catch
						{
						}
					}
					double num7 = ck - num6;
					this.m_p.Rows[ci]["Count"] = Convert.ToDouble(num7.ToString("G5"));
					if (Math.Abs(Convert.ToDouble(this.m_p.Rows[ci]["Count"])) < Convert.ToDouble(this.m_p.Rows[ci]["Var"]))
					{
						f0.Rows[ci].DefaultCellStyle.SelectionForeColor = Color.Black;
						ck = Math.Pow(10.0, 14.0);
						ci++;
					}
					else
					{
						f0.Rows[ci].DefaultCellStyle.SelectionForeColor = Color.Red;
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "^")
				{
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "^" + Convert.ToString(this.m_p.Rows[ci]["ValueText"]) + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					ci++;
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "!")
				{
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "!@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					ci++;
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "+=")
				{
					if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTREG")
					{
						int num8 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
						if (num8 >= 0)
						{
							double num9 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
							cl[num8] += num9;
						}
						ci++;
					}
					else if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
					{
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
					else
					{
						int num10 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
						double num11 = ck + Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
						if (num10 >= 0)
						{
							try
							{
								num11 = ck + Convert.ToDouble(cl[num10]);
							}
							catch
							{
							}
						}
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "=" + num11 + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
						ck = Math.Pow(10.0, 14.0);
						ci++;
					}
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == ">>")
				{
					ch.Enabled = false;
					string text2 = a("Input", this.m_p.Rows[ci]["ValueText"].ToString(), Convert.ToString(this.m_p.Rows[ci]["Count"]));
					if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTREG")
					{
						int num12 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
						if (num12 >= 0)
						{
							cl[num12] = text2;
						}
					}
					else
					{
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "=" + text2 + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
					ch.Enabled = true;
					ci++;
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "??")
				{
					int num13 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					double num14 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
					if (num13 >= 0)
					{
						try
						{
							num14 = Convert.ToDouble(cl[num13]);
						}
						catch
						{
						}
					}
					double value = ck - num14;
					this.m_p.Rows[ci]["Count"] = Convert.ToDouble(value.ToString("G5"));
					if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
					{
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
					else if (Math.Abs(value) < Convert.ToDouble(this.m_p.Rows[ci]["Var"]))
					{
						ck = Math.Pow(10.0, 14.0);
						ci++;
					}
					else
					{
						ck = Math.Pow(10.0, 14.0);
						ci--;
					}
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "?=")
				{
					int num15 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					double num16 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
					if (num15 >= 0)
					{
						try
						{
							num16 = Convert.ToDouble(cl[num15]);
						}
						catch
						{
						}
					}
					double value2 = ck - num16;
					this.m_p.Rows[ci]["Count"] = Convert.ToDouble(value2.ToString("G5"));
					if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
					{
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
					else if (Math.Abs(value2) < Convert.ToDouble(this.m_p.Rows[ci]["Var"]))
					{
						ck = Math.Pow(10.0, 14.0);
						ci += 2;
					}
					else
					{
						ck = Math.Pow(10.0, 14.0);
						ci++;
					}
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "?>")
				{
					int num17 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					double num18 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
					if (num17 >= 0)
					{
						try
						{
							num18 = Convert.ToDouble(cl[num17]);
						}
						catch
						{
						}
					}
					double num19 = ck - num18;
					this.m_p.Rows[ci]["Count"] = Convert.ToDouble(num19.ToString("G5"));
					if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
					{
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
					else if (num19 > 0.0)
					{
						ck = Math.Pow(10.0, 14.0);
						ci += 2;
					}
					else
					{
						ck = Math.Pow(10.0, 14.0);
						ci++;
					}
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "?<")
				{
					int num20 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					double num21 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
					if (num20 >= 0)
					{
						try
						{
							num21 = Convert.ToDouble(cl[num20]);
						}
						catch
						{
						}
					}
					double num22 = ck - num21;
					this.m_p.Rows[ci]["Count"] = Convert.ToDouble(num22.ToString("G5"));
					if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
					{
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
					else if (num22 < 0.0)
					{
						ck = Math.Pow(10.0, 14.0);
						ci += 2;
					}
					else
					{
						ck = Math.Pow(10.0, 14.0);
						ci++;
					}
				}
			}
			else
			{
				ch.Enabled = false;
				ci = 0;
				f2.BackColor = SystemColors.Control;
				f2.Text = "开始";
			}
			if (ch.Enabled)
			{
				cm++;
				if (cm % 2 == 0)
				{
					f2.BackColor = Color.LightGreen;
				}
				else
				{
					f2.BackColor = Color.GreenYellow;
				}
			}
			else
			{
				cm = 0u;
			}
		});
		Interlocked.CompareExchange(ref cg, 0, 1);
	}

	public void at()
	{
		int num = 0;
		this.m_i.Clear();
		try
		{
			this.m_au.RowFilter = "IsButton = 'true' and Addr = " + Convert.ToString(a2());
		}
		catch
		{
			this.m_au.RowFilter = "IsButton = 'true'";
		}
		for (int num2 = 0; num2 < this.m_au.Count; num2++)
		{
			DataView dataView = dg.b(Convert.ToString(this.m_au[num2].Row["ParamType"]), Convert.ToInt32(this.m_au[num2].Row["Addr"]));
			dataView.Sort = "ParamValue Desc";
			for (int num3 = 0; num3 < dataView.Count; num3++)
			{
				num++;
				DataRow dataRow = this.m_i.NewRow();
				dataRow["ButtonName"] = "button" + Convert.ToString(num);
				dataRow["ButtonCmdString"] = Convert.ToString(this.m_au[num2].Row["ParamName"]) + "=" + Convert.ToString(dataView[num3].Row["ParamValue"]) + "@" + this.m_au[num2].Row["Addr"].ToString();
				dataRow["ButtonText"] = Convert.ToString(dataView[num3].Row["CmdText"]) + " " + Convert.ToString(this.m_au[num2].Row["FolderName"]) + " " + Convert.ToString(this.m_au[num2].Row["ParamText"]);
				this.m_i.Rows.Add(dataRow);
			}
		}
		int num4 = this.m_i.Rows.Count;
		if (num4 > 22)
		{
			num4 = 22;
		}
		@do.Items.Clear();
		for (int num5 = 0; num5 < num4; num5++)
		{
			if (num5 != 0)
			{
				this.m_a1[num5 - 1] = new ToolStripSeparator();
				@do.Items.Add(this.m_a1[num5 - 1]);
			}
			this.m_a0[num5] = new ToolStripButton();
			@do.Items.Add(this.m_a0[num5]);
			this.m_a0[num5].DisplayStyle = ToolStripItemDisplayStyle.ImageAndText;
			this.m_a0[num5].Image = new Bitmap(Assembly.GetExecutingAssembly().GetManifestResourceStream("EasyHost.Resources.mouse_select_left.png"));
			this.m_a0[num5].Text = this.m_i.Rows[num5]["ButtonText"].ToString();
			this.m_a0[num5].ImageAlign = System.Drawing.ContentAlignment.MiddleLeft;
			this.m_a0[num5].TextAlign = System.Drawing.ContentAlignment.MiddleLeft;
			this.m_a0[num5].ToolTipText = this.m_i.Rows[num5]["ButtonCmdString"].ToString();
		}
	}

	private void a(object A_0, ToolStripItemClickedEventArgs A_1)
	{
		if (b5 != null && b5.c())
		{
			cp.Enqueue(A_1.ClickedItem.ToolTipText);
		}
	}

	private void bn(object A_0, EventArgs A_1)
	{
		if (cs == 0)
		{
			cp.Enqueue(cq + "=" + dw.Text.ToString() + "@" + cr);
			cp.Enqueue(cq + "?@" + cr);
			dv.Visible = false;
		}
		else if (cs == 1)
		{
			cp.Enqueue(cq + "=" + dz.SelectedValue.ToString() + "@" + cr);
			cp.Enqueue(cq + "?@" + cr);
			dv.Visible = false;
		}
	}

	private void bm(object A_0, EventArgs A_1)
	{
		dv.Visible = false;
	}

	private void b(object A_0, KeyEventArgs A_1)
	{
		if (A_1.KeyCode == Keys.Return)
		{
			cp.Enqueue(cq + "=" + dw.Text.ToString() + "@" + cr);
			cp.Enqueue(cq + "?@" + cr);
			dv.Visible = false;
		}
		else if (A_1.KeyCode == Keys.Escape)
		{
			dv.Visible = false;
		}
	}

	private void a(object A_0, KeyEventArgs A_1)
	{
		if (A_1.KeyCode == Keys.Return)
		{
			cp.Enqueue(cq + "=" + dz.SelectedValue.ToString() + "@" + cr);
			cp.Enqueue(cq + "?@" + cr);
			dv.Visible = false;
		}
		else if (A_1.KeyCode == Keys.Escape)
		{
			dv.Visible = false;
		}
	}

	private void a(object A_0, DrawItemEventArgs A_1)
	{
		string text = Convert.ToString((dz.Items[A_1.Index] as DataRowView)[dz.DisplayMember]);
		int num = (int)CreateGraphics().MeasureString(text, A_1.Font).Width;
		A_1.DrawBackground();
		A_1.Graphics.DrawString(text, A_1.Font, Brushes.Black, A_1.Bounds);
		if ((A_1.State & DrawItemState.Selected) == DrawItemState.Selected && dz.DroppedDown)
		{
			if (dz.Width < num)
			{
				ct.Show(text, dz, A_1.Bounds.X + A_1.Bounds.Width, A_1.Bounds.Y + A_1.Bounds.Height);
			}
			else
			{
				ct.Hide(dz);
			}
			A_1.DrawFocusRectangle();
		}
	}

	private void bl(object A_0, EventArgs A_1)
	{
		ct.Hide(dz);
	}

	private void bk(object A_0, EventArgs A_1)
	{
		dv.Visible = false;
	}

	protected void a(object A_0, ListChangedEventArgs A_1)
	{
		if (A_1.ListChangedType == ListChangedType.ItemAdded)
		{
			d4.RowCount = 0;
			DataTable dataTable = this.m_aq.ToTable();
			int num = 0;
			while (num == 0)
			{
				num = 1;
				try
				{
					for (int num2 = 0; num2 < dataTable.Rows.Count; num2++)
					{
						DataRow dataRow = dataTable.Rows[num2];
						int num3 = 0;
						int num4 = 0;
						for (num4 = 0; num4 < this.m_j.Columns.Count - 2; num4++)
						{
							if (dataRow["ParamName"].ToString() + "@" + dataRow["Addr"].ToString() + "-" + dataRow["Index"].ToString() == this.m_j.Columns[num4 + 2].ColumnName)
							{
								num3 = 1;
							}
						}
						if (num3 == 0)
						{
							this.m_j.Columns.Add(dataRow["ParamName"].ToString() + "@" + dataRow["Addr"].ToString() + "-" + dataRow["Index"].ToString(), typeof(double));
							num = 0;
						}
					}
				}
				catch
				{
				}
			}
			try
			{
				for (int num5 = 0; num5 < dataTable.Rows.Count; num5++)
				{
					DataRow dataRow2 = dataTable.Rows[num5];
					while (this.m_j.Columns[num5 + 2].ColumnName != dataRow2["ParamName"].ToString() + "@" + dataRow2["Addr"].ToString() + "-" + dataRow2["Index"].ToString())
					{
						this.m_j.Columns[num5 + 2].SetOrdinal(dataTable.Rows.Count + 1);
					}
				}
			}
			catch (Exception ex)
			{
				MessageBox.Show(ex.Message);
			}
			while (d4.ColumnCount > 2)
			{
				d4.Columns.RemoveAt(2);
			}
			for (int num6 = 0; num6 < this.m_j.Columns.Count - 2; num6++)
			{
				d4.Columns.Add("new", "new");
				d4.Columns[num6 + 2].DataPropertyName = this.m_aq[num6].Row["ParamName"].ToString() + "@" + this.m_aq[num6].Row["Addr"].ToString() + "-" + this.m_aq[num6].Row["Index"].ToString();
				d4.Columns[num6 + 2].HeaderText = this.m_aq[num6].Row["FolderName"].ToString() + " " + this.m_aq[num6].Row["ParamText"].ToString() + "@" + this.m_aq[num6].Row["Addr"].ToString();
				d4.Columns[num6 + 2].ReadOnly = true;
				d4.Columns[num6 + 2].SortMode = DataGridViewColumnSortMode.NotSortable;
				d4.Columns[num6 + 2].AutoSizeMode = DataGridViewAutoSizeColumnMode.DisplayedCells;
				this.m_j.Columns[num6 + 2].Caption = d4.Columns[num6 + 2].HeaderText;
			}
			a6();
			w();
		}
		if (A_1.ListChangedType == ListChangedType.ItemDeleted)
		{
			d4.RowCount = 0;
			int num7 = 0;
			DataTable dataTable2 = this.m_aq.ToTable();
			while (num7 == 0)
			{
				num7 = 1;
				try
				{
					for (int num8 = 2; num8 < this.m_j.Columns.Count; num8++)
					{
						int num9 = 0;
						for (int num10 = 0; num10 < dataTable2.Rows.Count; num10++)
						{
							DataRow dataRow3 = dataTable2.Rows[num10];
							if (dataRow3["ParamName"].ToString() + "@" + dataRow3["Addr"].ToString() + "-" + dataRow3["Index"].ToString() == this.m_j.Columns[num8].ColumnName)
							{
								num9 = 1;
							}
						}
						if (num9 == 0)
						{
							this.m_j.Columns.RemoveAt(num8);
							d4.Columns.RemoveAt(num8);
							num7 = 0;
						}
					}
				}
				catch
				{
				}
			}
			a6();
			w();
		}
		if (A_1.ListChangedType != ListChangedType.Reset)
		{
			return;
		}
		d4.RowCount = 0;
		int num11 = 0;
		DataTable dataTable3 = this.m_aq.ToTable();
		while (num11 == 0)
		{
			num11 = 1;
			try
			{
				for (int num12 = 2; num12 < this.m_j.Columns.Count; num12++)
				{
					int num13 = 0;
					for (int num14 = 0; num14 < dataTable3.Rows.Count; num14++)
					{
						DataRow dataRow4 = dataTable3.Rows[num14];
						if (dataRow4["ParamName"].ToString() + "@" + dataRow4["Addr"].ToString() + "-" + dataRow4["Index"].ToString() == this.m_j.Columns[num12].ColumnName)
						{
							num13 = 1;
						}
					}
					if (num13 == 0)
					{
						this.m_j.Columns.RemoveAt(num12);
						d4.Columns.RemoveAt(num12);
						num11 = 0;
					}
				}
			}
			catch
			{
			}
		}
		a6();
		w();
	}

	private void c(object A_0, DataGridViewCellMouseEventArgs A_1)
	{
		if (A_1.Button != MouseButtons.Right)
		{
			return;
		}
		int rowIndex = A_1.RowIndex;
		if (!bu)
		{
			if (rowIndex >= 0)
			{
				try
				{
					d5.ClearSelection();
					d5.CurrentCell = d5.Rows[rowIndex].Cells[0];
					d5.Rows[rowIndex].Selected = true;
					g6.Show(Control.MousePosition.X, Control.MousePosition.Y);
				}
				catch (Exception)
				{
				}
			}
		}
		else
		{
			a("Error: Can not reselect parameter when auto save mode！", Color.Red);
		}
	}

	private void bj(object A_0, EventArgs A_1)
	{
		try
		{
			int index = d5.SelectedRows[0].Index;
			this.m_ap[index].Row["datalevel"] = "0";
			this.m_a6 = false;
			a5();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void bi(object A_0, EventArgs A_1)
	{
		try
		{
			int index = d5.SelectedRows[0].Index;
			this.m_ap[index].Row["datalevel"] = "1";
			this.m_a6 = false;
			a5();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void bh(object A_0, EventArgs A_1)
	{
		try
		{
			int index = d5.SelectedRows[0].Index;
			if (Convert.ToString(this.m_ap[index].Row["paramtype"]) == "STR")
			{
				MessageBox.Show(g("warning_thisisastring", "This is a string. Do not record it or draw its curve!"), g("text_error", "Error"));
				return;
			}
			this.m_ap[index].Row["datalevel"] = "2";
			this.m_a6 = false;
			a5();
			a6();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void bg(object A_0, EventArgs A_1)
	{
		try
		{
			int index = d5.SelectedRows[0].Index;
			if (Convert.ToString(this.m_ap[index].Row["paramtype"]) == "STR")
			{
				MessageBox.Show(g("warning_thisisastring", "This is a string. Do not record it or draw its curve!"), g("text_error", "Error"));
				return;
			}
			this.m_ap[index].Row["datalevel"] = "3";
			this.m_a6 = false;
			a5();
			a6();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void bf(object A_0, EventArgs A_1)
	{
		try
		{
			int index = d5.SelectedRows[0].Index;
			if (Convert.ToInt32(this.m_ap[index].Row["datalevel"]) > 1)
			{
				string text = this.m_ap[index].Row["ParamName"].ToString() + "@" + this.m_ap[index].Row["Addr"].ToString() + "-" + this.m_ap[index].Row["Index"].ToString();
				double num = Convert.ToDouble(this.m_j.Compute("Max([" + text + "])", ""));
				double num2 = Convert.ToDouble(this.m_j.Compute("Min([" + text + "])", ""));
				double num3 = Convert.ToDouble(this.m_j.Compute("StDev([" + text + "])", ""));
				double num4 = Convert.ToDouble(this.m_j.Compute("Avg([" + text + "])", ""));
				string text2 = ":  ";
				string text3 = g("datastatistics_max", "MAX");
				string text4 = g("datastatistics_min", "MIN");
				string text5 = g("datastatistics_avg", "AVG");
				string text6 = g("datastatistics_stddev", "STDDEV");
				MessageBox.Show(text3 + text2 + num + "\r\n" + text4 + text2 + num2 + "\r\n" + text5 + text2 + num4 + "\r\n" + text6 + text2 + num3, g("datastatistics_result", "Statistics Result"));
			}
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void a(string A_0, Point A_1)
	{
		dv.Location = A_1;
		dv.Visible = true;
		if (A_0 == "STR64")
		{
			dv.Height = Convert.ToInt32((double)dy.Height * 4.5789);
			dw.Height = Convert.ToInt32((double)dy.Height * 3.1579);
			dy.Top = Convert.ToInt32((double)dy.Height * 3.4737);
			dx.Top = Convert.ToInt32((double)dy.Height * 3.4737);
			dw.Visible = true;
			dz.Visible = false;
			base.ActiveControl = dw;
			dw.Focus();
		}
		else if (A_0 == "OPT")
		{
			dv.Height = Convert.ToInt32((double)dy.Height * 2.2105);
			dw.Height = Convert.ToInt32((double)dy.Height * 0.9);
			dy.Top = Convert.ToInt32((double)dy.Height * 1.1052);
			dx.Top = Convert.ToInt32((double)dy.Height * 1.1052);
			dz.Visible = true;
			dw.Visible = false;
			dv.Visible = true;
			base.ActiveControl = dz;
			dz.Focus();
		}
		else
		{
			dv.Height = Convert.ToInt32((double)dy.Height * 2.2105);
			dw.Height = Convert.ToInt32((double)dy.Height * 0.9);
			dy.Top = Convert.ToInt32((double)dy.Height * 1.1052);
			dx.Top = Convert.ToInt32((double)dy.Height * 1.1052);
			dw.Visible = true;
			dz.Visible = false;
			base.ActiveControl = dw;
			dw.Focus();
		}
	}

	private void b(object A_0, DataGridViewCellMouseEventArgs A_1)
	{
		int columnIndex = A_1.ColumnIndex;
		int rowIndex = A_1.RowIndex;
		if (A_1.Button == MouseButtons.Left)
		{
			if (columnIndex < 0 || rowIndex < 0)
			{
				return;
			}
			if (b5 != null && b5.c())
			{
				if (dm.Columns[columnIndex].Name == "up")
				{
					DataRowView dataRowView = this.m_an[rowIndex];
					if (Convert.ToInt32(dataRowView["ParamLevel"]) >= 2)
					{
						cp.Enqueue(dataRowView["ParamName"].ToString() + "^" + df + "@" + dataRowView["Addr"].ToString());
						cp.Enqueue(dataRowView["ParamName"].ToString() + "?@" + dataRowView["Addr"].ToString());
					}
					else
					{
						a(g("text_error", "Error") + ": [" + dataRowView["ParamText"].ToString() + "] " + g("param_text_modifyforbidden", "This parameter could not be modified by user!"), Color.Red);
					}
				}
				else if (dm.Columns[columnIndex].Name == "down")
				{
					DataRowView dataRowView2 = this.m_an[rowIndex];
					if (Convert.ToInt32(dataRowView2["ParamLevel"]) >= 2)
					{
						cp.Enqueue(dataRowView2["ParamName"].ToString() + "^-" + df + "@" + dataRowView2["Addr"].ToString());
						cp.Enqueue(dataRowView2["ParamName"].ToString() + "?@" + dataRowView2["Addr"].ToString());
					}
					else
					{
						a(g("text_error", "Error") + ": [" + dataRowView2["ParamText"].ToString() + "] " + g("param_text_modifyforbidden", "This parameter could not be modified by user!"), Color.Red);
					}
				}
				else if (dm.Columns[columnIndex].Name == "save")
				{
					DataRowView dataRowView3 = this.m_an[rowIndex];
					if (Convert.ToInt32(dataRowView3["ParamLevel"]) >= 3)
					{
						cp.Enqueue(dataRowView3["ParamName"].ToString() + "!@" + dataRowView3["Addr"].ToString());
					}
					else
					{
						a(g("text_error", "Error") + ": [" + dataRowView3["ParamText"].ToString() + "] " + g("param_text_saveforbidden", "This parameter could not be saved by user!"), Color.Red);
					}
				}
				else if (dm.Columns[columnIndex].Name == "ValueText")
				{
					DataRowView dataRowView4 = this.m_an[rowIndex];
					string text = a(dataRowView4["paramtype"].ToString(), Convert.ToInt32(dataRowView4["Addr"]));
					if (text.EndsWith("[HSTR]"))
					{
						text = text.Substring(0, text.Length - 6);
					}
					if (Convert.ToInt32(dataRowView4["ParamLevel"]) >= 2)
					{
						cq = dataRowView4["ParamName"].ToString();
						cr = dataRowView4["Addr"].ToString();
						dz.Visible = false;
						dw.Visible = false;
						switch (text)
						{
						case "NUM":
						case "ERRORCODE":
							cs = 0;
							dw.Text = dataRowView4["paramvalue"].ToString();
							a("NUM", new Point(dm.Location.X + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).X, dm.Location.Y + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).Y));
							break;
						case "STR":
							cs = 0;
							dw.Text = dataRowView4["paramvalue"].ToString();
							a("STR", new Point(dm.Location.X + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).X, dm.Location.Y + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).Y));
							break;
						case "STR64":
							cs = 0;
							dw.Text = dataRowView4["paramvalue"].ToString();
							a("STR64", new Point(dm.Location.X + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).X, dm.Location.Y + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).Y));
							break;
						default:
						{
							cs = 1;
							DataView dataView = dg.b(text, Convert.ToInt32(dataRowView4["Addr"]));
							if (dataView.Count == 0)
							{
								cs = 0;
								dw.Text = dataRowView4["paramvalue"].ToString();
								a("NUM", new Point(dm.Location.X + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).X, dm.Location.Y + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).Y));
								return;
							}
							bool flag = false;
							DataTable dataTable = dataView.ToTable();
							DataView dataView2 = new DataView(dataTable);
							int num = 0;
							if (int.TryParse(dataRowView4["paramvalue"].ToString(), out var result))
							{
								num = result;
							}
							for (int num2 = 0; num2 < dataView2.Count; num2++)
							{
								if (Convert.ToInt32(dataView2[num2]["ParamValue"]) == num)
								{
									flag = true;
								}
							}
							if (!flag)
							{
								DataRow dataRow = dataTable.NewRow();
								dataRow["ParamType"] = dataRowView4["paramtype"].ToString();
								dataRow["ParamValue"] = num;
								dataRow["ValueText"] = num.ToString();
								dataTable.Rows.InsertAt(dataRow, dataTable.Rows.Count);
							}
							dz.DataSource = dataView2;
							try
							{
								dz.SelectedValue = num;
							}
							catch
							{
								dz.SelectedIndex = 0;
							}
							a("OPT", new Point(dm.Location.X + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).X, dm.Location.Y + dm.GetCellDisplayRectangle(columnIndex, rowIndex, cutOverflow: true).Y));
							break;
						}
						}
					}
					else if (text == "BTN")
					{
						if (dataRowView4["ToolTips"].ToString().Length > 0)
						{
							if (MessageBox.Show(dataRowView4["ToolTips"].ToString(), g("text_confirm", "Confirm"), MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes)
							{
								cp.Enqueue(dataRowView4["ParamName"].ToString() + "=" + dataRowView4["ValueText"].ToString() + "@" + dataRowView4["Addr"].ToString());
							}
						}
						else
						{
							cp.Enqueue(dataRowView4["ParamName"].ToString() + "=" + dataRowView4["ValueText"].ToString() + "@" + dataRowView4["Addr"].ToString());
						}
					}
					else
					{
						a(g("text_error", "Error") + ": [" + dataRowView4["ParamText"].ToString() + "] " + g("param_text_modifyforbidden", "This parameter could not be modified by user!"), Color.Red);
					}
				}
			}
			if (!(dm.Columns[columnIndex].Name == "help"))
			{
				return;
			}
			try
			{
				DataRowView dataRowView5 = this.m_an[rowIndex];
				fa.Clear();
				fa.SelectionColor = Color.Black;
				fa.AppendText("   " + g("info_paramname", "Parameter") + ": ");
				fa.SelectionColor = Color.Red;
				fa.AppendText(dataRowView5["ParamText"].ToString() + "\n");
				fa.SelectionColor = Color.Black;
				fa.AppendText("   " + g("info_paramcmd", "Command") + ": " + dataRowView5["ParamName"].ToString() + "\n");
				string text2 = a(dataRowView5["paramtype"].ToString(), Convert.ToInt32(dataRowView5["Addr"]));
				if (text2 != dataRowView5["paramtype"].ToString())
				{
					fa.AppendText("   " + g("info_datatype", "Data Type") + ": " + text2 + "\n");
				}
				else
				{
					fa.AppendText("   " + g("info_datatype", "Data Type") + ": " + dataRowView5["paramtype"].ToString() + "\n");
				}
				if (text2.EndsWith("[HSTR]"))
				{
					text2 = text2.Substring(0, text2.Length - 6);
				}
				DataView dataView3 = dg.b(text2, Convert.ToInt32(dataRowView5["Addr"]));
				if (dataView3.Count > 0)
				{
					for (int num3 = 0; num3 < dataView3.Count; num3++)
					{
						fa.AppendText(string.Concat("   ", g("info_paramvalue", "Option Value"), ": ", dataView3[num3]["paramvalue"], " (", g("info_valueText", "Option Description"), ": ", dataView3[num3]["ValueText"], ")\n"));
					}
				}
				string a_ = dataRowView5["ParamName"].ToString().Split(':')[1];
				string text3 = dh.b(a_);
				if (text3 != "")
				{
					fa.AppendText("   " + g("info_paramhelp", "Other info below:") + ": \n");
					fa.AppendText(text3.Replace("\\n", Environment.NewLine) + "\n");
				}
				else
				{
					fa.AppendText("   " + g("info_noparamhelp", "Sorry! No more information, please read its user manual!"));
				}
				e9.Visible = true;
				string text4 = dh.c(a_);
				if (!(text4 != ""))
				{
					return;
				}
				string text5 = this.m_bc + this.m_bd + this.m_bj + this.m_bk + text4;
				if (!File.Exists(text5))
				{
					return;
				}
				using Bitmap bitmap = new Bitmap(text5);
				float num4 = (float)fa.ClientSize.Width / (float)bitmap.Width;
				if (num4 >= 1f)
				{
					num4 = 1f;
				}
				int num5 = (int)((float)bitmap.Width * num4);
				int num6 = (int)((float)bitmap.Height * num4);
				using Bitmap image = new Bitmap(bitmap, new Size(num5, num6));
				Clipboard.Clear();
				Clipboard.SetImage(image);
				fa.ReadOnly = false;
				fa.Paste();
				fa.ReadOnly = true;
				Clipboard.Clear();
				return;
			}
			catch
			{
				return;
			}
		}
		if (A_1.Button != MouseButtons.Right)
		{
			return;
		}
		if (!bu)
		{
			if (rowIndex < 0)
			{
				return;
			}
			try
			{
				dm.ClearSelection();
				dm.CurrentCell = dm.Rows[rowIndex].Cells[0];
				dm.Rows[rowIndex].Selected = true;
				if (Convert.ToInt32(this.m_an[rowIndex].Row["paramlevel"]) < 1)
				{
					return;
				}
				foreach (ToolStripItem item in d6.Items)
				{
					item.Visible = true;
				}
				if (Convert.ToString(this.m_an[rowIndex].Row["paramtype"]).Substring(0, 3) == "STR")
				{
					d8.Visible = false;
					d7.Visible = false;
				}
				d6.Show(Control.MousePosition.X, Control.MousePosition.Y);
				return;
			}
			catch (Exception)
			{
				return;
			}
		}
		a("Error: Can not reselect parameter when auto save mode！", Color.Red);
	}

	private void a(object A_0, DataGridViewCellPaintingEventArgs A_1)
	{
		try
		{
			if (A_1.ColumnIndex == dm.Columns["up"].Index && A_1.RowIndex >= 0 && A_1.RowIndex < dm.Rows.Count)
			{
				if ((int)this.m_an[A_1.RowIndex].Row["ParamLevel"] >= 2)
				{
					return;
				}
				new Rectangle(A_1.CellBounds.X + 1, A_1.CellBounds.Y + 1, A_1.CellBounds.Width - 4, A_1.CellBounds.Height - 4);
				using (Brush brush = new SolidBrush(dm.GridColor))
				{
					using Brush brush2 = new SolidBrush(A_1.CellStyle.BackColor);
					A_1.Graphics.FillRectangle(brush2, A_1.CellBounds);
					using Pen pen = new Pen(brush);
					A_1.Graphics.DrawLine(pen, A_1.CellBounds.Left, A_1.CellBounds.Bottom - 1, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom - 1);
					A_1.Graphics.DrawLine(pen, A_1.CellBounds.Right - 1, A_1.CellBounds.Top, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom);
					Rectangle cellBounds = A_1.CellBounds;
					cellBounds.X++;
					cellBounds.Y++;
					cellBounds.Height -= 2;
					cellBounds.Width -= 2;
					ButtonRenderer.DrawButton(A_1.Graphics, cellBounds, PushButtonState.Disabled);
					TextRenderer.DrawText(A_1.Graphics, g("param_dgv_up", "Up"), dm.Font, cellBounds, SystemColors.GrayText);
				}
				A_1.Handled = true;
			}
			else if (A_1.ColumnIndex == dm.Columns["down"].Index && A_1.RowIndex >= 0 && A_1.RowIndex < dm.Rows.Count)
			{
				if ((int)this.m_an[A_1.RowIndex].Row["ParamLevel"] >= 2)
				{
					return;
				}
				new Rectangle(A_1.CellBounds.X + 1, A_1.CellBounds.Y + 1, A_1.CellBounds.Width - 4, A_1.CellBounds.Height - 4);
				using (Brush brush3 = new SolidBrush(dm.GridColor))
				{
					using Brush brush4 = new SolidBrush(A_1.CellStyle.BackColor);
					A_1.Graphics.FillRectangle(brush4, A_1.CellBounds);
					using Pen pen2 = new Pen(brush3);
					A_1.Graphics.DrawLine(pen2, A_1.CellBounds.Left, A_1.CellBounds.Bottom - 1, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom - 1);
					A_1.Graphics.DrawLine(pen2, A_1.CellBounds.Right - 1, A_1.CellBounds.Top, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom);
					Rectangle cellBounds2 = A_1.CellBounds;
					cellBounds2.X++;
					cellBounds2.Y++;
					cellBounds2.Height -= 2;
					cellBounds2.Width -= 2;
					ButtonRenderer.DrawButton(A_1.Graphics, cellBounds2, PushButtonState.Disabled);
					TextRenderer.DrawText(A_1.Graphics, g("param_dgv_down", "Down"), dm.Font, cellBounds2, SystemColors.GrayText);
				}
				A_1.Handled = true;
			}
			else if (A_1.ColumnIndex == dm.Columns["save"].Index && A_1.RowIndex >= 0 && A_1.RowIndex < dm.Rows.Count)
			{
				if ((int)this.m_an[A_1.RowIndex].Row["ParamLevel"] >= 3)
				{
					return;
				}
				new Rectangle(A_1.CellBounds.X + 1, A_1.CellBounds.Y + 1, A_1.CellBounds.Width - 4, A_1.CellBounds.Height - 4);
				using (Brush brush5 = new SolidBrush(dm.GridColor))
				{
					using Brush brush6 = new SolidBrush(A_1.CellStyle.BackColor);
					A_1.Graphics.FillRectangle(brush6, A_1.CellBounds);
					using Pen pen3 = new Pen(brush5);
					A_1.Graphics.DrawLine(pen3, A_1.CellBounds.Left, A_1.CellBounds.Bottom - 1, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom - 1);
					A_1.Graphics.DrawLine(pen3, A_1.CellBounds.Right - 1, A_1.CellBounds.Top, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom);
					Rectangle cellBounds3 = A_1.CellBounds;
					cellBounds3.X++;
					cellBounds3.Y++;
					cellBounds3.Height -= 2;
					cellBounds3.Width -= 2;
					ButtonRenderer.DrawButton(A_1.Graphics, cellBounds3, PushButtonState.Disabled);
					TextRenderer.DrawText(A_1.Graphics, g("param_dgv_save", "Save"), dm.Font, cellBounds3, SystemColors.GrayText);
				}
				A_1.Handled = true;
			}
			else if (A_1.ColumnIndex == dm.Columns["help"].Index && A_1.RowIndex >= 0 && A_1.RowIndex < dm.Rows.Count)
			{
				string a_ = this.m_an[A_1.RowIndex].Row["ParamName"].ToString().Split(':')[1];
				if (dh.d(a_))
				{
					return;
				}
				new Rectangle(A_1.CellBounds.X + 1, A_1.CellBounds.Y + 1, A_1.CellBounds.Width - 4, A_1.CellBounds.Height - 4);
				using (Brush brush7 = new SolidBrush(dm.GridColor))
				{
					using Brush brush8 = new SolidBrush(A_1.CellStyle.BackColor);
					A_1.Graphics.FillRectangle(brush8, A_1.CellBounds);
					using Pen pen4 = new Pen(brush7);
					A_1.Graphics.DrawLine(pen4, A_1.CellBounds.Left, A_1.CellBounds.Bottom - 1, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom - 1);
					A_1.Graphics.DrawLine(pen4, A_1.CellBounds.Right - 1, A_1.CellBounds.Top, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom);
					Rectangle cellBounds4 = A_1.CellBounds;
					cellBounds4.X++;
					cellBounds4.Y++;
					cellBounds4.Height -= 2;
					cellBounds4.Width -= 2;
					ButtonRenderer.DrawButton(A_1.Graphics, cellBounds4, PushButtonState.Disabled);
					TextRenderer.DrawText(A_1.Graphics, g("param_dgv_info", "Help"), dm.Font, cellBounds4, SystemColors.GrayText);
				}
				A_1.Handled = true;
			}
			else
			{
				if (A_1.ColumnIndex != dm.Columns["ValueText"].Index || A_1.RowIndex < 0 || A_1.RowIndex >= dm.Rows.Count || (int)this.m_an[A_1.RowIndex].Row["ParamLevel"] >= 1 || !(this.m_an[A_1.RowIndex].Row["ParamType"].ToString() == "BTN"))
				{
					return;
				}
				new Rectangle(A_1.CellBounds.X + 1, A_1.CellBounds.Y + 1, A_1.CellBounds.Width - 4, A_1.CellBounds.Height - 4);
				using (Brush brush9 = new SolidBrush(dm.GridColor))
				{
					using Brush brush10 = new SolidBrush(A_1.CellStyle.BackColor);
					A_1.Graphics.FillRectangle(brush10, A_1.CellBounds);
					using Pen pen5 = new Pen(brush9);
					A_1.Graphics.DrawLine(pen5, A_1.CellBounds.Left, A_1.CellBounds.Bottom - 1, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom - 1);
					A_1.Graphics.DrawLine(pen5, A_1.CellBounds.Right - 1, A_1.CellBounds.Top, A_1.CellBounds.Right - 1, A_1.CellBounds.Bottom);
					Rectangle cellBounds5 = A_1.CellBounds;
					cellBounds5.X++;
					cellBounds5.Y++;
					cellBounds5.Height -= 2;
					cellBounds5.Width -= 2;
					ButtonRenderer.DrawButton(A_1.Graphics, cellBounds5, PushButtonState.Normal);
					TextRenderer.DrawText(A_1.Graphics, g("param_dgv_run", "Run"), dm.Font, cellBounds5, SystemColors.ControlText);
				}
				A_1.Handled = true;
			}
		}
		catch
		{
		}
	}

	private void a(object A_0, DataGridViewCellToolTipTextNeededEventArgs A_1)
	{
		try
		{
			if (dm.Columns[A_1.ColumnIndex].HeaderText == "参数名称")
			{
				A_1.ToolTipText = this.m_an[A_1.RowIndex].Row["tooltips"].ToString();
			}
		}
		catch
		{
		}
	}

	private void be(object A_0, EventArgs A_1)
	{
		try
		{
			int index = dm.SelectedRows[0].Index;
			this.m_an[index].Row["datalevel"] = "0";
			this.m_a6 = false;
			a5();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void bd(object A_0, EventArgs A_1)
	{
		try
		{
			int index = dm.SelectedRows[0].Index;
			this.m_an[index].Row["datalevel"] = "1";
			this.m_a6 = false;
			a5();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void bc(object A_0, EventArgs A_1)
	{
		try
		{
			int index = dm.SelectedRows[0].Index;
			if (Convert.ToString(this.m_an[index].Row["paramtype"]) == "STR")
			{
				MessageBox.Show(g("warning_thisisastring", "This is a string. Do not record it or draw its curve!"), g("text_error", "Error"));
				return;
			}
			this.m_an[index].Row["datalevel"] = "2";
			this.m_a6 = false;
			a5();
			a6();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void bb(object A_0, EventArgs A_1)
	{
		try
		{
			int index = dm.SelectedRows[0].Index;
			if (Convert.ToString(this.m_an[index].Row["paramtype"]) == "STR")
			{
				MessageBox.Show(g("warning_thisisastring", "This is a string. Do not record it or draw its curve!"), g("text_error", "Error"));
				return;
			}
			this.m_an[index].Row["datalevel"] = "3";
			this.m_a6 = false;
			a5();
			a6();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void ba(object A_0, EventArgs A_1)
	{
		int index = dm.SelectedRows[0].Index;
		string[] array = this.m_an[index].Row["ParamName"].ToString().Split(new char[1] { ':' }, 2);
		DataRow dataRow = this.m_p.NewRow();
		dataRow["ModuleName"] = array[0];
		dataRow["ParamName"] = array[1];
		dataRow["ParamText"] = this.m_an[index].Row["ParamText"];
		dataRow["ValueText"] = this.m_an[index].Row["ParamValue"];
		dataRow["Addr"] = this.m_an[index].Row["Addr"];
		dataRow["Var"] = "1";
		dataRow["Count"] = "0";
		dataRow["Reg"] = -1;
		if (Convert.ToInt32(this.m_an[index].Row["ParamLevel"]) > 1)
		{
			dataRow["Cmd"] = "=";
		}
		else
		{
			dataRow["Cmd"] = "?";
		}
		this.m_p.Rows.Add(dataRow);
	}

	private void a9(object A_0, EventArgs A_1)
	{
		try
		{
			int index = dm.SelectedRows[0].Index;
			this.m_an[index].Row["Mark"] = "1";
			this.m_a6 = false;
			a5();
			a6();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	private void a8(object A_0, EventArgs A_1)
	{
		try
		{
			int index = dm.SelectedRows[0].Index;
			this.m_an[index].Row["Mark"] = "0";
			this.m_a6 = false;
			a5();
			a6();
		}
		catch (Exception ex)
		{
			MessageBox.Show("Failed: " + ex.Message);
		}
	}

	public void au()
	{
		eq.Font = new Font(this.m_c, 10f, FontStyle.Regular);
		eq.Nodes.Clear();
		eq.ImageList = dd;
		TreeNode treeNode = null;
		DataView dataView = new DataView(this.m_g);
		try
		{
			dataView.RowFilter = "foldername <> '软件通讯协议' and foldername <> 'PRIVATEOPTION' and addr = " + Convert.ToString(a2());
		}
		catch
		{
			dataView.RowFilter = "foldername <> '软件通讯协议' and foldername <> 'PRIVATEOPTION' and addr = 0";
		}
		DataTable dataTable = dataView.ToTable(true, "foldername", "pagename", "page");
		dataTable.DefaultView.Sort = "page ASC";
		DataView dataView2 = new DataView(dataTable)
		{
			Sort = "page ASC"
		};
		DataView dataView3 = new DataView(dataTable);
		dataView3.Sort = "page ASC";
		DataTable dataTable2 = dataView2.ToTable(true, "foldername");
		int num = 0;
		foreach (DataRowView item in dataTable2.DefaultView)
		{
			TreeNode treeNode2 = new TreeNode();
			treeNode2.Text = item[0].ToString();
			eq.Nodes.Add(treeNode2);
			if (treeNode2.FullPath == a9())
			{
				treeNode = treeNode2;
			}
			dataView3.RowFilter = "foldername = '" + item[0].ToString() + "'";
			treeNode2.ImageIndex = 0;
			if (dataView3.Count > 1)
			{
				for (int num2 = 0; num2 < dataView3.Count; num2++)
				{
					TreeNode treeNode3 = new TreeNode();
					treeNode3.Text = dataView3[num2].Row[1].ToString();
					treeNode3.ImageIndex = 0;
					treeNode2.Nodes.Add(treeNode3);
					if (treeNode3.FullPath == a9())
					{
						treeNode = treeNode3;
					}
				}
			}
			num++;
		}
		try
		{
			eq.Focus();
			if (treeNode != null)
			{
				eq.SelectedNode = treeNode;
			}
			else
			{
				if (eq.Nodes.Count <= 0)
				{
					return;
				}
				TreeNode treeNode4 = eq.Nodes[0];
				if (treeNode4 != null)
				{
					if (treeNode4.Nodes != null && treeNode4.Nodes.Count > 0 && treeNode4.Nodes[0] != null)
					{
						eq.SelectedNode = treeNode4.Nodes[0];
					}
					else
					{
						eq.SelectedNode = treeNode4;
					}
				}
			}
		}
		catch (Exception)
		{
		}
	}

	private void a(object A_0, TreeViewEventArgs A_1)
	{
		try
		{
			if (A_1.Node.Nodes.Count > 0)
			{
				this.m_an.RowFilter = "foldername = '" + A_1.Node.Text + "'and addr =" + Convert.ToString(a2());
				if (A_1.Node.IsExpanded)
				{
					A_1.Node.Collapse();
				}
				else
				{
					A_1.Node.Expand();
				}
				if (dj != null)
				{
					dj.ToolTipText = A_1.Node.FullPath;
				}
				this.m_at.RowFilter = "Mark >0 or datalevel > 0 or pagename = '新通讯协议' or (foldername = '" + A_1.Node.Text + "' and addr = " + a2() + ")";
				this.m_at.Sort = "addr ASC";
				this.m_az = "foldername = '" + A_1.Node.Text + "'";
				return;
			}
			if (A_1.Node.Parent == null)
			{
				this.m_an.RowFilter = "pagename = '" + A_1.Node.Text + "'and addr =" + Convert.ToString(a2());
				if (dj != null)
				{
					dj.ToolTipText = A_1.Node.FullPath;
				}
				this.m_at.RowFilter = "Mark >0 or datalevel > 0 or pagename = '新通讯协议' or (pagename = '" + A_1.Node.Text + "' and addr = " + a2() + ")";
				this.m_at.Sort = "addr ASC";
				this.m_az = "pagename = '" + A_1.Node.Text + "'";
				return;
			}
			this.m_an.RowFilter = "pagename = '" + A_1.Node.Text + "'and foldername = '" + A_1.Node.Parent.Text + "'and addr =" + Convert.ToString(a2());
			if (dj != null)
			{
				dj.ToolTipText = A_1.Node.FullPath;
			}
			this.m_at.RowFilter = "Mark >0 or datalevel > 0 or pagename = '新通讯协议' or (pagename = '" + A_1.Node.Text + "' and foldername = '" + A_1.Node.Parent.Text + "'and addr = " + a2() + ")";
			this.m_at.Sort = "addr ASC";
			this.m_az = "pagename = '" + A_1.Node.Text + "' and foldername = '" + A_1.Node.Parent.Text + "'";
		}
		catch (Exception ex)
		{
			MessageBox.Show(ex.Message);
		}
	}

	private void a(object A_0, DataGridViewRowPostPaintEventArgs A_1)
	{
		SolidBrush brush = new SolidBrush(f0.RowHeadersDefaultCellStyle.ForeColor);
		string text = A_1.RowIndex.ToString(CultureInfo.CurrentUICulture);
		SizeF sizeF = A_1.Graphics.MeasureString(text, f0.DefaultCellStyle.Font);
		A_1.Graphics.DrawString(text, f0.DefaultCellStyle.Font, brush, (float)(A_1.RowBounds.Location.X + f0.RowHeadersWidth) - sizeF.Width - 4f, A_1.RowBounds.Location.Y + 4);
	}

	private void a7(object A_0, EventArgs A_1)
	{
		if (!ch.Enabled)
		{
			this.m_a3 = "script";
			e4.Filter = "Script File(*.script)|*.script";
			e4.InitialDirectory = this.m_bg;
			e4.FileName = f5.Text;
			e4.ShowDialog();
		}
	}

	private void k(string A_0)
	{
		if (f0.RowCount > 0)
		{
			for (int num = 0; num < this.m_p.Rows.Count; num++)
			{
				this.m_p.Rows[num]["count"] = "0";
			}
			this.m_p.WriteXml(A_0);
			f5.Text = A_0;
		}
		else
		{
			MessageBox.Show("No Data", "Error");
		}
	}

	private void a6(object A_0, EventArgs A_1)
	{
		a(0);
		f7.Filter = "Script File(*.script)|*.script;|All(*.*)|*.*";
		f7.InitialDirectory = this.m_bg;
		f7.FileName = "";
		f7.ShowDialog();
	}

	private void c(object A_0, CancelEventArgs A_1)
	{
		try
		{
			this.m_p.Clear();
			this.m_p.ReadXml(f7.FileName);
			f5.Text = f7.FileName;
			this.m_bg = Path.GetDirectoryName(f7.FileName);
			for (int num = 0; num < this.m_p.Rows.Count; num++)
			{
				this.m_p.Rows[num]["Index"] = num;
			}
		}
		catch (Exception ex)
		{
			MessageBox.Show("Open Failed: " + ex.Message, "Error");
		}
	}

	private void a5(object A_0, EventArgs A_1)
	{
		a(0);
		this.m_p.Clear();
		f5.Text = g("script_filename", "Script File Name");
	}

	private void a4(object A_0, EventArgs A_1)
	{
		if (b5 != null && b5.c())
		{
			if (ch.Enabled)
			{
				a(2);
			}
			else
			{
				a(1);
			}
		}
		else
		{
			MessageBox.Show(g("warning_openserialport", "Please Open COM at first"), g("text_error", "Error"));
		}
	}

	private void a3(object A_0, EventArgs A_1)
	{
		a(0);
	}

	private void a(object A_0, DataGridViewCellValidatingEventArgs A_1)
	{
		int columnIndex = A_1.ColumnIndex;
		int rowIndex = A_1.RowIndex;
		if (columnIndex < 0 || rowIndex < 0)
		{
			return;
		}
		if (f0.Columns[columnIndex].Name == "ParamValue")
		{
			string text = A_1.FormattedValue.ToString();
			if (text.Length == 0)
			{
				A_1.Cancel = true;
				MessageBox.Show(g("script_tips_inputemptyerror", "Empty input string was wrong."));
			}
			else if (text.Length != Encoding.Default.GetByteCount(text))
			{
				A_1.Cancel = true;
				MessageBox.Show(g("script_tips_inputwrongformat", "Input string was not in a correct format."));
			}
			else
			{
				A_1.Cancel = false;
			}
		}
		else
		{
			if (!(f0.Columns[columnIndex].Name == "Var"))
			{
				return;
			}
			DataRowView dataRowView = this.m_aw[rowIndex];
			if (dataRowView["ParamName"].ToString() == "SCRIPTGO")
			{
				int result = 0;
				if (!int.TryParse(A_1.FormattedValue.ToString(), out result))
				{
					A_1.Cancel = true;
					MessageBox.Show(g("script_tips_inputcorrectloops", "Please input a Positive Integer"));
				}
				else if (result <= 0)
				{
					A_1.Cancel = true;
					MessageBox.Show(g("script_tips_inputcorrectloops", "Please input a Positive Integer"));
				}
			}
			else if (dataRowView["Cmd"].ToString() == "?")
			{
				double result2 = 0.0;
				if (!double.TryParse(A_1.FormattedValue.ToString(), out result2))
				{
					A_1.Cancel = true;
					MessageBox.Show(g("script_tips_inputcorrectband", "Please input a number > 0"));
				}
				else if (result2 <= 0.0)
				{
					A_1.Cancel = true;
					MessageBox.Show(g("script_tips_inputcorrectband", "Please input a number > 0"));
				}
			}
		}
	}

	private void c(object A_0, DataGridViewCellEventArgs A_1)
	{
		int columnIndex = A_1.ColumnIndex;
		int rowIndex = A_1.RowIndex;
		if (columnIndex < 0 || rowIndex < 0)
		{
			return;
		}
		cu.IsBalloon = true;
		if (f0.Columns[columnIndex].Name == "ParamName")
		{
			DataRowView dataRowView = this.m_aw[rowIndex];
			this.m_ax.RowFilter = ("ParamName = '" + dataRowView["ModuleName"].ToString() + ":" + dataRowView["ParamName"].ToString() + "' and Addr = " + dataRowView["Addr"].ToString()) ?? "";
			if (this.m_ax.Count > 0 && this.m_ax[0]["ParamType"].ToString() != "NUM" && this.m_ax[0]["ParamType"].ToString() != "ERRORCODE")
			{
				DataView dataView = dg.b(this.m_ax[0]["Paramtype"].ToString(), Convert.ToInt32(dataRowView["Addr"]));
				string text = "";
				for (int num = 0; num < dataView.Count; num++)
				{
					text = text + dataView[num]["ParamValue"].ToString() + ": " + dataView[num]["ValueText"].ToString() + Environment.NewLine;
				}
				cu.Show(text, f0, f0.GetCellDisplayRectangle(columnIndex + 1, rowIndex, cutOverflow: true).X - 10, f0.GetCellDisplayRectangle(columnIndex + 1, rowIndex, cutOverflow: true).Y + f0.GetCellDisplayRectangle(columnIndex + 1, rowIndex, cutOverflow: true).Height - 10);
			}
		}
		else if (f0.Columns[columnIndex].Name == "Var")
		{
			DataRowView dataRowView2 = this.m_aw[rowIndex];
			string text2 = "";
			if (dataRowView2["ParamName"].ToString() == "SCRIPTGO")
			{
				text2 = g("script_tips_inputloops", "Input Needed Loops");
				f0[columnIndex, rowIndex].ReadOnly = false;
			}
			else if (dataRowView2["Cmd"].ToString() == "?" || dataRowView2["Cmd"].ToString() == "??" || dataRowView2["Cmd"].ToString() == "?=")
			{
				text2 = g("script_tips_inputband", "Input Needed Tolerance Band");
				f0[columnIndex, rowIndex].ReadOnly = false;
			}
			else
			{
				f0[columnIndex, rowIndex].ReadOnly = true;
			}
			cu.Show(text2, f0, f0.GetCellDisplayRectangle(columnIndex + 1, rowIndex, cutOverflow: true).X - 10, f0.GetCellDisplayRectangle(columnIndex + 1, rowIndex, cutOverflow: true).Y + f0.GetCellDisplayRectangle(columnIndex + 1, rowIndex, cutOverflow: true).Height - 10, 5000);
		}
		else if (f0.Columns[columnIndex].Name == "Cmd")
		{
			DataRowView dataRowView3 = this.m_aw[rowIndex];
			if (dataRowView3["ParamName"].ToString() == "SCRIPTGO")
			{
				f0[columnIndex, rowIndex].ReadOnly = true;
			}
			else if (dataRowView3["ParamName"].ToString() == "SCRIPTDELAY")
			{
				f0[columnIndex, rowIndex].ReadOnly = true;
			}
			else
			{
				f0[columnIndex, rowIndex].ReadOnly = false;
			}
		}
	}

	private void b(object A_0, DataGridViewCellEventArgs A_1)
	{
		cu.Hide(f0);
	}

	private void a(object A_0, DataGridViewCellEventArgs A_1)
	{
		if (f0.Columns[A_1.ColumnIndex] is DataGridViewComboBoxColumn && A_1.RowIndex != -1 && A_1.RowIndex != -1)
		{
			SendKeys.Send("{F4}");
		}
	}

	private void a(object A_0, DataGridViewCellCancelEventArgs A_1)
	{
		f0.ImeMode = ImeMode.OnHalf;
	}

	private string a(string A_0, string A_1, string A_2)
	{
		Form form = new Form();
		form.MinimizeBox = false;
		form.MaximizeBox = false;
		form.StartPosition = FormStartPosition.CenterScreen;
		form.Width = 300;
		form.Height = 150;
		form.Text = A_0;
		new Label
		{
			Text = A_1,
			Left = 25,
			Top = 20,
			Parent = form,
			AutoSize = true
		};
		TextBox textBox = new TextBox();
		textBox.Left = 60;
		textBox.Top = 50;
		textBox.Width = 180;
		textBox.Text = A_2;
		textBox.Parent = form;
		Button button = new Button();
		button.Left = 40;
		button.Top = 80;
		button.Parent = form;
		button.Text = "确定";
		form.AcceptButton = button;
		button.DialogResult = DialogResult.OK;
		new Button
		{
			Left = 160,
			Top = 80,
			Parent = form,
			Text = "取消",
			DialogResult = DialogResult.Cancel
		};
		try
		{
			if (form.ShowDialog() == DialogResult.OK)
			{
				return textBox.Text;
			}
			return string.Empty;
		}
		finally
		{
			form.Dispose();
		}
	}

	private void a(int A_0)
	{
		switch (A_0)
		{
		case 0:
			ch.Enabled = false;
			ci = 0;
			f2.Text = g("script_btn_start", "Start");
			f2.BackColor = SystemColors.Control;
			break;
		case 1:
			if (ci == 0)
			{
				for (int num = 0; num < this.m_p.Rows.Count; num++)
				{
					this.m_p.Rows[num]["count"] = "0";
				}
			}
			ck = Math.Pow(10.0, 14.0);
			f2.Text = g("script_btn_pause", "Pause");
			f2.BackColor = Color.GreenYellow;
			ch.Enabled = true;
			break;
		default:
			ch.Enabled = false;
			ck = Math.Pow(10.0, 14.0);
			f2.Text = g("script_btn_continue", "Continue");
			f2.BackColor = Color.GreenYellow;
			break;
		}
	}

	private void a2(object A_0, EventArgs A_1)
	{
		if (d4.SelectedRows.Count > 0)
		{
			if (MessageBox.Show("确定要删除选定的记录吗？", "确认删除", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
			{
				return;
			}
			List<int> list = new List<int>();
			foreach (DataGridViewRow selectedRow in d4.SelectedRows)
			{
				if (!selectedRow.IsNewRow)
				{
					int index = selectedRow.Index;
					int num = d4.RowCount - index - 1;
					if (num >= 0 && num < this.m_j.Rows.Count)
					{
						int item = Convert.ToInt32(this.m_j.Rows[num]["DataIndex"]);
						list.Add(item);
					}
				}
			}
			d4.SuspendLayout();
			d4.VirtualMode = false;
			foreach (int item2 in list)
			{
				DataRow[] array = this.m_j.Select("DataIndex = " + item2);
				if (array.Length != 0)
				{
					this.m_j.Rows.Remove(array[0]);
				}
			}
			d4.VirtualMode = true;
			d4.RowCount = this.m_j.Rows.Count;
			d4.ResumeLayout();
		}
		else
		{
			MessageBox.Show("请先选择要删除的记录", "提示", MessageBoxButtons.OK, MessageBoxIcon.Asterisk);
		}
	}

	private void a1(object A_0, EventArgs A_1)
	{
		d4.RowCount = 0;
		this.m_j.Clear();
	}

	private void a0(object A_0, EventArgs A_1)
	{
		cb = Convert.ToDouble(ea.Value);
		try
		{
			XDocument xDocument = XDocument.Load(this.m_be);
			xDocument.Element("EasyHost").Element("RecordInterval").Attribute("value")
				.SetValue(ea.Value);
			xDocument.Save(this.m_be);
		}
		catch (Exception ex)
		{
			MessageBox.Show("Save Failed: " + ex.Message, "Error");
		}
	}

	private void az(object A_0, EventArgs A_1)
	{
		this.m_a3 = "datarecord";
		e4.Filter = "Text Document(*.txt)|*.txt";
		e4.InitialDirectory = this.m_bh;
		e4.FileName = "";
		e4.ShowDialog();
	}

	private void j(string A_0)
	{
		a(this.m_j, A_0);
	}

	private void a(object A_0, MaskInputRejectedEventArgs A_1)
	{
		MessageBox.Show(g("warning_onlynumisvalid", "Please Input Positive Integer") + "\r 0 - 9999999", g("text_error", "ERROR"));
	}

	private void ay(object A_0, EventArgs A_1)
	{
		if (ek.Checked)
		{
			bq = DateTime.Now.ToString("yyyyMMddHHmmss") + ".txt";
			string text = bt + "\\" + bq;
			try
			{
				if (File.Exists(text))
				{
					MessageBox.Show(g("warning_fileexist", "File is already existing") + ": " + text, g("text_error", "ERROR"));
					em.Text = "*.txt";
					ek.Checked = false;
					return;
				}
				em.Text = bq;
				br = File.CreateText(text);
				br.AutoFlush = true;
				foreach (DataColumn column in this.m_j.Columns)
				{
					bs.Append(column.Caption.ToString() + "\t");
				}
				br.WriteLine(bs.ToString() + "\r\n");
				bs.Clear();
				for (int num = 0; num < d5.Columns.Count; num++)
				{
					d5.Columns[num].SortMode = DataGridViewColumnSortMode.NotSortable;
				}
				bu = true;
				return;
			}
			catch (Exception ex)
			{
				MessageBox.Show("Save Failed: " + ex.Message.ToString(), "Error");
				return;
			}
		}
		bu = false;
		try
		{
			br.Close();
			br.Dispose();
		}
		catch (Exception ex2)
		{
			MessageBox.Show(ex2.Message);
		}
	}

	private void ax(object A_0, EventArgs A_1)
	{
		if (gd.SelectedIndex == 0)
		{
			ec.Enabled = true;
		}
		else if (gd.SelectedIndex == 1)
		{
			ec.Enabled = false;
		}
		else if (gd.SelectedIndex == 2)
		{
			ec.Enabled = false;
		}
	}

	public static bool a(DataTable A_0, string A_1)
	{
		try
		{
			StreamWriter streamWriter = File.CreateText(A_1);
			StringBuilder stringBuilder = new StringBuilder();
			foreach (DataColumn column in A_0.Columns)
			{
				stringBuilder.Append(column.Caption.ToString() + "\t");
			}
			streamWriter.WriteLine(stringBuilder.ToString());
			stringBuilder.Clear();
			foreach (DataRow row in A_0.Rows)
			{
				stringBuilder.Append(row[0].ToString() + "\t");
				stringBuilder.Append(Convert.ToDateTime(row[1]).ToString("yyyy-MM-dd HH:mm:ss.fff") + "\t");
				for (int num = 2; num < A_0.Columns.Count; num++)
				{
					stringBuilder.Append(row[num].ToString() + "\t");
				}
				streamWriter.WriteLine(stringBuilder.ToString());
				stringBuilder.Clear();
			}
			streamWriter.Close();
			return true;
		}
		catch (Exception ex)
		{
			MessageBox.Show("Save Failed: " + ex.Message.ToString(), "Error");
			return false;
		}
	}

	private void aw(object A_0, EventArgs A_1)
	{
		if (c2 != 0)
		{
			int num = dq.Location.Y;
			int num2 = dq.Location.X;
			if (num2 + e6.Width + 6 > base.Width)
			{
				num2 = base.Width - e6.Width - 6;
			}
			e6.Location = new Point(num2, num);
			e6.Visible = true;
			gt.Hide(dq);
			try
			{
				this.m_ay.RowFilter = "addr = " + a2();
			}
			catch
			{
			}
		}
	}

	private void av(object A_0, EventArgs A_1)
	{
		e6.Visible = false;
	}

	public void av()
	{
		int num = 0;
		int num2 = 0;
		int num3 = 0;
		int num4 = 0;
		for (int num5 = 0; num5 < this.m_h.Rows.Count; num5++)
		{
			if (this.m_h.Rows[num5]["model"].ToString() + "@" + this.m_h.Rows[num5]["Addr"].ToString() == a8())
			{
				num = ((this.m_h.Rows[num5]["ledsw"] != DBNull.Value) ? Convert.ToInt32(this.m_h.Rows[num5]["ledsw"]) : 0);
				num2 = ((this.m_h.Rows[num5]["lederr"] != DBNull.Value) ? Convert.ToInt32(this.m_h.Rows[num5]["lederr"]) : 0);
				num4 = ((this.m_h.Rows[num5]["constatus"] != DBNull.Value) ? Convert.ToInt32(this.m_h.Rows[num5]["constatus"]) : 0);
			}
		}
		if (num2 != 0 && c2 == 0)
		{
			gt.IsBalloon = false;
			gt.UseFading = false;
			gt.Show(g("led_tooltip_err", "Error! Click Me!"), dq);
		}
		c2 = num2;
		c1 = num;
		c3 = num4 switch
		{
			0 => 0, 
			3 => 1, 
			_ => 2, 
		};
	}

	public void aw()
	{
		int millisecond = DateTime.Now.Millisecond;
		if (c1 == 1)
		{
			dp.Image = cy;
		}
		else if (c1 == 2)
		{
			if (millisecond % (1000 / c0) >= 1000 / c0 / 2)
			{
				dp.Image = cy;
			}
			else
			{
				dp.Image = cw;
			}
		}
		else
		{
			dp.Image = cw;
		}
		if (c2 == 1)
		{
			dq.Image = cx;
		}
		else if (c2 == 2)
		{
			if (millisecond % (1000 / c0) >= 1000 / c0 / 2)
			{
				dq.Image = cx;
			}
			else
			{
				dq.Image = cw;
			}
		}
		else
		{
			dq.Image = cw;
		}
		if (c3 == 1)
		{
			dr.Image = cy;
		}
		else if (c3 == 2)
		{
			if (millisecond % (1000 / c0) >= 1000 / c0 / 2)
			{
				dr.Image = cx;
			}
			else
			{
				dr.Image = cw;
			}
		}
		else
		{
			dr.Image = cw;
		}
	}

	public void ax()
	{
		if (!bz)
		{
			return;
		}
		bv = DateTime.Now.ToString("yyyy-MM-dd-HH-mm-ss") + ".log";
		string text = this.m_bc + bw + "\\" + bv;
		try
		{
			if (File.Exists(text))
			{
				MessageBox.Show(g("warning_fileexist", "File is already existing") + ": " + text, g("text_error", "Error"));
				return;
			}
			bx = File.CreateText(text);
			bx.AutoFlush = true;
			bx.WriteLine(by.ToString() + "\r\n");
			by.Clear();
		}
		catch (Exception ex)
		{
			MessageBox.Show(g("text_failed", "Failed") + ": " + ex.Message.ToString(), g("text_error", "Eorror"));
		}
	}

	public void ay()
	{
		try
		{
			bx.Close();
			bx.Dispose();
		}
		catch
		{
		}
	}

	public void p(string A_0)
	{
		BeginInvoke((MethodInvoker)delegate
		{
			ef.Text = "Log: " + A_0 + " (" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ")";
			b0 = DateTime.Now;
			if (bx != null && bz)
			{
				by.Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ": " + A_0);
				bx.WriteLine(by.ToString());
				by.Clear();
			}
		});
	}

	public void a(string A_0, Color A_1)
	{
		if (!d3.IsHandleCreated)
		{
			return;
		}
		d3.BeginInvoke((MethodInvoker)delegate
		{
			ef.Text = A_0 + "(" + DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ")";
			ef.ForeColor = A_1;
			b0 = DateTime.Now;
			if (bx != null && bz)
			{
				by.Append(DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ": " + A_0);
				bx.WriteLine(by.ToString());
				by.Clear();
			}
		});
	}

	public void b(string A_0, Color A_1)
	{
		if (!fy.IsHandleCreated)
		{
			return;
		}
		fy.BeginInvoke((MethodInvoker)delegate
		{
			string value = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff") + ": " + A_0 + "\n";
			fy.SelectionStart = fy.TextLength;
			fy.SelectionColor = A_1;
			fy.AppendText(value);
			string[] lines = fy.Lines;
			if (lines.Length > 500)
			{
				string[] array = new string[100];
				Array.Copy(lines, lines.Length - 100, array, 0, 100);
				fy.Clear();
				for (int num = 0; num < array.Length; num++)
				{
					fy.AppendText(array[num] + "\n");
				}
			}
			fy.ScrollToCaret();
			if (bx != null && bz)
			{
				by.Append(value);
				bx.WriteLine(value);
				by.Clear();
			}
		});
	}

	public void c(string A_0, Color A_1)
	{
	}

	public void az()
	{
		if ((DateTime.Now - b0).Seconds > 30)
		{
			a(g("status_text_help", "Tips") + ": " + this.m_o.Rows[b1]["HelpText"].ToString(), Color.Black);
			b1++;
			if (b1 >= this.m_o.Rows.Count)
			{
				b1 = 0;
			}
		}
	}

	private void au(object A_0, EventArgs A_1)
	{
		b0 = DateTime.Now;
	}

	public void a0()
	{
		this.m_ao.RowFilter = "";
		DataView defaultView = this.m_ao.ToTable(true, "Addr", "Model").DefaultView;
		defaultView.Sort = "Addr ASC";
		this.m_h.Clear();
		for (int num = 0; num < defaultView.Count; num++)
		{
			DataRowView dataRowView = defaultView[num];
			DataRow dataRow = this.m_h.NewRow();
			dataRow["Addr"] = Convert.ToInt32(dataRowView["Addr"]);
			dataRow["model"] = Convert.ToString(dataRowView["Model"]);
			dataRow["constatus"] = 0;
			dataRow["tabpagename"] = Convert.ToString(dataRowView["Model"]) + "@" + Convert.ToString(dataRowView["Addr"]);
			dataRow["lastcomtime"] = DateTime.Now;
			dataRow["ledsw"] = 0;
			dataRow["lederr"] = 0;
			this.m_h.Rows.Add(dataRow);
		}
	}

	public void a1()
	{
		hp.SmallImageList = de;
		int count = hp.Items.Count;
		DataView dataView = new DataView(this.m_h);
		dataView.Sort = "Addr ASC";
		for (int num = 0; num < dataView.Count; num++)
		{
			ListViewItem listViewItem = new ListViewItem();
			listViewItem.ImageIndex = 0;
			listViewItem.Text = dataView[num]["tabpagename"].ToString();
			listViewItem.Name = dataView[num]["tabpagename"].ToString();
			hp.Items.Add(listViewItem);
		}
		for (int num2 = 0; num2 < count; num2++)
		{
			hp.Items.RemoveAt(0);
		}
		if (hp.SelectedItems.Count == 0 && hp.Items.Count > 0)
		{
			hp.Items[0].Selected = true;
		}
	}

	public int a2()
	{
		int result = 0;
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToString(this.m_h.Rows[num]["tabpagename"]) == a8())
			{
				return Convert.ToInt32(this.m_h.Rows[num]["Addr"]);
			}
		}
		return result;
	}

	public string f(int A_0)
	{
		string result = "";
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToInt32(this.m_h.Rows[num]["Addr"]) == A_0)
			{
				return Convert.ToString(this.m_h.Rows[num]["model"]);
			}
		}
		return result;
	}

	public int g(int A_0)
	{
		int c2 = 0;
		Invoke((MethodInvoker)delegate
		{
			int num = (int)(ca * (double)this.m_at.Count * 3.0);
			if (num >= cc)
			{
				cc = num;
			}
			for (int num2 = 0; num2 < this.m_h.Rows.Count; num2++)
			{
				if (Convert.ToInt32(this.m_h.Rows[num2]["Addr"]) == A_0)
				{
					try
					{
						if (new TimeSpan(DateTime.Now.Ticks - Convert.ToDateTime(this.m_h.Rows[num2]["lastcomtime"]).Ticks).TotalSeconds > (double)cc)
						{
							this.m_h.Rows[num2]["constatus"] = 1;
							this.m_h.Rows[num2]["ledsw"] = 0;
							this.m_h.Rows[num2]["lederr"] = 0;
						}
						c2 = Convert.ToInt32(this.m_h.Rows[num2]["constatus"]);
					}
					catch
					{
					}
				}
			}
		});
		return c2;
	}

	public void d(string A_0, int A_1)
	{
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToInt32(this.m_h.Rows[num]["Addr"]) != A_1)
			{
				continue;
			}
			if (A_0 == Convert.ToString(this.m_h.Rows[num]["model"]))
			{
				this.m_h.Rows[num]["constatus"] = 3;
				this.m_bp = false;
				break;
			}
			this.m_h.Rows[num]["constatus"] = 2;
			if (this.m_h.Rows.Count == 1 && A_1 == 0 && !this.m_bp)
			{
				this.m_bp = true;
				BeginInvoke((MethodInvoker)delegate
				{
					if (MessageBox.Show(this, g("device_autoreload", "Wrong model file. Auto reload the correct model file?"), g("text_confirm", "Confirm"), MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes && this.m_g.Rows.Count > 0)
					{
						h(255);
					}
				});
			}
			a(g("warning_wrongmodelfile", "Wrong Model File") + " @ " + A_1, Color.Red);
			break;
		}
	}

	public void a(DateTime A_0, int A_1)
	{
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToInt32(this.m_h.Rows[num]["Addr"]) == A_1)
			{
				this.m_h.Rows[num]["lastcomtime"] = A_0;
				break;
			}
		}
	}

	public string q(string A_0)
	{
		string result = "No info!";
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToString(this.m_h.Rows[num]["tabpagename"]) == A_0)
			{
				switch (Convert.ToInt32(this.m_h.Rows[num]["constatus"]))
				{
				case 0:
					result = g("status_text_constatus_close", "COM is closed.");
					break;
				case 1:
					result = g("status_text_constatus_noresponse", "COM is open, but no response.");
					break;
				case 2:
					result = g("status_text_constatus_wrongmodel", "COM is open, but wrong model file.");
					break;
				case 3:
					result = g("status_text_constatus_right", "COM is open. Communication correctly.");
					break;
				}
			}
		}
		return result;
	}

	public void a(int A_0, int A_1, int A_2)
	{
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToInt32(this.m_h.Rows[num]["Addr"]) == A_2)
			{
				this.m_h.Rows[num]["ledsw"] = A_0;
				this.m_h.Rows[num]["lederr"] = A_1;
				break;
			}
		}
	}

	public void e(int A_0, int A_1)
	{
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToInt32(this.m_h.Rows[num]["Addr"]) == A_1)
			{
				this.m_h.Rows[num]["lederr"] = A_0;
				break;
			}
		}
	}

	public void f(int A_0, int A_1)
	{
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToInt32(this.m_h.Rows[num]["Addr"]) == A_1)
			{
				this.m_h.Rows[num]["ledsw"] = A_0;
				break;
			}
		}
	}

	public void a3()
	{
		Invoke((MethodInvoker)delegate
		{
			for (int num = 0; num < this.m_h.Rows.Count; num++)
			{
				this.m_h.Rows[num]["ledsw"] = 0;
				this.m_h.Rows[num]["lederr"] = 0;
				this.m_h.Rows[num]["constatus"] = 0;
			}
		});
	}

	public void a4()
	{
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			int a_ = Convert.ToInt32(this.m_h.Rows[num]["constatus"]) switch
			{
				3 => 1, 
				0 => 0, 
				_ => 2, 
			};
			int a_2 = Convert.ToInt32(this.m_h.Rows[num]["ledsw"]);
			int a_3 = Convert.ToInt32(this.m_h.Rows[num]["lederr"]);
			int num2 = b(a_2, a_3, a_);
			try
			{
				if (hp.Items[num].ImageIndex != num2)
				{
					hp.Items[num].ImageIndex = num2;
				}
			}
			catch
			{
			}
		}
	}

	public int b(int A_0, int A_1, int A_2)
	{
		if (DateTime.Now.Millisecond % (1000 / c0) >= 1000 / c0 / 2)
		{
			if (A_0 == 2)
			{
				A_0 = 1;
			}
			if (A_1 == 2)
			{
				A_1 = 1;
			}
			if (A_0 == 0 && A_1 == 0 && A_2 == 2)
			{
				A_2 = 8;
			}
			else if (A_2 == 2)
			{
				A_2 = 1;
			}
		}
		else
		{
			if (A_0 == 2)
			{
				A_0 = 0;
			}
			if (A_1 == 2)
			{
				A_1 = 0;
			}
			if (A_2 == 2)
			{
				A_2 = 0;
			}
		}
		return A_0 * 4 + A_1 * 2 + A_2;
	}

	private void at(object A_0, EventArgs A_1)
	{
		Process.Start("http://shop69907694.taobao.com");
	}

	private void @as(object A_0, EventArgs A_1)
	{
		MessageBox.Show(this.m_a7 + "\r" + this.m_a8 + "(Build: " + File.GetLastWriteTime(GetType().Assembly.Location).ToString() + ") \rhttp://www.yexian.com", "About");
	}

	private void ar(object A_0, EventArgs A_1)
	{
		Process.Start("http://www.yexian.com");
	}

	private void aq(object A_0, EventArgs A_1)
	{
		Process.Start(this.m_bc + "Help.pdf");
	}

	private void ap(object A_0, EventArgs A_1)
	{
		string[] array = x();
		if (array == null)
		{
			return;
		}
		try
		{
			ca = Convert.ToDouble(array[0]);
			XDocument xDocument = XDocument.Load(this.m_be);
			xDocument.Element("EasyHost").Element("SendInterval").Attribute("value")
				.SetValue(ca);
			xDocument.Save(this.m_be);
		}
		catch (Exception ex)
		{
			p("Error when saving interval setting," + ex.Message);
		}
		try
		{
			cn = Convert.ToInt32(array[1]);
			XDocument xDocument2 = XDocument.Load(this.m_be);
			xDocument2.Element("EasyHost").Element("DefaultAddr").Attribute("value")
				.SetValue(cn);
			xDocument2.Save(this.m_be);
		}
		catch (Exception ex2)
		{
			p("Error when saving default address setting, " + ex2.Message);
		}
		try
		{
			df = Convert.ToInt32(array[2]);
			XDocument xDocument3 = XDocument.Load(this.m_be);
			xDocument3.Element("EasyHost").Element("StepRate").Attribute("value")
				.SetValue(df);
			xDocument3.Save(this.m_be);
		}
		catch (Exception ex3)
		{
			MessageBox.Show(g("text_failed", "Failed") + ": " + ex3.Message, g("text_error", "Error"));
		}
		if (bz != Convert.ToBoolean(array[3]))
		{
			if (!bz)
			{
				bz = true;
				ax();
			}
			else
			{
				bz = false;
			}
			try
			{
				XDocument xDocument4 = XDocument.Load(this.m_be);
				xDocument4.Element("EasyHost").Element("LogFileFlag").Attribute("value")
					.SetValue(bz);
				xDocument4.Save(this.m_be);
			}
			catch (Exception ex4)
			{
				MessageBox.Show("Save Failed: " + ex4.Message, "错误");
			}
		}
		try
		{
			this.m_bb = Convert.ToBoolean(array[4]);
			XDocument xDocument5 = XDocument.Load(this.m_be);
			xDocument5.Element("EasyHost").Element("AutoLoadFlag").Attribute("value")
				.SetValue(this.m_bb);
			xDocument5.Save(this.m_be);
		}
		catch (Exception ex5)
		{
			MessageBox.Show("Save Failed: " + ex5.Message, "错误");
		}
		try
		{
			b2 = Convert.ToBoolean(array[5]);
			XDocument xDocument6 = XDocument.Load(this.m_be);
			xDocument6.Element("EasyHost").Element("AutoCheckFlag").Attribute("value")
				.SetValue(b2);
			xDocument6.Save(this.m_be);
		}
		catch (Exception ex6)
		{
			MessageBox.Show("Save Failed: " + ex6.Message, "错误");
		}
		try
		{
			b3 = Convert.ToBoolean(array[6]);
			XDocument xDocument7 = XDocument.Load(this.m_be);
			if (xDocument7.Element("EasyHost").Element("FileTransferFlag") == null)
			{
				xDocument7.Element("EasyHost").Add(new XElement("FileTransferFlag", new XAttribute("value", b3)));
			}
			else
			{
				xDocument7.Element("EasyHost").Element("FileTransferFlag").Attribute("value")
					.SetValue(b3);
			}
			xDocument7.Save(this.m_be);
			if (b3)
			{
				if (!d0.TabPages.Contains(fk))
				{
					d0.TabPages.Add(fk);
				}
			}
			else if (d0.TabPages.Contains(fk))
			{
				d0.TabPages.Remove(fk);
			}
		}
		catch (Exception ex7)
		{
			MessageBox.Show("Save Failed: " + ex7.Message, "错误");
		}
	}

	private string[] x()
	{
		Form form = new Form();
		form.MinimizeBox = false;
		form.MaximizeBox = false;
		form.StartPosition = FormStartPosition.CenterScreen;
		form.Width = 300;
		form.Height = 300;
		form.Text = g("option_menu", "Option");
		new Label
		{
			Left = 20,
			Top = 10,
			AutoSize = true,
			Parent = form,
			Text = g("option_menu_comInterval", "Interval")
		};
		NumericUpDown numericUpDown = new NumericUpDown();
		numericUpDown.Left = 200;
		numericUpDown.Top = 5;
		numericUpDown.Width = 50;
		numericUpDown.Parent = form;
		numericUpDown.DecimalPlaces = 3;
		numericUpDown.Minimum = 0.01m;
		numericUpDown.Maximum = 1000m;
		numericUpDown.Increment = 0.01m;
		numericUpDown.Value = Convert.ToDecimal(ca);
		new Label
		{
			Left = 20,
			Top = 40,
			AutoSize = true,
			Parent = form,
			Text = g("option_menu_defaultaddr", "Default Addr")
		};
		NumericUpDown numericUpDown2 = new NumericUpDown();
		numericUpDown2.Left = 200;
		numericUpDown2.Top = 35;
		numericUpDown2.Width = 50;
		numericUpDown2.Parent = form;
		numericUpDown2.Minimum = 0m;
		numericUpDown2.Maximum = 255m;
		numericUpDown2.Increment = 1m;
		numericUpDown2.Value = Convert.ToDecimal(cn);
		new Label
		{
			Left = 20,
			Top = 70,
			AutoSize = true,
			Parent = form,
			Text = g("option_menu_steprate", "Step")
		};
		NumericUpDown numericUpDown3 = new NumericUpDown();
		numericUpDown3.Left = 200;
		numericUpDown3.Top = 65;
		numericUpDown3.Width = 50;
		numericUpDown3.Parent = form;
		numericUpDown3.Minimum = 1m;
		numericUpDown3.Maximum = 255m;
		numericUpDown3.Increment = 1m;
		numericUpDown3.Value = Convert.ToDecimal(df);
		CheckBox checkBox = new CheckBox();
		checkBox.Left = 20;
		checkBox.Top = 100;
		checkBox.AutoSize = true;
		checkBox.Parent = form;
		checkBox.Text = g("option_menu_autoSaveLog", "Autosave Log");
		checkBox.Checked = bz;
		CheckBox checkBox2 = new CheckBox();
		checkBox2.Left = 20;
		checkBox2.Top = 130;
		checkBox2.AutoSize = true;
		checkBox2.Parent = form;
		checkBox2.Text = g("option_menu_autoLoadProject", "Auto Load Project");
		checkBox2.Checked = this.m_bb;
		CheckBox checkBox3 = new CheckBox();
		checkBox3.Left = 20;
		checkBox3.Top = 160;
		checkBox3.AutoSize = true;
		checkBox3.Parent = form;
		checkBox3.Text = g("option_menu_autoCheck", "Auto Check");
		checkBox3.Checked = b2;
		CheckBox checkBox4 = new CheckBox();
		checkBox4.Left = 20;
		checkBox4.Top = 190;
		checkBox4.AutoSize = true;
		checkBox4.Parent = form;
		checkBox4.Text = g("option_menu_fileTransfer", "Support File Transfer");
		checkBox4.Checked = b3;
		Button button = new Button();
		button.Left = 120;
		button.Top = 220;
		button.Parent = form;
		button.Text = g("text_ok", "OK");
		form.AcceptButton = button;
		button.DialogResult = DialogResult.OK;
		new Button
		{
			Left = 200,
			Top = 220,
			Parent = form,
			Text = g("text_cancel", "Cancel"),
			DialogResult = DialogResult.Cancel
		};
		try
		{
			if (form.ShowDialog() == DialogResult.OK)
			{
				return new string[7]
				{
					numericUpDown.Text,
					numericUpDown2.Text,
					numericUpDown3.Text,
					checkBox.Checked.ToString(),
					checkBox2.Checked.ToString(),
					checkBox3.Checked.ToString(),
					checkBox4.Checked.ToString()
				};
			}
			return null;
		}
		finally
		{
			form.Dispose();
		}
	}

	private void ao(object A_0, EventArgs A_1)
	{
		this.m_a3 = "project";
		e4.Filter = "XML Document(*.xml)|*.xml";
		e4.InitialDirectory = this.m_bf;
		e4.FileName = this.m_a4;
		e4.ShowDialog();
	}

	private void i(string A_0)
	{
		this.m_g.WriteXml(A_0);
		this.m_a4 = Path.GetFileName(A_0);
		this.m_a6 = true;
		a5();
	}

	private void b(object A_0, CancelEventArgs A_1)
	{
		try
		{
			string directoryName = Path.GetDirectoryName(e4.FileName);
			if (this.m_a3 == "project")
			{
				i(e4.FileName);
				this.m_bf = directoryName;
			}
			else if (this.m_a3 == "datarecord")
			{
				j(e4.FileName);
				this.m_bh = directoryName;
			}
			else if (this.m_a3 == "script")
			{
				k(e4.FileName);
				this.m_bg = directoryName;
			}
			else if (this.m_a3 == "cmdlist")
			{
				g(e4.FileName);
				this.m_bh = directoryName;
			}
			else if (this.m_a3 == "cmd2script")
			{
				f(e4.FileName);
				this.m_bg = directoryName;
			}
			else if (this.m_a3 == "savedvaluelist")
			{
				e(e4.FileName);
				this.m_bh = directoryName;
			}
		}
		catch (Exception ex)
		{
			MessageBox.Show(g("text_failed", "Failed") + ": " + ex.Message, g("text_error", "Error"));
		}
	}

	private void an(object A_0, EventArgs A_1)
	{
		if (!this.m_a6)
		{
			MessageBoxButtons buttons = MessageBoxButtons.OKCancel;
			if (MessageBox.Show(g("project_text_notSaved", "Project is not saved!"), g("text_continue", "Continue") + "?", buttons) == DialogResult.Cancel)
			{
				return;
			}
		}
		try
		{
			h(255);
		}
		catch
		{
		}
	}

	private void am(object A_0, EventArgs A_1)
	{
		if (!this.m_a6)
		{
			MessageBoxButtons buttons = MessageBoxButtons.OKCancel;
			if (MessageBox.Show(g("project_text_notSavedWarning", "Project is not saved! Current project will be lost! Continue?"), g("text_continue", "Continue") + "?", buttons) == DialogResult.Cancel)
			{
				return;
			}
		}
		try
		{
			e5.Filter = "XML Document(*.xml)|*.xml";
			e5.InitialDirectory = this.m_bf;
			e5.FileName = "";
			e5.ShowDialog();
		}
		catch
		{
		}
	}

	private void a(object A_0, CancelEventArgs A_1)
	{
		try
		{
			a(e5.FileName, A_1: true, 0);
			this.m_bf = Path.GetDirectoryName(e5.FileName);
		}
		catch (Exception ex)
		{
			MessageBox.Show(g("text_failed", "Failed") + ": " + ex.Message, g("text_error", "Error"));
		}
	}

	private void al(object A_0, EventArgs A_1)
	{
		if (!this.m_a6)
		{
			MessageBoxButtons buttons = MessageBoxButtons.OKCancel;
			if (MessageBox.Show(g("project_text_notSavedWarning", "Project is not saved! Current project will be lost! Continue?"), g("text_continue", "Continue") + "?", buttons) == DialogResult.Cancel)
			{
				return;
			}
		}
		global::i i2 = new global::i(g("project_text_new", "Creat a new Project"), g("modelfile_selectmodel", "Model"), g("modelfile_selectversion", "Version"), g("modelfile_selectaddr", "Address"), "0", di);
		if (i2.ShowDialog() == DialogResult.OK)
		{
			this.m_g.Clear();
			a(this.m_bc + this.m_bd + this.m_bi + i2.b() + ".xml", A_1: false, Convert.ToInt32(i2.c()));
			this.m_a4 = this.m_a9 + this.m_ba + ".xml";
			this.m_ba++;
			this.m_a6 = false;
			a5();
		}
	}

	public void a5()
	{
		if (!this.m_a6)
		{
			Text = "*" + this.m_a4 + " -" + this.m_a7 + " " + this.m_a8;
		}
		else
		{
			Text = this.m_a4 + " -" + this.m_a7 + " " + this.m_a8;
		}
	}

	public bool r(string A_0)
	{
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			if (Convert.ToString(this.m_h.Rows[num]["Addr"]) == A_0)
			{
				return true;
			}
		}
		return false;
	}

	public DataRow a(string A_0, string A_1, string A_2, string A_3, string A_4, string A_5, DataTable A_6)
	{
		DataRow dataRow = A_6.NewRow();
		dataRow["Model"] = A_0;
		dataRow["ParamName"] = A_1;
		dataRow["ParamText"] = A_2;
		dataRow["ParamValue"] = "";
		dataRow["ValueText"] = "";
		dataRow["ParamType"] = "NUM";
		dataRow["Unit"] = "";
		dataRow["Page"] = 255;
		dataRow["PageName"] = A_3;
		dataRow["FolderName"] = A_4;
		dataRow["ParamLevel"] = 1;
		dataRow["ToolTips"] = A_5;
		dataRow["IsButton"] = false;
		return dataRow;
	}

	public void a6()
	{
		ee.Invoke((MethodInvoker)delegate
		{
			ee.Series.Clear();
			a7();
			int num = 0;
			for (int num2 = 0; num2 < Math.Min(this.m_aq.Count, this.m_j.Columns.Count - 2); num2++)
			{
				if (Convert.ToInt32(this.m_aq[num2].Row["DataLevel"]) == 3 && d4.Columns[num2 + 2].DataPropertyName != c9 && (Convert.ToInt32(this.m_aq[num2].Row["Addr"]) == a2() || !gh.Checked))
				{
					Series series = new Series
					{
						ChartType = SeriesChartType.Line,
						XValueType = da
					};
					try
					{
						if (num < c4.Length)
						{
							series.Color = c4[num];
						}
						else if (num < 2 * c4.Length)
						{
							series.Color = c4[num - c4.Length];
							series.BorderDashStyle = ChartDashStyle.Dot;
						}
						series.BorderWidth = dc;
						series.LegendText = this.m_aq[num2].Row["FolderName"].ToString() + ": " + this.m_aq[num2].Row["ParamText"].ToString() + " @" + this.m_aq[num2].Row["Addr"].ToString();
						series.Points.DataBind(this.m_ar, c9, d4.Columns[num2 + 2].DataPropertyName, "");
						ee.Series.Add(series);
						num++;
					}
					catch (Exception)
					{
					}
				}
			}
		});
	}

	public void a7()
	{
		if (da == ChartValueType.DateTime)
		{
			if (c7 == DateTimeIntervalType.Auto)
			{
				int num = base.Width / 2 / dc;
				if (num <= 0)
				{
					num = 1;
				}
				int num2 = Convert.ToInt32(Math.Ceiling(Convert.ToDouble(this.m_j.Rows.Count) / (double)num));
				if (num2 <= 0)
				{
					num2 = 1;
				}
				this.m_ar.RowFilter = "DataIndex%" + num2 + "=0";
				return;
			}
			DateTime dateTime = DateTime.FromOADate(c5);
			DateTime dateTime2 = DateTime.FromOADate(c5 + c6);
			this.m_as.RowFilter = string.Concat("RecordTime >= '", dateTime, "' and RecordTime <= '", dateTime2, "'");
			int num3 = base.Width / 4 / dc;
			if (num3 <= 0)
			{
				num3 = 1;
			}
			int num4 = Convert.ToInt32(Math.Ceiling(Convert.ToDouble(this.m_as.Count) / (double)num3));
			int num5 = Convert.ToInt32(Math.Ceiling(Convert.ToDouble(this.m_j.Rows.Count) / (double)num3 * 2.0));
			if (num5 <= 0)
			{
				num5 = 1;
			}
			this.m_ar.RowFilter = string.Concat("RecordTime >= '", dateTime, "' and RecordTime <= '", dateTime2, "' and DataIndex%", num4, "=0 or DataIndex%", num5, "=0");
		}
		else
		{
			if (da != ChartValueType.Double)
			{
				return;
			}
			if (c7 == DateTimeIntervalType.Auto)
			{
				int num6 = base.Width / 2 / dc;
				if (num6 <= 0)
				{
					num6 = 1;
				}
				int num7 = Convert.ToInt32(Math.Ceiling(Convert.ToDouble(this.m_j.Rows.Count) / (double)num6));
				if (num7 <= 0)
				{
					num7 = 1;
				}
				this.m_ar.RowFilter = "DataIndex%" + num7 + "=0";
			}
			else
			{
				double num8 = c5;
				double num9 = c5 + c6;
				this.m_as.RowFilter = "'" + c9 + "' >= '" + num8 + "' and '" + c9 + "' <= '" + num9 + "'";
				int num10 = base.Width / 4 / dc;
				if (num10 <= 0)
				{
					num10 = 1;
				}
				int num11 = Convert.ToInt32(Math.Ceiling(Convert.ToDouble(this.m_as.Count) / (double)num10));
				int num12 = Convert.ToInt32(Math.Ceiling(Convert.ToDouble(this.m_j.Rows.Count) / (double)num10 * 2.0));
				if (num12 <= 0)
				{
					num12 = 1;
				}
				this.m_ar.RowFilter = "'" + c9 + "' >= '" + num8 + "' and '" + c9 + "' <= '" + num9 + "' and DataIndex%" + num11 + "=0 or DataIndex%" + num12 + "=0";
			}
			double num13 = ee.ChartAreas[0].AxisX.Maximum - ee.ChartAreas[0].AxisX.Minimum;
			double num14 = ee.ChartAreas[0].AxisX.Interval;
			if (gk.Checked)
			{
				num14 = Convert.ToDouble(gm.Text);
			}
			else if (num13 / ee.ChartAreas[0].AxisX.Interval < Convert.ToDouble(gg.Value))
			{
				num14 /= 2.0;
			}
			else if (num13 / ee.ChartAreas[0].AxisX.Interval > Convert.ToDouble(gf.Value))
			{
				num14 = ((!(num13 / ee.ChartAreas[0].AxisX.Interval > Convert.ToDouble(gf.Value * 2m))) ? (num14 * 2.0) : (num13 / Convert.ToDouble(gf.Value)));
			}
			if (gj.Checked)
			{
				if (Convert.ToDouble(gl.Text) < ee.ChartAreas[0].AxisX.Maximum)
				{
					ee.ChartAreas[0].AxisX.Minimum = Convert.ToDouble(gl.Text);
				}
				else
				{
					a("Error: invalid value!", Color.Red);
				}
			}
			if (go.Checked)
			{
				double num15 = Convert.ToDouble(gn.Text);
				if (num15 > ee.ChartAreas[0].AxisX.Minimum)
				{
					ee.ChartAreas[0].AxisX.Maximum = num15;
				}
				else
				{
					a("Error: invalid value!", Color.Red);
				}
			}
			ee.ChartAreas[0].AxisX.MajorGrid.Interval = num14;
			ee.ChartAreas[0].AxisX.Interval = num14;
		}
	}

	private void a(object A_0, ViewEventArgs A_1)
	{
		if (A_1.Axis.ToString() == "Axis-X axis")
		{
			c7 = A_1.NewSizeType;
			c5 = A_1.NewPosition;
			c6 = A_1.NewSize;
		}
	}

	private void a(object A_0, ToolTipEventArgs A_1)
	{
		if (A_1.HitTestResult.ChartElementType == ChartElementType.DataPoint)
		{
			int pointIndex = A_1.HitTestResult.PointIndex;
			DataPoint dataPoint = A_1.HitTestResult.Series.Points[pointIndex];
			A_1.Text = "(" + DateTime.FromOADate(dataPoint.XValue).ToString() + ", " + Convert.ToString(dataPoint.YValues[0]) + ")";
		}
	}

	private void c(object A_0, MouseEventArgs A_1)
	{
		if (A_1.Button == MouseButtons.Right)
		{
			c8 = !c8;
		}
	}

	private void w()
	{
		string text = d4.Columns[1].HeaderText;
		if (ge.SelectedIndex >= 0)
		{
			text = ge.Items[ge.SelectedIndex].ToString();
		}
		ge.Items.Clear();
		ge.Items.Add(d4.Columns[1].HeaderText);
		for (int num = 2; num < this.m_j.Columns.Count; num++)
		{
			if (Convert.ToInt32(this.m_aq[num - 2].Row["DataLevel"]) == 3)
			{
				ge.Items.Add(d4.Columns[num].HeaderText);
			}
		}
		for (int num2 = 0; num2 < ge.Items.Count; num2++)
		{
			if (ge.Items[num2].ToString() == text)
			{
				ge.SelectedIndex = num2;
			}
		}
	}

	private void ak(object A_0, EventArgs A_1)
	{
		da = ChartValueType.DateTime;
		c9 = "RecordTime";
		for (int num = 2; num < this.m_j.Columns.Count; num++)
		{
			if (d4.Columns[num].HeaderText == ge.Items[ge.SelectedIndex].ToString())
			{
				c9 = d4.Columns[num].DataPropertyName;
				da = ChartValueType.Double;
				break;
			}
		}
		a(da);
		a6();
	}

	private void aj(object A_0, EventArgs A_1)
	{
		a6();
	}

	private void ai(object A_0, EventArgs A_1)
	{
		a(da);
		a6();
	}

	private void ah(object A_0, EventArgs A_1)
	{
		a(da);
		a6();
	}

	private void a(ChartValueType A_0)
	{
		ee.ChartAreas.Clear();
		ee.ChartAreas.Add(new ChartArea());
		if (ee.Series.Count > 0)
		{
			ee.Series[0].LegendText = "Curve";
		}
		ee.Legends[0].BackColor = Color.Transparent;
		ee.Legends[0].ForeColor = Color.White;
		ee.Legends[0].DockedToChartArea = ee.ChartAreas[0].Name;
		ee.Legends[0].IsDockedInsideChartArea = true;
		ee.ChartAreas[0].BackColor = Color.Black;
		ee.ChartAreas[0].CursorX.IsUserSelectionEnabled = true;
		ee.ChartAreas[0].CursorX.Interval = 1E-05;
		ee.ChartAreas[0].CursorX.IsUserEnabled = true;
		ee.ChartAreas[0].CursorX.LineColor = Color.White;
		ee.ChartAreas[0].CursorX.LineDashStyle = ChartDashStyle.Dot;
		ee.ChartAreas[0].CursorY.IsUserSelectionEnabled = true;
		ee.ChartAreas[0].CursorY.Interval = 1E-05;
		ee.ChartAreas[0].CursorY.IsUserEnabled = true;
		ee.ChartAreas[0].CursorY.LineColor = Color.White;
		ee.ChartAreas[0].CursorY.LineDashStyle = ChartDashStyle.Dot;
		ee.ChartAreas[0].AxisX.MajorGrid.LineColor = Color.DarkGreen;
		ee.ChartAreas[0].AxisX.MajorGrid.LineDashStyle = ChartDashStyle.Dot;
		ee.ChartAreas[0].AxisX.LineColor = Color.DarkGreen;
		if (A_0 == ChartValueType.DateTime)
		{
			ee.ChartAreas[0].AxisX.LabelStyle.Format = "HH:mm:ss";
		}
		else
		{
			ee.ChartAreas[0].AxisX.Interval = (db = (ee.ChartAreas[0].AxisX.Maximum - ee.ChartAreas[0].AxisX.Minimum) / Convert.ToDouble(gf.Value));
			ee.ChartAreas[0].AxisX.LabelStyle.Format = "G5";
		}
		ee.ChartAreas[0].AxisY.IsStartedFromZero = false;
		ee.ChartAreas[0].AxisY.MajorGrid.LineColor = Color.DarkGreen;
		ee.ChartAreas[0].AxisY.MajorGrid.LineDashStyle = ChartDashStyle.Dot;
		ee.ChartAreas[0].AxisY.LineColor = Color.DarkGreen;
		ee.ChartAreas[0].AxisX.ScrollBar.BackColor = Color.Gray;
		ee.ChartAreas[0].AxisX.ScrollBar.ButtonColor = Color.DarkGreen;
		ee.ChartAreas[0].AxisX.ScrollBar.LineColor = Color.Black;
		ee.ChartAreas[0].AxisX.ScrollBar.Size = 18.0;
		ee.ChartAreas[0].AxisY.ScrollBar.BackColor = Color.Gray;
		ee.ChartAreas[0].AxisY.ScrollBar.ButtonColor = Color.DarkGreen;
		ee.ChartAreas[0].AxisY.ScrollBar.LineColor = Color.Black;
		ee.ChartAreas[0].AxisY.ScrollBar.Size = 18.0;
	}

	private void ag(object A_0, EventArgs A_1)
	{
		this.m_a2 = true;
	}

	private void af(object A_0, EventArgs A_1)
	{
		this.m_a2 = false;
	}

	private void ae(object A_0, EventArgs A_1)
	{
		e9.Visible = false;
	}

	public void f(string A_0, string A_1)
	{
		XmlReader xmlReader = null;
		if (!new FileInfo(this.m_bc + this.m_bd + "lang-" + this.m_b + ".xml").Exists)
		{
			return;
		}
		xmlReader = new XmlTextReader(this.m_bc + this.m_bd + "lang-" + this.m_b + ".xml");
		XmlDocument xmlDocument = new XmlDocument();
		xmlDocument.Load(xmlReader);
		foreach (XmlNode item in xmlDocument.DocumentElement.SelectNodes("Form[Name='" + A_0 + "']/Controls/Control"))
		{
			try
			{
				XmlNode xmlNode2 = item.SelectSingleNode("@name");
				XmlNode xmlNode3 = item.SelectSingleNode("@text");
				if (xmlNode2 != null)
				{
					this.m_d.Add(xmlNode2.InnerText.ToLower(), xmlNode3.InnerText);
				}
			}
			catch (FormatException)
			{
				MessageBox.Show("Read Lang XML failed!");
			}
		}
		xmlReader.Close();
	}

	public string g(string A_0, string A_1)
	{
		if (this.m_d.Contains(A_0.ToLower()))
		{
			return (string)this.m_d[A_0.ToLower()];
		}
		p("Language define not found: " + A_0);
		return A_1;
	}

	private void ad(object A_0, EventArgs A_1)
	{
		h("zh-CN");
	}

	private void ac(object A_0, EventArgs A_1)
	{
		h("en");
	}

	private void h(string A_0)
	{
		this.m_b = A_0;
		try
		{
			XDocument xDocument = XDocument.Load(this.m_be);
			xDocument.Element("EasyHost").Element("Language").Attribute("value")
				.SetValue(this.m_b);
			xDocument.Save(this.m_be);
		}
		catch (Exception ex)
		{
			a("Error: when saving language setting, " + ex.Message, Color.Red);
		}
		if (MessageBox.Show("Successfully. Restart to take effect!\r\n Restart the program Now?", "Reselect Language", MessageBoxButtons.OKCancel, MessageBoxIcon.Exclamation) != DialogResult.OK)
		{
			return;
		}
		if (b5 != null && b5.c())
		{
			try
			{
				b5.b();
			}
			catch
			{
			}
		}
		Dispose(disposing: true);
		Close();
		Application.Restart();
	}

	public void h(int A_0)
	{
		if (A_0 == 255)
		{
			this.m_g.Clear();
			this.m_a4 = "";
		}
		else
		{
			DataRow[] array = this.m_g.Select("addr = " + A_0);
			for (int num = 0; num < array.Length; num++)
			{
				array[num].Delete();
			}
		}
		dg.a(A_0);
		dh.a(A_0);
		this.m_g.AcceptChanges();
		a0();
		a1();
		au();
		at();
		this.m_a6 = false;
		a5();
		di.a();
	}

	public void a(string A_0, bool A_1, int A_2)
	{
		if (A_1)
		{
			this.m_g.Clear();
			this.m_a6 = true;
			try
			{
				this.m_g.ReadXml(A_0);
				this.m_a4 = Path.GetFileName(A_0);
				a(g("project_text_filename", "Project File") + ": " + Path.GetFileNameWithoutExtension(A_0), Color.Black);
				string a_ = "";
				XDocument xDocument = XDocument.Load(A_0);
				if (xDocument.Root.Attribute("Option") != null)
				{
					a_ = xDocument.Root.Attribute("Option").Value.ToString();
				}
				dg.a(a_);
				string a_2 = "";
				if (xDocument.Root.Attribute("Help") != null)
				{
					a_2 = xDocument.Root.Attribute("Help").Value.ToString();
				}
				dh.a(a_2);
			}
			catch (Exception ex)
			{
				MessageBox.Show(ex.Message);
			}
		}
		else
		{
			this.m_f.Clear();
			try
			{
				this.m_f.ReadXml(A_0);
				string a_3 = "";
				XDocument xDocument2 = XDocument.Load(A_0);
				if (xDocument2.Root.Attribute("Option") != null)
				{
					a_3 = xDocument2.Root.Attribute("Option").Value.ToString();
				}
				dg.a(a_3, A_2);
				string a_4 = "";
				if (xDocument2.Root.Attribute("Help") != null)
				{
					a_4 = xDocument2.Root.Attribute("Help").Value.ToString();
				}
				dh.a(a_4, A_2);
				DataTable dataTable = this.m_g.Clone();
				dataTable.Merge(this.m_f);
				string a_5 = this.m_f.Rows[0]["Model"].ToString();
				dataTable.Rows.Add(a(a_5, "MENU:INFO", "下位机信息", "新通讯协议", "软件通讯协议", "软件通讯协议：下位机信息", dataTable));
				for (int num = 0; num < dataTable.Rows.Count; num++)
				{
					dataTable.Rows[num]["Addr"] = A_2;
					dataTable.Rows[num]["datalevel"] = "0";
					dataTable.Rows[num]["Mark"] = "0";
				}
				for (int num2 = 0; num2 < dataTable.Rows.Count; num2++)
				{
					DataRow dataRow = this.m_g.NewRow();
					for (int num3 = 0; num3 < dataTable.Columns.Count; num3++)
					{
						if (this.m_g.Columns.Contains(dataTable.Columns[num3].ColumnName))
						{
							dataRow[dataTable.Columns[num3].ColumnName] = dataTable.Rows[num2][num3];
						}
					}
					this.m_g.Rows.Add(dataRow);
				}
				this.m_a6 = false;
				a(g("modelfile_text_filename", "Model File") + ": " + Path.GetFileNameWithoutExtension(A_0), Color.Black);
			}
			catch (Exception)
			{
			}
		}
		this.m_t.Clear();
		DataView dataView = new DataView(this.m_g);
		dataView.RowFilter = "foldername = 'PRIVATEOPTION'";
		for (int num4 = 0; num4 < dataView.Count; num4++)
		{
			DataRow dataRow2 = this.m_t.NewRow();
			dataRow2["ParamType"] = dataView[num4]["ParamType"];
			dataRow2["ParamValue"] = dataView[num4]["ParamName"];
			dataRow2["ValueText"] = dataView[num4]["ParamText"];
			dataRow2["CmdText"] = "";
			this.m_t.Rows.Add(dataRow2);
		}
		dg.a(this.m_t);
		ac();
		a0();
		a1();
		au();
		at();
		a5();
		di.a();
	}

	private void b(object A_0, MouseEventArgs A_1)
	{
		if (A_1.Button == MouseButtons.Right)
		{
			cv = f0.HitTest(A_1.X, A_1.Y).RowIndex;
		}
	}

	private void a(object A_0, MouseEventArgs A_1)
	{
		if (A_1.Button == MouseButtons.Left)
		{
			if (ch.Enabled)
			{
				return;
			}
			try
			{
				if (f0.HitTest(A_1.X, A_1.Y).RowIndex < 0)
				{
					f0.ClearSelection();
				}
				return;
			}
			catch (Exception)
			{
				return;
			}
		}
		if (A_1.Button != MouseButtons.Right || ch.Enabled)
		{
			return;
		}
		try
		{
			gv.Enabled = true;
			gx.Enabled = true;
			gw.Enabled = true;
			gy.Enabled = true;
			gz.Enabled = true;
			hb.Enabled = true;
			hc.Enabled = true;
			gv.Visible = true;
			gx.Visible = true;
			gw.Visible = true;
			gy.Visible = true;
			gz.Visible = true;
			hb.Visible = true;
			hc.Visible = true;
			if (f0.SelectedRows.Count == 0)
			{
				gv.Enabled = false;
				gx.Enabled = false;
				gw.Enabled = false;
				gy.Enabled = false;
				gv.Visible = false;
				gx.Visible = false;
				gw.Visible = false;
				gy.Visible = false;
			}
			if (f0.SelectedRows.Count != 1)
			{
				hb.Enabled = false;
				hc.Enabled = false;
				hb.Visible = false;
				hc.Visible = false;
			}
			if (this.m_q.Rows.Count == 0)
			{
				gz.Enabled = false;
				gz.Visible = false;
			}
			gu.Show(Control.MousePosition.X, Control.MousePosition.Y);
		}
		catch (Exception)
		{
		}
	}

	private void c(int A_0, int A_1)
	{
		f0.ClearSelection();
		for (int num = 0; num < this.m_aw.Count; num++)
		{
			int num2 = Convert.ToInt32(this.m_aw[num]["Index"]);
			if (num2 >= A_0 && num2 < A_0 + A_1)
			{
				f0.Rows[num].Selected = true;
			}
		}
	}

	private void ab(object A_0, EventArgs A_1)
	{
		try
		{
			if (f0.SelectedRows.Count == 0 || MessageBox.Show(g("script_text_confirmRemove", "Remove the selected script rows?"), g("text_confirm", "Confirm"), MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
			{
				return;
			}
			for (int num = f0.Rows.Count - 1; num >= 0; num--)
			{
				if (f0.Rows[num].Selected)
				{
					this.m_aw[num].Delete();
				}
			}
			DataView dataView = new DataView(this.m_p);
			dataView.Sort = "Index ASC";
			for (int num2 = 0; num2 < dataView.Count; num2++)
			{
				this.m_aw[num2]["Index"] = num2;
			}
		}
		catch
		{
		}
	}

	private void aa(object A_0, EventArgs A_1)
	{
		try
		{
			int num = 0;
			int num2 = 0;
			for (int num3 = 0; num3 < f0.Rows.Count; num3++)
			{
				if (f0.Rows[num3].Selected)
				{
					num2++;
					if (num2 == 1)
					{
						num = num3;
					}
					if (num + num2 - 1 != num3)
					{
						return;
					}
				}
			}
			if (num2 == 0)
			{
				return;
			}
			if (num > 0)
			{
				this.m_aw[num - 1]["Index"] = num + num2 - 1;
				object[] itemArray = this.m_p.Rows[num - 1].ItemArray;
				for (int num4 = 0; num4 < num2; num4++)
				{
					this.m_aw[num + num4]["Index"] = num + num4 - 1;
					this.m_p.Rows[num + num4 - 1].ItemArray = this.m_p.Rows[num + num4].ItemArray;
				}
				this.m_p.Rows[num + num2 - 1].ItemArray = itemArray;
			}
			f0.ClearSelection();
			for (int num5 = num; num5 < num + num2; num5++)
			{
				f0.Rows[num5 - 1].Selected = true;
			}
		}
		catch
		{
		}
	}

	private void z(object A_0, EventArgs A_1)
	{
		try
		{
			int num = 0;
			int num2 = 0;
			for (int num3 = 0; num3 < f0.Rows.Count; num3++)
			{
				if (f0.Rows[num3].Selected)
				{
					num2++;
					if (num2 == 1)
					{
						num = num3;
					}
					if (num + num2 - 1 != num3)
					{
						return;
					}
				}
			}
			if (num2 == 0)
			{
				return;
			}
			if (num + num2 < this.m_aw.Count)
			{
				this.m_aw[num + num2]["Index"] = num;
				object[] itemArray = this.m_p.Rows[num + num2].ItemArray;
				for (int num4 = 0; num4 < num2; num4++)
				{
					this.m_aw[num + num2 - num4 - 1]["Index"] = num + num2 - num4;
					this.m_p.Rows[num + num2 - num4].ItemArray = this.m_p.Rows[num + num2 - num4 - 1].ItemArray;
				}
				this.m_p.Rows[num].ItemArray = itemArray;
			}
			f0.ClearSelection();
			for (int num5 = num; num5 < num + num2; num5++)
			{
				f0.Rows[num5 + 1].Selected = true;
			}
		}
		catch
		{
		}
	}

	private void y(object A_0, EventArgs A_1)
	{
		try
		{
			this.m_q.Clear();
			for (int num = 0; num < f0.Rows.Count; num++)
			{
				if (f0.Rows[num].Selected)
				{
					DataRow dataRow = this.m_q.NewRow();
					dataRow.ItemArray = this.m_p.Rows[num].ItemArray;
					this.m_q.Rows.Add(dataRow);
				}
			}
		}
		catch
		{
		}
	}

	private void x(object A_0, EventArgs A_1)
	{
		if (f0.SelectedRows.Count > 1)
		{
			return;
		}
		try
		{
			int num = 0;
			num = ((f0.SelectedRows.Count != 0) ? f0.SelectedRows[0].Index : this.m_p.Rows.Count);
			for (int num2 = num; num2 < this.m_p.Rows.Count; num2++)
			{
				this.m_p.Rows[num2]["Index"] = num2 + this.m_q.Rows.Count;
			}
			for (int num3 = 0; num3 < this.m_q.Rows.Count; num3++)
			{
				this.m_q.Rows[num3]["Index"] = num + num3;
				DataRow dataRow = this.m_p.NewRow();
				dataRow.ItemArray = this.m_q.Rows[num3].ItemArray;
				this.m_p.Rows.InsertAt(dataRow, num3 + num);
			}
			f0.ClearSelection();
			for (int num4 = num; num4 < num + this.m_q.Rows.Count; num4++)
			{
				f0.Rows[num4].Selected = true;
			}
		}
		catch
		{
		}
	}

	private void w(object A_0, EventArgs A_1)
	{
		if (f0.SelectedRows.Count > 1)
		{
			return;
		}
		try
		{
			int num = 0;
			num = ((f0.SelectedRows.Count != 0) ? f0.SelectedRows[0].Index : this.m_p.Rows.Count);
			for (int num2 = num; num2 < this.m_p.Rows.Count; num2++)
			{
				this.m_p.Rows[num2]["Index"] = num2 + 1;
			}
			DataRow dataRow = this.m_p.NewRow();
			dataRow["ParamName"] = "SCRIPTGO";
			dataRow["ParamText"] = g("script_cmd_go", "Go");
			dataRow["ModuleName"] = "1";
			dataRow["ValueText"] = "1";
			dataRow["Addr"] = "0";
			dataRow["Cmd"] = "=";
			dataRow["Var"] = "1";
			dataRow["Count"] = "0";
			dataRow["Reg"] = -1;
			dataRow["Index"] = num;
			this.m_p.Rows.InsertAt(dataRow, num);
			f0.ClearSelection();
			f0.Rows[num].Selected = true;
		}
		catch
		{
		}
	}

	private void v(object A_0, EventArgs A_1)
	{
		if (f0.SelectedRows.Count > 1)
		{
			return;
		}
		try
		{
			int num = 0;
			num = ((f0.SelectedRows.Count != 0) ? f0.SelectedRows[0].Index : this.m_p.Rows.Count);
			for (int num2 = num; num2 < this.m_p.Rows.Count; num2++)
			{
				this.m_p.Rows[num2]["Index"] = num2 + 1;
			}
			DataRow dataRow = this.m_p.NewRow();
			dataRow["ParamName"] = "SCRIPTDELAY";
			dataRow["ParamText"] = g("script_cmd_delay", "Delay");
			dataRow["ModuleName"] = "1";
			dataRow["ValueText"] = "1";
			dataRow["Addr"] = "0";
			dataRow["Cmd"] = "=";
			dataRow["Var"] = "1";
			dataRow["Count"] = "0";
			dataRow["Reg"] = -1;
			dataRow["Index"] = num;
			this.m_p.Rows.InsertAt(dataRow, num);
			f0.ClearSelection();
			f0.Rows[num].Selected = true;
		}
		catch
		{
		}
	}

	private void u(object A_0, EventArgs A_1)
	{
		if (f0.SelectedRows.Count > 1)
		{
			return;
		}
		try
		{
			int num = 0;
			num = ((f0.SelectedRows.Count != 0) ? f0.SelectedRows[0].Index : this.m_p.Rows.Count);
			for (int num2 = num; num2 < this.m_p.Rows.Count; num2++)
			{
				this.m_p.Rows[num2]["Index"] = num2 + 1;
			}
			DataRow dataRow = this.m_p.NewRow();
			dataRow["ParamName"] = "Param";
			dataRow["ParamText"] = g("script_cmd_blank", "Blank");
			dataRow["ModuleName"] = "Module";
			dataRow["ValueText"] = "1";
			dataRow["Addr"] = "0";
			dataRow["Cmd"] = "=";
			dataRow["Var"] = "1";
			dataRow["Count"] = "0";
			dataRow["Reg"] = -1;
			dataRow["Index"] = num;
			this.m_p.Rows.InsertAt(dataRow, num);
			f0.ClearSelection();
			f0.Rows[num].Selected = true;
		}
		catch
		{
		}
	}

	private void t(object A_0, EventArgs A_1)
	{
		if (b5 != null && b5.c())
		{
			if (!ch.Enabled && f0.SelectedRows.Count == 1)
			{
				ci = f0.SelectedRows[0].Index;
				for (int num = 0; num < this.m_p.Rows.Count; num++)
				{
					this.m_p.Rows[num]["count"] = "0";
				}
				ck = Math.Pow(10.0, 14.0);
				f2.Text = g("script_btn_pause", "Pause");
				f2.BackColor = Color.GreenYellow;
				ch.Enabled = true;
			}
		}
		else
		{
			MessageBox.Show(g("warning_openserialport", "Please Open COM at first"), g("text_error", "Error"));
		}
	}

	private void s(object A_0, EventArgs A_1)
	{
		if (b5 != null && b5.c())
		{
			if (!ch.Enabled && f0.SelectedRows.Count == 1)
			{
				ci = f0.SelectedRows[0].Index;
				ck = Math.Pow(10.0, 14.0);
				f2.Text = g("script_btn_pause", "Pause");
				f2.BackColor = Color.GreenYellow;
				ch.Enabled = true;
			}
		}
		else
		{
			MessageBox.Show(g("warning_openserialport", "Please Open COM at first"), g("text_error", "Error"));
		}
	}

	private void b(object A_0, DragEventArgs A_1)
	{
		A_1.Effect = DragDropEffects.Move;
	}

	private void a(object A_0, DataGridViewCellMouseEventArgs A_1)
	{
		if (A_1.Clicks < 1 && A_1.Button == MouseButtons.Right && A_1.ColumnIndex == -1 && A_1.RowIndex > -1)
		{
			f0.DoDragDrop(f0.Rows[A_1.RowIndex], DragDropEffects.Move);
		}
	}

	private int b(int A_0, int A_1)
	{
		int rowCount = f0.RowCount;
		for (int num = 0; num < rowCount; num++)
		{
			Rectangle rowDisplayRectangle = f0.GetRowDisplayRectangle(num, cutOverflow: false);
			if (f0.RectangleToScreen(rowDisplayRectangle).Contains(A_0, A_1))
			{
				return num;
			}
		}
		if (rowCount > 0)
		{
			Rectangle rowDisplayRectangle2 = f0.GetRowDisplayRectangle(rowCount - 1, cutOverflow: false);
			Rectangle rectangle = new Rectangle(rowDisplayRectangle2.X, rowDisplayRectangle2.Y + f0.Rows[rowCount - 1].Height, f0.Width, f0.Height - rowDisplayRectangle2.Y - f0.Rows[rowCount - 1].Height);
			if (f0.RectangleToScreen(rectangle).Contains(A_0, A_1))
			{
				return rowCount;
			}
		}
		return -1;
	}

	private void a(object A_0, DragEventArgs A_1)
	{
		int num = b(A_1.X, A_1.Y);
		if (cv == num || num < 0 || !A_1.Data.GetDataPresent(typeof(DataGridViewRow)))
		{
			return;
		}
		int num2 = 0;
		int num3 = 0;
		for (int num4 = 0; num4 < f0.Rows.Count; num4++)
		{
			if (f0.Rows[num4].Selected)
			{
				num3++;
				if (num3 == 1)
				{
					num2 = num4;
				}
				if (num2 + num3 - 1 != num4)
				{
					return;
				}
			}
		}
		this.m_q.Clear();
		for (int num5 = 0; num5 < f0.Rows.Count; num5++)
		{
			if (f0.Rows[num5].Selected)
			{
				DataRow dataRow = this.m_q.NewRow();
				dataRow.ItemArray = this.m_p.Rows[num5].ItemArray;
				this.m_q.Rows.Add(dataRow);
			}
		}
		if (this.m_q.Rows.Count == 0)
		{
			return;
		}
		int num6 = 0;
		if (num2 < num)
		{
			num6 = num - num3;
		}
		else
		{
			if (num2 <= num)
			{
				return;
			}
			num6 = num;
		}
		for (int num7 = 0; num7 < num3; num7++)
		{
			this.m_p.Rows.RemoveAt(num2);
		}
		for (int num8 = 0; num8 < num3; num8++)
		{
			DataRow dataRow2 = this.m_p.NewRow();
			dataRow2.ItemArray = this.m_q.Rows[num8].ItemArray;
			this.m_p.Rows.InsertAt(dataRow2, num6 + num8);
		}
		for (int num9 = 0; num9 < this.m_p.Rows.Count; num9++)
		{
			this.m_p.Rows[num9]["Index"] = num9;
		}
		f0.ClearSelection();
		for (int num10 = 0; num10 < num3; num10++)
		{
			f0.Rows[num10 + num6].Selected = true;
		}
	}

	private void r(object A_0, EventArgs A_1)
	{
		cj = Convert.ToDouble(g4.Value);
		ch.Interval = cj * 1000.0;
		try
		{
			XDocument xDocument = XDocument.Load(this.m_be);
			xDocument.Element("EasyHost").Element("ScriptInterval").Attribute("value")
				.SetValue(g4.Value);
			xDocument.Save(this.m_be);
		}
		catch (Exception ex)
		{
			MessageBox.Show("Save Failed: " + ex.Message, "Error");
		}
	}

	private void q(object A_0, EventArgs A_1)
	{
		d0.Height = d3.Location.Y - d0.Location.Y - 4;
		d0.Width = base.Width - d0.Location.X - 6;
		hp.Height = d3.Location.Y - hp.Location.Y - 4;
	}

	private void a(object A_0, DataGridViewCellValueEventArgs A_1)
	{
		if (A_1.RowIndex < d4.RowCount && A_1.ColumnIndex < this.m_j.Columns.Count && d4.RowCount - A_1.RowIndex - 1 < this.m_j.Rows.Count)
		{
			A_1.Value = this.m_j.Rows[d4.RowCount - A_1.RowIndex - 1][A_1.ColumnIndex];
		}
	}

	private void p(object A_0, EventArgs A_1)
	{
		this.m_a3 = "cmdlist";
		e4.Filter = "Text Document(*.txt)|*.txt | CSV File(*.csv)|*.csv";
		e4.InitialDirectory = this.m_bh;
		e4.FileName = "";
		e4.ShowDialog();
	}

	private void o(object A_0, EventArgs A_1)
	{
		this.m_a3 = "cmd2script";
		e4.Filter = "Script File(*.script)|*.script";
		e4.InitialDirectory = this.m_bg;
		e4.FileName = f5.Text;
		e4.ShowDialog();
	}

	private void n(object A_0, EventArgs A_1)
	{
		if (b5 != null && b5.c())
		{
			int num = a2();
			DataRow[] array = this.m_g.Select("addr = " + num + " AND ParamLevel >= 3");
			foreach (DataRow dataRow in array)
			{
				cp.Enqueue(dataRow["ParamName"].ToString() + "?@" + num);
			}
		}
		else
		{
			MessageBox.Show(g("warning_openserialport", "Please Open COM at first"), g("text_error", "Error"));
		}
	}

	private void m(object A_0, EventArgs A_1)
	{
		this.m_a3 = "savedvaluelist";
		e4.Filter = "Text Document(*.txt)|*.txt | CSV File(*.csv)|*.csv";
		e4.InitialDirectory = this.m_bh;
		e4.FileName = "";
		e4.ShowDialog();
	}

	private void g(string A_0)
	{
		int num = a2();
		if (File.Exists(A_0))
		{
			MessageBox.Show("File Exist: " + A_0, "Error");
			return;
		}
		StreamWriter streamWriter = File.CreateText(A_0);
		StringBuilder stringBuilder = new StringBuilder();
		string text = "\t\t";
		if (Path.GetExtension(A_0) == ".csv")
		{
			text = ",";
			streamWriter.Write('\ufeff');
		}
		DataRow[] array = this.m_g.Select("addr = " + num);
		foreach (DataRow dataRow in array)
		{
			if (!(dataRow["ParamName"].ToString() != "MENU:INFO") || !(dataRow["FolderName"].ToString() != "PRIVATEOPTION"))
			{
				continue;
			}
			stringBuilder.Append(dataRow["ParamName"].ToString() + text);
			if (dataRow["FolderName"].ToString() == dataRow["PageName"].ToString())
			{
				stringBuilder.Append(dataRow["FolderName"].ToString() + ">" + dataRow["ParamText"].ToString() + text);
			}
			else
			{
				stringBuilder.Append(dataRow["FolderName"].ToString() + ">" + dataRow["PageName"].ToString() + ">" + dataRow["ParamText"].ToString() + text);
			}
			DataView dataView = dg.b(dataRow["paramtype"].ToString(), Convert.ToInt32(dataRow["Addr"]));
			if (dataView.Count > 0)
			{
				stringBuilder.Append(g("info_paramvalue", "Option Value") + ": ");
				for (int num3 = 0; num3 < dataView.Count; num3++)
				{
					stringBuilder.Append(string.Concat(dataView[num3]["paramvalue"], " (", dataView[num3]["ValueText"], "); "));
				}
			}
			else
			{
				stringBuilder.Append(g("info_datatype", "Data Type") + ": " + dataRow["ParamType"].ToString());
			}
			streamWriter.WriteLine(stringBuilder.ToString());
			stringBuilder.Clear();
		}
		streamWriter.Close();
	}

	private void f(string A_0)
	{
		DataTable dataTable = new DataTable("Table_Script");
		dataTable.Columns.Add("Addr", typeof(int));
		dataTable.Columns.Add("ModuleName", Type.GetType("System.String"));
		dataTable.Columns.Add("ParamName", Type.GetType("System.String"));
		dataTable.Columns.Add("ParamText", Type.GetType("System.String"));
		dataTable.Columns.Add("Cmd", Type.GetType("System.String"));
		dataTable.Columns.Add("ValueText", Type.GetType("System.String"));
		dataTable.Columns.Add("Var", typeof(double));
		dataTable.Columns.Add("Count", typeof(double));
		dataTable.Columns.Add("Reg", typeof(int));
		dataTable.Columns.Add("Index", typeof(int));
		int num = a2();
		DataRow[] array = this.m_g.Select("addr = " + num);
		foreach (DataRow dataRow in array)
		{
			if (dataRow["ParamName"].ToString() != "MENU:INFO" && Convert.ToInt32(dataRow["ParamLevel"]) >= 3 && dataRow["ParamValue"].ToString().Length > 0)
			{
				string[] array2 = dataRow["ParamName"].ToString().Split(new char[1] { ':' }, 2);
				DataRow dataRow2 = dataTable.NewRow();
				dataRow2["ModuleName"] = array2[0];
				dataRow2["ParamName"] = array2[1];
				dataRow2["ParamText"] = dataRow["ParamText"];
				dataRow2["ValueText"] = dataRow["ParamValue"];
				dataRow2["Addr"] = dataRow["Addr"];
				dataRow2["Var"] = "1";
				dataRow2["Count"] = "0";
				dataRow2["Reg"] = -1;
				dataRow2["Cmd"] = "=";
				dataTable.Rows.Add(dataRow2);
				DataRow dataRow3 = dataTable.NewRow();
				dataRow3["ModuleName"] = array2[0];
				dataRow3["ParamName"] = array2[1];
				dataRow3["ParamText"] = dataRow["ParamText"];
				dataRow3["ValueText"] = "0";
				dataRow3["Addr"] = dataRow["Addr"];
				dataRow3["Var"] = "1";
				dataRow3["Count"] = "0";
				dataRow3["Reg"] = -1;
				dataRow3["Cmd"] = "!";
				dataTable.Rows.Add(dataRow3);
			}
		}
		if (dataTable.Rows.Count > 0)
		{
			for (int num3 = 0; num3 < dataTable.Rows.Count; num3++)
			{
				dataTable.Rows[num3]["count"] = "0";
			}
			dataTable.WriteXml(A_0);
		}
		else
		{
			MessageBox.Show("No Data", "Error");
		}
	}

	private void e(string A_0)
	{
		int num = a2();
		if (File.Exists(A_0))
		{
			MessageBox.Show("File Exist: " + A_0, "Error");
			return;
		}
		StreamWriter streamWriter = File.CreateText(A_0);
		StringBuilder stringBuilder = new StringBuilder();
		string text = "\t\t";
		if (Path.GetExtension(A_0) == ".csv")
		{
			text = ",";
			streamWriter.Write('\ufeff');
		}
		DataRow[] array = this.m_g.Select("addr = " + num + " AND ParamLevel >= 3");
		foreach (DataRow dataRow in array)
		{
			stringBuilder.Append(dataRow["ParamName"].ToString() + text);
			if (dataRow["FolderName"].ToString() == dataRow["PageName"].ToString())
			{
				stringBuilder.Append(dataRow["FolderName"].ToString() + ">" + dataRow["ParamText"].ToString() + text);
			}
			else
			{
				stringBuilder.Append(dataRow["FolderName"].ToString() + ">" + dataRow["PageName"].ToString() + ">" + dataRow["ParamText"].ToString() + text);
			}
			stringBuilder.Append(dataRow["ParamValue"].ToString() + text);
			stringBuilder.Append(dataRow["ValueText"].ToString());
			streamWriter.WriteLine(stringBuilder.ToString());
			stringBuilder.Clear();
		}
		streamWriter.Close();
	}

	public string a8()
	{
		if (dj != null)
		{
			return dj.Text;
		}
		return "";
	}

	public string a9()
	{
		if (dj != null)
		{
			return dj.ToolTipText;
		}
		return "";
	}

	private void l(object A_0, EventArgs A_1)
	{
		if (hp.Items.Count > 0 && hp.SelectedItems.Count > 0 && hp.SelectedItems[0] != dj)
		{
			for (int num = 0; num < hp.Items.Count; num++)
			{
				hp.Items[num].BackColor = Color.White;
			}
			hp.Items[hp.SelectedIndices[0]].BackColor = Color.SkyBlue;
			dj = hp.SelectedItems[0];
			au();
			at();
			a6();
		}
	}

	private void k(object A_0, EventArgs A_1)
	{
		if (b5 != null && b5.c())
		{
			cp.Enqueue(hj.Text);
		}
	}

	private void j(object A_0, EventArgs A_1)
	{
		int num = -1;
		for (int num2 = 0; num2 < this.m_h.Rows.Count; num2++)
		{
			num = Math.Max(num, Convert.ToInt32(this.m_h.Rows[num2]["Addr"]));
		}
		global::i i2 = new global::i(g("device_text_new", "Add a device"), g("modelfile_selectmodel", "Model"), g("modelfile_selectversion", "Version"), g("modelfile_selectaddr", "Address"), (num + 1).ToString(), di);
		if (i2.ShowDialog() == DialogResult.OK)
		{
			if (!r(i2.c()))
			{
				a(this.m_bc + this.m_bd + this.m_bi + i2.b() + ".xml", A_1: false, Convert.ToInt32(i2.c()));
			}
			else
			{
				MessageBox.Show(g("text_failed", "Failed") + "! " + g("warning_addrisused", "This Addr has been used!"));
			}
		}
	}

	private void i(object A_0, EventArgs A_1)
	{
		if (this.m_g.Rows.Count > 0)
		{
			MessageBoxButtons buttons = MessageBoxButtons.OKCancel;
			if (MessageBox.Show(g("device_text_remove", "Current device will be removed"), g("text_continue", "Continue") + "?", buttons) != DialogResult.Cancel)
			{
				h(a2());
			}
		}
		else
		{
			MessageBox.Show(g("warning_nodeviceinproject", "There is no device in current project"), g("text_error", "Error"));
		}
	}

	private void h(object A_0, EventArgs A_1)
	{
		this.m_an.RowFilter = "Mark = 1";
	}

	private void g(object A_0, EventArgs A_1)
	{
		j(A_0, A_1);
	}

	private void f(object A_0, EventArgs A_1)
	{
		i(A_0, A_1);
	}

	private void a(object A_0, DrawListViewItemEventArgs A_1)
	{
		ListView listView = A_0 as ListView;
		int num = 50;
		Rectangle rect = new Rectangle(A_1.Bounds.X + 2, A_1.Bounds.Y + (num - listView.SmallImageList.ImageSize.Height) / 2, listView.SmallImageList.ImageSize.Width, listView.SmallImageList.ImageSize.Height);
		Rectangle rectangle = new Rectangle(rect.Right + 5, A_1.Bounds.Y, A_1.Bounds.Width - rect.Right - 5, num);
		if ((A_1.State & ListViewItemStates.Selected) != 0)
		{
			A_1.Graphics.FillRectangle(SystemBrushes.Highlight, rectangle);
		}
		else
		{
			A_1.Graphics.FillRectangle(SystemBrushes.Window, A_1.Bounds);
		}
		if (A_1.Item.ImageIndex != -1 && listView.SmallImageList != null)
		{
			A_1.Graphics.DrawImage(listView.SmallImageList.Images[A_1.Item.ImageIndex], rect);
		}
		using (Brush brush = new SolidBrush(((A_1.State & ListViewItemStates.Selected) != 0) ? SystemColors.HighlightText : SystemColors.WindowText))
		{
			StringFormat format = new StringFormat
			{
				LineAlignment = StringAlignment.Center,
				Alignment = StringAlignment.Near
			};
			A_1.Graphics.DrawString(A_1.Item.Text, listView.Font, brush, rectangle, format);
		}
		if ((A_1.State & ListViewItemStates.Focused) != 0)
		{
			A_1.DrawFocusRectangle();
		}
	}

	private void e(object A_0, EventArgs A_1)
	{
		if (this.m_u != global::f.a.a)
		{
			MessageBox.Show("有文件操作正在执行，无法执行此操作！", "提示");
			return;
		}
		d result;
		while (this.m_x.TryDequeue(out result))
		{
		}
		d obj = new d();
		obj.a(global::f.b.a);
		obj.a(A_0: false);
		d item = obj;
		this.m_x.Enqueue(item);
		d obj2 = new d();
		obj2.a(global::f.b.f);
		obj2.a(A_0: false);
		obj2.a(delegate
		{
			b("文件列表刷新完成", Color.Blue);
			return true;
		});
		item = obj2;
		this.m_x.Enqueue(item);
		v();
	}

	private void d(object A_0, EventArgs A_1)
	{
		if (this.m_u != global::f.a.a)
		{
			MessageBox.Show("有文件操作正在执行，无法执行此操作！", "提示");
		}
		else if (fm.SelectedItems.Count > 0)
		{
			string text = fm.SelectedItems[0].SubItems[0].Text;
			if (MessageBox.Show($"确定要删除文件 '{text}' 吗？", "确认", MessageBoxButtons.YesNo) == DialogResult.Yes)
			{
				a(text);
			}
		}
		else
		{
			MessageBox.Show("请先选择要删除的文件！", "提示");
		}
	}

	private void c(object A_0, EventArgs A_1)
	{
		if (this.m_u != global::f.a.a)
		{
			MessageBox.Show("有文件操作正在执行，无法执行此操作！", "提示");
		}
		else if (fm.SelectedItems.Count > 0)
		{
			string a_ = fm.SelectedItems[0].SubItems[0].Text;
			c(a_);
		}
		else
		{
			MessageBox.Show("请先选择要计算CRC的文件！", "提示");
		}
	}

	private void b(object A_0, EventArgs A_1)
	{
		if (this.m_u != global::f.a.a)
		{
			MessageBox.Show("有文件操作正在执行，无法执行此操作！", "提示");
			return;
		}
		OpenFileDialog openFileDialog = new OpenFileDialog();
		openFileDialog.Title = "选择要上传的文件";
		openFileDialog.Filter = "所有文件|*.*";
		if (openFileDialog.ShowDialog() == DialogResult.OK)
		{
			string fileName = openFileDialog.FileName;
			string fileName2 = Path.GetFileName(fileName);
			c(fileName, fileName2);
		}
	}

	private void a(object A_0, EventArgs A_1)
	{
		if (this.m_u != global::f.a.a)
		{
			MessageBox.Show("有文件操作正在执行，无法执行此操作！", "提示");
			return;
		}
		if (fm.SelectedItems.Count == 0)
		{
			MessageBox.Show("请先选择要下载的文件！", "提示");
			return;
		}
		string text = fm.SelectedItems[0].SubItems[0].Text;
		SaveFileDialog saveFileDialog = new SaveFileDialog();
		saveFileDialog.Title = "保存文件";
		saveFileDialog.FileName = text;
		saveFileDialog.Filter = "所有文件|*.*";
		saveFileDialog.OverwritePrompt = false;
		if (saveFileDialog.ShowDialog() == DialogResult.OK)
		{
			string fileName = saveFileDialog.FileName;
			b(text, fileName);
		}
	}

	private void v()
	{
		bool flag = true;
		while (flag && this.m_x.Count != 0)
		{
			flag = false;
			if (!this.m_x.TryDequeue(out this.m_w))
			{
				break;
			}
			try
			{
				this.m_v = this.m_w.d();
				this.m_y = this.m_w.e();
				switch (this.m_w.a())
				{
				case global::f.b.a:
					if (base.InvokeRequired)
					{
						BeginInvoke((MethodInvoker)delegate
						{
							fq.Enabled = false;
							fu.Enabled = false;
							fr.Enabled = false;
							fs.Enabled = false;
						});
					}
					else
					{
						fq.Enabled = false;
						fu.Enabled = false;
						fr.Enabled = false;
						fs.Enabled = false;
					}
					if (fm.InvokeRequired)
					{
						fm.BeginInvoke((MethodInvoker)delegate
						{
							fm.Items.Clear();
						});
					}
					else
					{
						fm.Items.Clear();
					}
					if (!this.m_v)
					{
						b("正在刷新文件列表...", Color.Blue);
					}
					this.m_u = global::f.a.b;
					cp.Enqueue("get(LFMGR:FILELIST)");
					break;
				case global::f.b.b:
					if (!string.IsNullOrEmpty(this.m_w.b()))
					{
						d(this.m_w.b());
					}
					break;
				case global::f.b.c:
					if (!string.IsNullOrEmpty(this.m_w.b()))
					{
						b(this.m_w.b());
					}
					break;
				case global::f.b.d:
					if (!string.IsNullOrEmpty(this.m_w.b()) && !string.IsNullOrEmpty(this.m_w.c()))
					{
						d(this.m_w.c(), this.m_w.b());
					}
					break;
				case global::f.b.e:
					if (!string.IsNullOrEmpty(this.m_w.b()) && !string.IsNullOrEmpty(this.m_w.c()))
					{
						a(this.m_w.b(), this.m_w.c());
					}
					break;
				case global::f.b.g:
				{
					if (string.IsNullOrEmpty(this.m_w.b()))
					{
						break;
					}
					ListViewItem listViewItem = fm.Items.Cast<ListViewItem>().FirstOrDefault((ListViewItem A_0) => A_0.Text.Equals(this.m_w.b(), StringComparison.OrdinalIgnoreCase));
					if (listViewItem != null && !string.IsNullOrEmpty(listViewItem.SubItems[0].Text))
					{
						if (uint.TryParse(listViewItem.SubItems[2].Text, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out var result))
						{
							if (result == this.m_al)
							{
								b($"CRC验证成功！文件名: {this.m_w.b()}, CRC: {result:X8}", Color.Green);
								v();
							}
							else
							{
								b($"CRC验证失败！文件名: {this.m_w.b()}, 本地: {this.m_al:X8}, 远程: {result:X8}", Color.Red);
							}
						}
						else
						{
							b($"无法解析远程文件CRC: {this.m_w.b()}", Color.Red);
						}
					}
					else
					{
						b($"未找到指定文件或CRC为空: {this.m_w.b()}", Color.Red);
					}
					this.m_u = global::f.a.a;
					fq.Enabled = true;
					fu.Enabled = true;
					fr.Enabled = true;
					fs.Enabled = true;
					if (this.m_y != null)
					{
						this.m_y();
						this.m_y = null;
					}
					break;
				}
				case global::f.b.f:
					if (this.m_y != null)
					{
						bool num = this.m_y();
						this.m_y = null;
						flag = num;
					}
					break;
				}
			}
			catch (Exception ex)
			{
				d result2;
				while (this.m_x.TryDequeue(out result2))
				{
				}
				b($"LFMgr操作执行错误: {ex.Message}", Color.Red);
				this.m_u = global::f.a.a;
				fq.Enabled = true;
				fu.Enabled = true;
				fr.Enabled = true;
				fs.Enabled = true;
			}
		}
	}

	private void d(string A_0)
	{
		this.m_u = global::f.a.c;
		fq.Enabled = false;
		fu.Enabled = false;
		fr.Enabled = false;
		fs.Enabled = false;
		if (!this.m_v)
		{
			b($"正在获取文件\"{A_0}\"的CRC...", Color.Blue);
		}
		cp.Enqueue($"set(LFMGR:FILECRC, \"{A_0}\")");
	}

	private void c(string A_0)
	{
		d result;
		while (this.m_x.TryDequeue(out result))
		{
		}
		d obj = new d();
		obj.a(global::f.b.b);
		obj.a(A_0);
		obj.a(A_0: false);
		d item = obj;
		this.m_x.Enqueue(item);
		d obj2 = new d();
		obj2.a(global::f.b.f);
		obj2.a(A_0: false);
		obj2.a(delegate
		{
			b("文件CRC获取成功", Color.Blue);
			return true;
		});
		item = obj2;
		this.m_x.Enqueue(item);
		v();
	}

	private void b(string A_0)
	{
		this.m_u = global::f.a.d;
		fq.Enabled = false;
		fu.Enabled = false;
		fr.Enabled = false;
		fs.Enabled = false;
		if (!this.m_v)
		{
			b($"正在删除文件 '{A_0}'...", Color.Blue);
		}
		cp.Enqueue($"set(LFMGR:FILEDELETE, \"{A_0}\")");
	}

	private void a(string A_0)
	{
		d result;
		while (this.m_x.TryDequeue(out result))
		{
		}
		d obj = new d();
		obj.a(global::f.b.c);
		obj.a(A_0);
		obj.a(A_0: false);
		d item = obj;
		this.m_x.Enqueue(item);
		d obj2 = new d();
		obj2.a(global::f.b.a);
		obj2.a(A_0: true);
		item = obj2;
		this.m_x.Enqueue(item);
		d obj3 = new d();
		obj3.a(global::f.b.f);
		obj3.a(A_0: false);
		obj3.a(delegate
		{
			b("文件删除成功", Color.Blue);
			return true;
		});
		item = obj3;
		this.m_x.Enqueue(item);
		v();
	}

	private void d(string A_0, string A_1)
	{
		this.m_u = global::f.a.e;
		fq.Enabled = false;
		fu.Enabled = false;
		fr.Enabled = false;
		fs.Enabled = false;
		fw.Value = 0;
		this.m_z = A_0;
		this.m_aa = A_1;
		this.m_ab = new FileInfo(A_0).Length;
		this.m_ac = 0L;
		this.m_al = Utils.CalculateFileCRC32(A_0);
		this.m_ad = 0;
		if (!this.m_v)
		{
			b($"开始上传文件: {A_1}", Color.Blue);
			b($"文件大小: {this.m_ab} 字节", Color.Green);
			b($"文件CRC32: {this.m_al:X8}", Color.Green);
		}
		try
		{
			if (this.m_ae != null)
			{
				this.m_ae.Close();
				this.m_ae = null;
			}
			this.m_ae = new FileStream(A_0, FileMode.Open, FileAccess.Read);
			string item = global::p.a(global::p.a('t', this.m_ab, A_1));
			cp.Enqueue(item);
		}
		catch (Exception ex)
		{
			b($"上传错误: {ex.Message}", Color.Red);
			if (this.m_ae != null)
			{
				this.m_ae.Close();
				this.m_ae = null;
			}
			u();
		}
	}

	private void c(string A_0, string A_1)
	{
		d result;
		while (this.m_x.TryDequeue(out result))
		{
		}
		d obj = new d();
		obj.a(global::f.b.d);
		obj.b(A_0);
		obj.a(A_1);
		obj.a(A_0: false);
		d item = obj;
		this.m_x.Enqueue(item);
		d obj2 = new d();
		obj2.a(global::f.b.a);
		obj2.a(A_0: true);
		item = obj2;
		this.m_x.Enqueue(item);
		d obj3 = new d();
		obj3.a(global::f.b.b);
		obj3.a(A_1);
		obj3.a(A_0: true);
		item = obj3;
		this.m_x.Enqueue(item);
		d obj4 = new d();
		obj4.a(global::f.b.g);
		obj4.a(A_1);
		obj4.a(A_0: true);
		item = obj4;
		this.m_x.Enqueue(item);
		d obj5 = new d();
		obj5.a(global::f.b.f);
		obj5.a(A_0: false);
		obj5.a(delegate
		{
			b("文件上传成功", Color.Blue);
			return true;
		});
		item = obj5;
		this.m_x.Enqueue(item);
		v();
	}

	private void u()
	{
		if (this.m_ae != null)
		{
			this.m_ae.Close();
			this.m_ae = null;
		}
		this.m_z = string.Empty;
		this.m_aa = string.Empty;
		this.m_ab = 0L;
		this.m_ac = 0L;
		this.m_ad = 0;
		this.m_u = global::f.a.a;
		BeginInvoke((MethodInvoker)delegate
		{
			fq.Enabled = true;
			fu.Enabled = true;
			fr.Enabled = true;
			fs.Enabled = true;
			fv.Enabled = true;
			if (this.m_y != null)
			{
				this.m_y();
				this.m_y = null;
			}
			fw.Value = 0;
		});
	}

	private void t()
	{
		if (this.m_ak != null)
		{
			try
			{
				this.m_ak.Close();
			}
			catch (Exception ex)
			{
				b($"关闭下载文件流错误: {ex.Message}", Color.Red);
			}
			finally
			{
				this.m_ak = null;
			}
		}
		this.m_af = string.Empty;
		this.m_ag = string.Empty;
		this.m_ah = 0L;
		this.m_ai = 0L;
		this.m_aj = 0;
		this.m_u = global::f.a.a;
		BeginInvoke((MethodInvoker)delegate
		{
			fq.Enabled = true;
			fu.Enabled = true;
			fr.Enabled = true;
			fs.Enabled = true;
			fv.Enabled = true;
			if (this.m_y != null)
			{
				this.m_y();
				this.m_y = null;
			}
			fw.Value = 0;
		});
	}

	private void b(string A_0, string A_1)
	{
		d result;
		while (this.m_x.TryDequeue(out result))
		{
		}
		d obj = new d();
		obj.a(global::f.b.e);
		obj.a(A_0);
		obj.b(A_1);
		obj.a(A_0: false);
		d item = obj;
		this.m_x.Enqueue(item);
		d obj2 = new d();
		obj2.a(global::f.b.b);
		obj2.a(A_0);
		obj2.a(A_0: true);
		item = obj2;
		this.m_x.Enqueue(item);
		d obj3 = new d();
		obj3.a(global::f.b.g);
		obj3.a(A_0);
		obj3.a(A_0: true);
		item = obj3;
		this.m_x.Enqueue(item);
		d obj4 = new d();
		obj4.a(global::f.b.f);
		obj4.a(A_0: false);
		obj4.a(delegate
		{
			b("文件下载成功", Color.Blue);
			return true;
		});
		item = obj4;
		this.m_x.Enqueue(item);
		v();
	}

	private void a(int A_0, int A_1)
	{
		if (this.m_ae == null || this.m_u != global::f.a.e)
		{
			return;
		}
		try
		{
			if (A_0 == this.m_ab)
			{
				A_1 = 0;
			}
			if (A_1 > 0)
			{
				long val = ((A_1 > 512) ? 512 : A_1);
				long val2 = this.m_ae.Length - A_0;
				val = Math.Min(val, val2);
				if (val < 0)
				{
					b($"下位机文件错误，需要删除重新上传。需要从{A_0}字节处读取，实际总长才{this.m_ae.Length}个字节", Color.Red);
					u();
					return;
				}
				byte[] array = new byte[val];
				this.m_ae.Seek(A_0, SeekOrigin.Begin);
				int num = this.m_ae.Read(array, 0, array.Length);
				if (num < val)
				{
					b($"读取文件数据失败：预期{val}字节，实际读取{num}字节", Color.Red);
					u();
					return;
				}
				string item = global::p.a(global::p.a('T', (long)A_0, array));
				cp.Enqueue(item);
				this.m_ac = A_0 + val;
				int a2 = (int)(this.m_ac * 100 / this.m_ab);
				if (fw.InvokeRequired)
				{
					fw.Invoke((Action)delegate
					{
						fw.Value = a2;
					});
				}
				else
				{
					fw.Value = a2;
				}
			}
			else
			{
				string item2 = global::p.a(global::p.a('T'));
				cp.Enqueue(item2);
				this.m_ad = 0;
			}
		}
		catch (Exception ex)
		{
			b($"数据块发送错误: {ex.Message}", Color.Red);
			this.m_ad++;
			if (this.m_ad < 3)
			{
				b($"重试发送数据块 ({this.m_ad}/3)...", Color.Orange);
				a(A_0, A_1);
			}
			else
			{
				b("上传失败: 超过最大重试次数", Color.Red);
				u();
			}
		}
	}

	private void a(string A_0, string A_1)
	{
		try
		{
			this.m_u = global::f.a.f;
			fq.Enabled = false;
			fu.Enabled = false;
			fr.Enabled = false;
			fs.Enabled = false;
			fv.Enabled = false;
			fw.Value = 0;
			if (string.IsNullOrEmpty(A_0))
			{
				b("下载文件名不能为空", Color.Red);
				t();
				return;
			}
			if (string.IsNullOrEmpty(A_1))
			{
				b("本地文件路径不能为空", Color.Red);
				t();
				return;
			}
			this.m_ag = A_0;
			this.m_ah = 0L;
			foreach (ListViewItem item2 in fm.Items)
			{
				if (item2.Text == A_0)
				{
					if (item2.Tag != null && item2.Tag is long)
					{
						this.m_ah = (long)item2.Tag;
					}
					break;
				}
			}
			this.m_af = A_1;
			this.m_ai = 0L;
			this.m_al = uint.MaxValue;
			this.m_aj = 0;
			if (File.Exists(A_1) && MessageBox.Show($"文件 {A_1} 已存在，是否覆盖？", "文件已存在", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes)
			{
				b($"用户取消了下载: {A_0}", Color.Blue);
				t();
				return;
			}
			string directoryName = Path.GetDirectoryName(A_1);
			if (!string.IsNullOrEmpty(directoryName) && !Directory.Exists(directoryName))
			{
				try
				{
					Directory.CreateDirectory(directoryName);
				}
				catch (Exception ex)
				{
					b($"创建目录失败: {ex.Message}", Color.Red);
					t();
					return;
				}
			}
			if (this.m_ak != null)
			{
				try
				{
					this.m_ak.Close();
				}
				catch (Exception ex2)
				{
					b($"关闭旧下载文件流错误: {ex2.Message}", Color.Red);
				}
				finally
				{
					this.m_ak = null;
				}
			}
			this.m_ak = new MemoryStream();
			string fileName = Path.GetFileName(A_1);
			if (!this.m_v)
			{
				b($"开始下载文件: {A_0} -> {fileName}", Color.Blue);
			}
			string item = global::p.a(global::p.a('r', 120L, A_0));
			cp.Enqueue(item);
		}
		catch (Exception ex3)
		{
			b($"下载初始化错误: {ex3.Message}", Color.Red);
			t();
		}
	}

	private void a(long A_0, byte[] A_1)
	{
		if (this.m_ak == null || this.m_u != global::f.a.f)
		{
			return;
		}
		try
		{
			if (A_1 == null || A_1.Length == 0)
			{
				if (this.m_ak != null)
				{
					try
					{
						using FileStream destination = new FileStream(this.m_af, FileMode.Create, FileAccess.Write);
						this.m_ak.Seek(0L, SeekOrigin.Begin);
						this.m_ak.CopyTo(destination);
					}
					catch (Exception ex)
					{
						if (!this.m_v)
						{
							b($"文件写入失败: {ex.Message}", Color.Red);
						}
						t();
					}
					finally
					{
						this.m_ak.Close();
						this.m_ak = null;
					}
					if (!this.m_v)
					{
						b($"文件数据块下载完成: {this.m_ag}", Color.Green);
					}
					if (fw.InvokeRequired)
					{
						fw.Invoke((Action)delegate
						{
							fw.Value = 100;
						});
					}
					else
					{
						fw.Value = 100;
					}
				}
				string item = global::p.a(global::p.a('R'));
				cp.Enqueue(item);
				return;
			}
			if ((int)A_0 != this.m_ai)
			{
				b($"下载地址错误: 预期{this.m_ai}，实际{A_0}", Color.Red);
				t();
				return;
			}
			this.m_ak.Write(A_1, 0, A_1.Length);
			this.m_al = Utils.CalculateCRC32(this.m_al, A_1);
			this.m_ai += A_1.Length;
			int a2 = 0;
			if (this.m_ah > 0)
			{
				a2 = (int)((double)this.m_ai * 100.0 / (double)this.m_ah);
				a2 = Math.Min(a2, 100);
			}
			else
			{
				a2 = (int)(this.m_ai * 100) / 120 % 100;
			}
			if (fw.InvokeRequired)
			{
				fw.Invoke((Action)delegate
				{
					fw.Value = a2;
				});
			}
			else
			{
				fw.Value = a2;
			}
			string item2 = global::p.a(global::p.a('R', this.m_ai, (ushort)120));
			cp.Enqueue(item2);
		}
		catch (Exception ex2)
		{
			if (!this.m_v)
			{
				b($"下载数据处理错误: {ex2.Message}", Color.Red);
			}
			this.m_aj++;
			if (this.m_aj < 3)
			{
				if (!this.m_v)
				{
					b($"重试下载数据块 ({this.m_aj}/3)...", Color.Orange);
				}
				string item3 = global::p.a(global::p.a('R', this.m_ai, (ushort)120));
				cp.Enqueue(item3);
			}
			else
			{
				if (!this.m_v)
				{
					b("下载失败: 超过最大重试次数", Color.Red);
				}
				t();
			}
		}
	}

	protected override void Dispose(bool disposing)
	{
		if (disposing && dk != null)
		{
			dk.Dispose();
		}
		if (disposing && this.m_ae != null)
		{
			this.m_ae.Dispose();
			this.m_ae = null;
		}
		base.Dispose(disposing);
	}

	private void s()
	{
		dk = new Container();
		ComponentResourceManager componentResourceManager = new ComponentResourceManager(typeof(global::f));
		DataGridViewCellStyle dataGridViewCellStyle = new DataGridViewCellStyle();
		ChartArea chartArea = new ChartArea();
		Legend legend = new Legend();
		Series series = new Series();
		dl = new MenuStrip();
		ev = new ToolStripMenuItem();
		e0 = new ToolStripMenuItem();
		e1 = new ToolStripMenuItem();
		e2 = new ToolStripMenuItem();
		e3 = new ToolStripMenuItem();
		ew = new ToolStripMenuItem();
		ey = new ToolStripMenuItem();
		ez = new ToolStripMenuItem();
		hf = new ToolStripMenuItem();
		hk = new ToolStripMenuItem();
		hq = new ToolStripMenuItem();
		hr = new ToolStripMenuItem();
		ej = new ToolStripMenuItem();
		fc = new ToolStripMenuItem();
		fd = new ToolStripMenuItem();
		fe = new ToolStripMenuItem();
		er = new ToolStripMenuItem();
		ex = new ToolStripMenuItem();
		et = new ToolStripMenuItem();
		eu = new ToolStripMenuItem();
		es = new ToolStripMenuItem();
		dm = new DataGridView();
		dn = new Button();
		@do = new ToolStrip();
		dp = new PictureBox();
		dq = new PictureBox();
		dr = new PictureBox();
		ds = new Label();
		dt = new Label();
		du = new Label();
		dv = new Panel();
		dw = new TextBox();
		dz = new ComboBox();
		dx = new Button();
		dy = new Button();
		d0 = new e();
		ei = new TabPage();
		hl = new Button();
		hi = new Button();
		hj = new TextBox();
		e9 = new Panel();
		fb = new Button();
		fa = new RichTextBox();
		eq = new TreeView();
		d2 = new TabPage();
		gs = new GroupBox();
		gd = new ComboBox();
		d9 = new Label();
		ea = new NumericUpDown();
		ff = new GroupBox();
		em = new TextBox();
		ek = new CheckBox();
		eb = new Button();
		fg = new GroupBox();
		en = new MaskedTextBox();
		fh = new Label();
		el = new CheckBox();
		ec = new Button();
		ed = new Button();
		fi = new GroupBox();
		g5 = new RichTextBox();
		eo = new GroupBox();
		d4 = new DataGridView();
		e7 = new DataGridViewTextBoxColumn();
		e8 = new DataGridViewTextBoxColumn();
		ep = new GroupBox();
		d5 = new DataGridView();
		d1 = new TabPage();
		gr = new GroupBox();
		gp = new Label();
		gj = new CheckBox();
		gq = new Label();
		gk = new CheckBox();
		gn = new TextBox();
		gg = new NumericUpDown();
		gf = new NumericUpDown();
		gm = new TextBox();
		go = new CheckBox();
		gl = new TextBox();
		gi = new GroupBox();
		ge = new ComboBox();
		gh = new CheckBox();
		ee = new Chart();
		fj = new TabPage();
		g3 = new Label();
		g4 = new NumericUpDown();
		f9 = new Button();
		f2 = new Button();
		f1 = new Button();
		f5 = new Label();
		f3 = new Button();
		f0 = new DataGridView();
		f4 = new Button();
		fk = new TabPage();
		fw = new ProgressBar();
		fx = new Label();
		fz = new Label();
		fl = new GroupBox();
		fm = new ListView();
		fn = new ColumnHeader();
		fo = new ColumnHeader();
		fp = new ColumnHeader();
		fq = new Button();
		fr = new Button();
		fs = new Button();
		fu = new Button();
		fv = new Button();
		ft = new GroupBox();
		fy = new RichTextBox();
		e6 = new DataGridView();
		d3 = new StatusStrip();
		ef = new ToolStripStatusLabel();
		he = new ToolStripStatusLabel();
		eg = new ToolStripStatusLabel();
		eh = new ToolStripStatusLabel();
		d6 = new ContextMenuStrip(dk);
		gb = new ToolStripMenuItem();
		f8 = new ToolStripMenuItem();
		d7 = new ToolStripMenuItem();
		d8 = new ToolStripMenuItem();
		ga = new ToolStripSeparator();
		f6 = new ToolStripMenuItem();
		hm = new ToolStripMenuItem();
		hn = new ToolStripMenuItem();
		e4 = new SaveFileDialog();
		e5 = new OpenFileDialog();
		f7 = new OpenFileDialog();
		gc = new BackgroundWorker();
		gt = new ToolTip(dk);
		gu = new ContextMenuStrip(dk);
		gw = new ToolStripMenuItem();
		gx = new ToolStripMenuItem();
		gy = new ToolStripMenuItem();
		gz = new ToolStripMenuItem();
		gv = new ToolStripMenuItem();
		g0 = new ToolStripMenuItem();
		g1 = new ToolStripMenuItem();
		g2 = new ToolStripMenuItem();
		hb = new ToolStripMenuItem();
		hc = new ToolStripMenuItem();
		g6 = new ContextMenuStrip(dk);
		g7 = new ToolStripMenuItem();
		g8 = new ToolStripMenuItem();
		g9 = new ToolStripMenuItem();
		ha = new ToolStripMenuItem();
		hd = new ToolStripMenuItem();
		hg = new Button();
		hh = new Button();
		ho = new global::b();
		hp = new ListView();
		dl.SuspendLayout();
		((ISupportInitialize)dm).BeginInit();
		((ISupportInitialize)dp).BeginInit();
		((ISupportInitialize)dq).BeginInit();
		((ISupportInitialize)dr).BeginInit();
		dv.SuspendLayout();
		d0.SuspendLayout();
		ei.SuspendLayout();
		e9.SuspendLayout();
		d2.SuspendLayout();
		gs.SuspendLayout();
		((ISupportInitialize)ea).BeginInit();
		ff.SuspendLayout();
		fg.SuspendLayout();
		fi.SuspendLayout();
		eo.SuspendLayout();
		((ISupportInitialize)d4).BeginInit();
		ep.SuspendLayout();
		((ISupportInitialize)d5).BeginInit();
		d1.SuspendLayout();
		gr.SuspendLayout();
		((ISupportInitialize)gg).BeginInit();
		((ISupportInitialize)gf).BeginInit();
		gi.SuspendLayout();
		((ISupportInitialize)ee).BeginInit();
		fj.SuspendLayout();
		((ISupportInitialize)g4).BeginInit();
		((ISupportInitialize)f0).BeginInit();
		fk.SuspendLayout();
		fl.SuspendLayout();
		ft.SuspendLayout();
		((ISupportInitialize)e6).BeginInit();
		d3.SuspendLayout();
		d6.SuspendLayout();
		gu.SuspendLayout();
		g6.SuspendLayout();
		SuspendLayout();
		dl.ImageScalingSize = new Size(32, 32);
		dl.Items.AddRange(new ToolStripItem[5] { ev, ew, ej, fc, er });
		dl.Location = new Point(0, 0);
		dl.Name = "menuStrip1";
		dl.Size = new Size(962, 25);
		dl.TabIndex = 1;
		dl.Text = "menuStrip1";
		ev.DisplayStyle = ToolStripItemDisplayStyle.Text;
		ev.DropDownItems.AddRange(new ToolStripItem[4] { e0, e1, e2, e3 });
		ev.Name = "ToolStripMenuItem_Project";
		ev.Size = new Size(44, 21);
		ev.Text = "工程";
		e0.Name = "ToolStripMenuItem_OpenProject";
		e0.Size = new Size(208, 22);
		e0.Text = "打开1个已有的工程文件";
		e0.Click += am;
		e1.Name = "ToolStripMenuItem_NewProject";
		e1.Size = new Size(208, 22);
		e1.Text = "新建工程文件";
		e1.Click += al;
		e2.Name = "ToolStripMenuItem_SaveProject";
		e2.Size = new Size(208, 22);
		e2.Text = "保存现在使用的工程文件";
		e2.Click += ao;
		e3.Name = "ToolStripMenuItem_CloseProject";
		e3.Size = new Size(208, 22);
		e3.Text = "关闭现在使用的工程文件";
		e3.Click += an;
		ew.DisplayStyle = ToolStripItemDisplayStyle.Text;
		ew.DropDownItems.AddRange(new ToolStripItem[6] { ey, ez, hf, hk, hq, hr });
		ew.Name = "ToolStripMenuItem_LowerPC";
		ew.Size = new Size(44, 21);
		ew.Text = "设备";
		ey.Image = (Image)componentResourceManager.GetObject("ToolStripMenuItem_NewLowerPC.Image");
		ey.Name = "ToolStripMenuItem_NewLowerPC";
		ey.Size = new Size(256, 22);
		ey.Text = "添加新的下位机";
		ey.Click += g;
		ez.Image = (Image)componentResourceManager.GetObject("ToolStripMenuItem_RemoveLowerPC.Image");
		ez.Name = "ToolStripMenuItem_RemoveLowerPC";
		ez.Size = new Size(256, 22);
		ez.Text = "删除选中的下位机";
		ez.Click += f;
		hf.Name = "toolStripMenuItem_exportcmdlist";
		hf.Size = new Size(256, 22);
		hf.Text = "导出选中下位机的通讯命令列表";
		hf.Click += p;
		hk.Name = "toolStripMenuItem_exportcmdscript";
		hk.Size = new Size(256, 22);
		hk.Text = "导出选中下位机的设置命令为脚本";
		hk.Click += o;
		hq.Name = "toolStripMenuItem_updatesavedvalue";
		hq.Size = new Size(256, 22);
		hq.Text = "Update Saved Value";
		hq.Click += n;
		hr.Name = "toolStripMenuItem_exportsavedvalue";
		hr.Size = new Size(256, 22);
		hr.Text = "Export Saved Value";
		hr.Click += m;
		ej.DisplayStyle = ToolStripItemDisplayStyle.Text;
		ej.Name = "toolStripMenuItem_Option";
		ej.Size = new Size(44, 21);
		ej.Text = "设置";
		ej.Click += ap;
		fc.DisplayStyle = ToolStripItemDisplayStyle.Text;
		fc.DropDownItems.AddRange(new ToolStripItem[2] { fd, fe });
		fc.Name = "ToolStripMenuItem_Language";
		fc.Size = new Size(44, 21);
		fc.Text = "语言";
		fd.Name = "zhCNToolStripMenuItem";
		fd.Size = new Size(124, 22);
		fd.Text = "简体中文";
		fd.Click += ad;
		fe.Name = "englishToolStripMenuItem";
		fe.Size = new Size(124, 22);
		fe.Text = "English";
		fe.Click += ac;
		er.DisplayStyle = ToolStripItemDisplayStyle.Text;
		er.DropDownItems.AddRange(new ToolStripItem[4] { ex, et, eu, es });
		er.Name = "toolStripMenuItem_Help";
		er.Size = new Size(44, 21);
		er.Text = "帮助";
		ex.Name = "toolStripMenuItem_HelpFile";
		ex.Size = new Size(184, 22);
		ex.Text = "打开帮助文件";
		ex.Click += aq;
		et.Name = "toolStripMenuItem_Website";
		et.Size = new Size(184, 22);
		et.Text = "访问我们的网站";
		et.Click += ar;
		eu.Name = "toolStripMenuItem_Taobao";
		eu.Size = new Size(184, 22);
		eu.Text = "访问我们的淘宝网店";
		eu.Click += at;
		es.Name = "toolStripMenuItem_AboutSoftware";
		es.Size = new Size(184, 22);
		es.Text = "关于软件";
		es.Click += @as;
		dm.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
		dm.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
		dm.Location = new Point(238, 3);
		dm.Name = "dataGridView_Display";
		dataGridViewCellStyle.Font = new Font("微软雅黑", 9f, FontStyle.Regular, GraphicsUnit.Point, 134);
		dm.RowsDefaultCellStyle = dataGridViewCellStyle;
		dm.RowTemplate.DefaultCellStyle.Font = new Font("微软雅黑", 10.125f, FontStyle.Regular, GraphicsUnit.Point, 134);
		dm.RowTemplate.Height = 23;
		dm.ShowCellToolTips = false;
		dm.Size = new Size(408, 542);
		dm.TabIndex = 3;
		dm.CellMouseClick += b;
		dm.CellPainting += a;
		dn.BackColor = SystemColors.Control;
		dn.Font = new Font("宋体", 18f);
		dn.Location = new Point(480, 28);
		dn.Name = "button_connect";
		dn.Size = new Size(156, 43);
		dn.TabIndex = 0;
		dn.Text = "连接";
		dn.UseVisualStyleBackColor = false;
		dn.Click += bo;
		@do.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		@do.Dock = DockStyle.None;
		@do.ImageScalingSize = new Size(32, 32);
		@do.LayoutStyle = ToolStripLayoutStyle.VerticalStackWithOverflow;
		@do.Location = new Point(840, 3);
		@do.Name = "toolStrip_CustomButtons";
		@do.ShowItemToolTips = false;
		@do.Size = new Size(26, 111);
		@do.TabIndex = 10;
		@do.Text = "toolStrip3";
		@do.ItemClicked += a;
		dp.Location = new Point(687, 26);
		dp.Name = "pictureBox_sw";
		dp.Size = new Size(30, 30);
		dp.SizeMode = PictureBoxSizeMode.StretchImage;
		dp.TabIndex = 13;
		dp.TabStop = false;
		dq.Location = new Point(765, 26);
		dq.Name = "pictureBox_err";
		dq.Size = new Size(30, 30);
		dq.SizeMode = PictureBoxSizeMode.StretchImage;
		dq.TabIndex = 14;
		dq.TabStop = false;
		dq.MouseEnter += aw;
		dr.Location = new Point(843, 26);
		dr.Name = "pictureBox_con";
		dr.Size = new Size(30, 30);
		dr.SizeMode = PictureBoxSizeMode.StretchImage;
		dr.TabIndex = 15;
		dr.TabStop = false;
		ds.AutoSize = true;
		ds.Location = new Point(683, 59);
		ds.Name = "label_led_sw";
		ds.Size = new Size(41, 12);
		ds.TabIndex = 16;
		ds.Text = "输出灯";
		dt.AutoSize = true;
		dt.Location = new Point(761, 59);
		dt.Name = "label_led_err";
		dt.Size = new Size(41, 12);
		dt.TabIndex = 17;
		dt.Text = "错误灯";
		du.AutoSize = true;
		du.Location = new Point(839, 59);
		du.Name = "label_led_con";
		du.Size = new Size(41, 12);
		du.TabIndex = 18;
		du.Text = "连接灯";
		dv.BackColor = Color.LightGray;
		dv.BorderStyle = BorderStyle.FixedSingle;
		dv.Controls.Add(dw);
		dv.Controls.Add(dz);
		dv.Controls.Add(dx);
		dv.Controls.Add(dy);
		dv.ForeColor = SystemColors.ControlText;
		dv.Location = new Point(804, 74);
		dv.Name = "panel_inputparam";
		dv.Size = new Size(259, 85);
		dv.TabIndex = 19;
		dv.Visible = false;
		dw.Font = new Font("微软雅黑", 15f, FontStyle.Regular, GraphicsUnit.Point, 134);
		dw.Location = new Point(0, 0);
		dw.MinimumSize = new Size(4, 17);
		dw.Multiline = true;
		dw.Name = "textBox_inputparam";
		dw.Size = new Size(257, 32);
		dw.TabIndex = 0;
		dw.KeyDown += b;
		dz.Dock = DockStyle.Fill;
		dz.DrawMode = DrawMode.OwnerDrawFixed;
		dz.DropDownStyle = ComboBoxStyle.DropDownList;
		dz.Font = new Font("微软雅黑", 15f, FontStyle.Regular, GraphicsUnit.Point, 134);
		dz.FormattingEnabled = true;
		dz.Location = new Point(0, 0);
		dz.Name = "comboBox_inputparam";
		dz.Size = new Size(257, 35);
		dz.TabIndex = 3;
		dz.DrawItem += a;
		dz.DropDownClosed += bl;
		dz.KeyDown += a;
		dx.Font = new Font("宋体", 15f, FontStyle.Regular, GraphicsUnit.Point, 134);
		dx.Location = new Point(140, 42);
		dx.Name = "button_inputcancel";
		dx.Size = new Size(114, 38);
		dx.TabIndex = 2;
		dx.Text = "取消";
		dx.UseVisualStyleBackColor = true;
		dx.Click += bm;
		dy.Font = new Font("宋体", 15f, FontStyle.Regular, GraphicsUnit.Point, 134);
		dy.Location = new Point(3, 42);
		dy.Name = "button_inputok";
		dy.Size = new Size(111, 38);
		dy.TabIndex = 1;
		dy.Text = "确定";
		dy.UseVisualStyleBackColor = true;
		dy.Click += bn;
		d0.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
		d0.Controls.Add(ei);
		d0.Controls.Add(d2);
		d0.Controls.Add(d1);
		d0.Controls.Add(fj);
		d0.Controls.Add(fk);
		d0.Location = new Point(201, 91);
		d0.Margin = new Padding(4);
		d0.Multiline = true;
		d0.Name = "tabControl_data";
		d0.RightToLeft = RightToLeft.No;
		d0.SelectedIndex = 0;
		d0.Size = new Size(878, 574);
		d0.TabIndex = 21;
		d0.SelectedIndexChanged += bk;
		ei.BackColor = SystemColors.Control;
		ei.Controls.Add(hl);
		ei.Controls.Add(hi);
		ei.Controls.Add(hj);
		ei.Controls.Add(e9);
		ei.Controls.Add(dv);
		ei.Controls.Add(eq);
		ei.Controls.Add(dm);
		ei.Controls.Add(@do);
		ei.Font = new Font("Arial", 9f, FontStyle.Regular, GraphicsUnit.Point, 0);
		ei.Location = new Point(4, 22);
		ei.Name = "tabPage_Param";
		ei.Padding = new Padding(3);
		ei.Size = new Size(870, 548);
		ei.TabIndex = 3;
		ei.Text = " Parameters";
		hl.Anchor = AnchorStyles.Bottom | AnchorStyles.Left;
		hl.Location = new Point(4, 519);
		hl.Name = "button_markparam";
		hl.Size = new Size(226, 23);
		hl.TabIndex = 35;
		hl.Text = "Display Marked Parameters";
		hl.UseVisualStyleBackColor = true;
		hl.Click += h;
		hi.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
		hi.Location = new Point(665, 519);
		hi.Name = "button_cmd";
		hi.Size = new Size(187, 23);
		hi.TabIndex = 34;
		hi.Text = "Send CMD";
		hi.UseVisualStyleBackColor = true;
		hi.Click += k;
		hj.Anchor = AnchorStyles.Bottom | AnchorStyles.Right;
		hj.Location = new Point(665, 486);
		hj.Name = "textBox_cmd";
		hj.Size = new Size(187, 21);
		hj.TabIndex = 33;
		e9.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
		e9.BackColor = SystemColors.Info;
		e9.BorderStyle = BorderStyle.FixedSingle;
		e9.Controls.Add(fb);
		e9.Controls.Add(fa);
		e9.Location = new Point(248, 27);
		e9.Name = "panel_help";
		e9.Size = new Size(382, 504);
		e9.TabIndex = 32;
		fb.AutoSize = true;
		fb.Location = new Point(4, 3);
		fb.Name = "button_closehelp";
		fb.Size = new Size(166, 37);
		fb.TabIndex = 32;
		fb.Text = "关闭帮助信息";
		fb.UseVisualStyleBackColor = true;
		fb.Click += ae;
		fa.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
		fa.Font = new Font("宋体", 11.25f, FontStyle.Regular, GraphicsUnit.Point, 134);
		fa.Location = new Point(4, 46);
		fa.Name = "richTextBox_help";
		fa.ReadOnly = true;
		fa.Size = new Size(373, 452);
		fa.TabIndex = 31;
		fa.Text = "";
		eq.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left;
		eq.Font = new Font("宋体", 11.25f, FontStyle.Regular, GraphicsUnit.Point, 134);
		eq.FullRowSelect = true;
		eq.HideSelection = false;
		eq.ItemHeight = 25;
		eq.Location = new Point(3, 3);
		eq.Name = "treeView_folder";
		eq.Size = new Size(229, 504);
		eq.TabIndex = 30;
		eq.AfterSelect += a;
		d2.BackColor = SystemColors.Control;
		d2.Controls.Add(gs);
		d2.Controls.Add(ff);
		d2.Controls.Add(eb);
		d2.Controls.Add(fg);
		d2.Controls.Add(ec);
		d2.Controls.Add(ed);
		d2.Controls.Add(fi);
		d2.Controls.Add(eo);
		d2.Controls.Add(ep);
		d2.Font = new Font("宋体", 9f, FontStyle.Regular, GraphicsUnit.Point, 134);
		d2.Location = new Point(4, 22);
		d2.Name = "tabPage_datarecord";
		d2.Padding = new Padding(3);
		d2.Size = new Size(870, 548);
		d2.TabIndex = 1;
		d2.Text = " Data Record";
		gs.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		gs.Controls.Add(gd);
		gs.Controls.Add(d9);
		gs.Controls.Add(ea);
		gs.Location = new Point(635, 116);
		gs.Name = "groupBox_recordsetting";
		gs.Size = new Size(187, 86);
		gs.TabIndex = 12;
		gs.TabStop = false;
		gs.Text = "记录设置";
		gd.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		gd.FormattingEnabled = true;
		gd.Location = new Point(6, 20);
		gd.Name = "comboBox_DataRecord";
		gd.Size = new Size(168, 20);
		gd.TabIndex = 10;
		gd.SelectedIndexChanged += ax;
		d9.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		d9.AutoSize = true;
		d9.Location = new Point(4, 52);
		d9.Name = "label_recordinterval";
		d9.Size = new Size(101, 12);
		d9.TabIndex = 2;
		d9.Text = "记录间隔（秒）：";
		ea.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		ea.DecimalPlaces = 1;
		ea.Increment = new decimal(new int[4] { 1, 0, 0, 65536 });
		ea.Location = new Point(135, 50);
		ea.Maximum = new decimal(new int[4] { 1000, 0, 0, 0 });
		ea.Minimum = new decimal(new int[4] { 1, 0, 0, 65536 });
		ea.Name = "numericUpDown_recordinterval";
		ea.Size = new Size(39, 21);
		ea.TabIndex = 1;
		ea.Value = new decimal(new int[4] { 1, 0, 0, 0 });
		ea.ValueChanged += a0;
		ff.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		ff.Controls.Add(em);
		ff.Controls.Add(ek);
		ff.Location = new Point(633, 424);
		ff.Name = "groupBox_autosave";
		ff.Size = new Size(189, 79);
		ff.TabIndex = 5;
		ff.TabStop = false;
		ff.Text = "自动保存记录";
		em.Location = new Point(8, 42);
		em.Name = "textBox_autosavefilename";
		em.ReadOnly = true;
		em.Size = new Size(175, 21);
		em.TabIndex = 9;
		em.Text = "*.txt";
		em.TextAlign = HorizontalAlignment.Right;
		ek.AutoSize = true;
		ek.Location = new Point(8, 20);
		ek.Name = "checkBox_AutoSaveDataRecord";
		ek.Size = new Size(72, 16);
		ek.TabIndex = 6;
		ek.Text = "自动保存";
		ek.UseVisualStyleBackColor = true;
		ek.CheckedChanged += ay;
		eb.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		eb.Location = new Point(658, 384);
		eb.Name = "button_savedatarecord";
		eb.Size = new Size(141, 30);
		eb.TabIndex = 4;
		eb.Text = "导出记录到TXT文件";
		eb.UseVisualStyleBackColor = true;
		eb.Click += az;
		fg.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		fg.Controls.Add(en);
		fg.Controls.Add(fh);
		fg.Controls.Add(el);
		fg.Location = new Point(635, 299);
		fg.Name = "groupBox_memoryprotect";
		fg.Size = new Size(187, 72);
		fg.TabIndex = 9;
		fg.TabStop = false;
		fg.Text = "内存保护";
		en.InsertKeyMode = InsertKeyMode.Insert;
		en.Location = new Point(102, 39);
		en.Mask = "9999999";
		en.Name = "maskedTextBox_MemorySizeLimit";
		en.PromptChar = ' ';
		en.Size = new Size(64, 21);
		en.TabIndex = 9;
		en.Text = "500000";
		en.TextAlign = HorizontalAlignment.Right;
		en.MaskInputRejected += a;
		fh.AutoSize = true;
		fh.Location = new Point(6, 42);
		fh.Name = "label_memorylimit";
		fh.Size = new Size(89, 12);
		fh.TabIndex = 8;
		fh.Text = "数据总量限制：";
		el.AutoSize = true;
		el.Checked = true;
		el.CheckState = CheckState.Checked;
		el.Location = new Point(5, 20);
		el.Name = "checkBox_AutoMemoryProtect";
		el.Size = new Size(72, 16);
		el.TabIndex = 3;
		el.Text = "内存保护";
		el.UseVisualStyleBackColor = true;
		ec.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		ec.Enabled = false;
		ec.Location = new Point(658, 214);
		ec.Name = "button_deleteselection";
		ec.Size = new Size(141, 30);
		ec.TabIndex = 4;
		ec.Text = "删除选定记录";
		ec.UseVisualStyleBackColor = true;
		ec.Click += a2;
		ed.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		ed.Location = new Point(658, 253);
		ed.Name = "button_cleardatarecord";
		ed.Size = new Size(141, 30);
		ed.TabIndex = 3;
		ed.Text = "清空现有的数据记录";
		ed.UseVisualStyleBackColor = true;
		ed.Click += a1;
		fi.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		fi.Controls.Add(g5);
		fi.Location = new Point(635, 6);
		fi.Name = "groupBox_paramselecthelp";
		fi.Size = new Size(187, 101);
		fi.TabIndex = 1;
		fi.TabStop = false;
		fi.Text = "About parameters";
		g5.BorderStyle = BorderStyle.None;
		g5.Location = new Point(6, 18);
		g5.Margin = new Padding(2);
		g5.Name = "richTextBox_paramselecthelp";
		g5.ReadOnly = true;
		g5.Size = new Size(177, 78);
		g5.TabIndex = 1;
		g5.Text = "";
		eo.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
		eo.Controls.Add(d4);
		eo.Location = new Point(6, 205);
		eo.Name = "dgv_datarecord_groupbox";
		eo.Size = new Size(623, 329);
		eo.TabIndex = 11;
		eo.TabStop = false;
		eo.Text = "数据历史记录";
		d4.AllowUserToOrderColumns = true;
		d4.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
		d4.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.DisplayedCells;
		d4.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
		d4.Columns.AddRange(e7, e8);
		d4.Location = new Point(6, 15);
		d4.Name = "dataGridView_datarecord";
		d4.RowTemplate.Height = 23;
		d4.SelectionMode = DataGridViewSelectionMode.FullRowSelect;
		d4.Size = new Size(608, 310);
		d4.TabIndex = 0;
		d4.CellValueNeeded += a;
		e7.HeaderText = "序号";
		e7.Name = "index";
		e7.Width = 54;
		e8.DataPropertyName = "recordtime";
		e8.HeaderText = "记录时间";
		e8.Name = "recordtime";
		e8.Width = 78;
		ep.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
		ep.Controls.Add(d5);
		ep.Location = new Point(6, 6);
		ep.Name = "dgv_datasetting_groupbox";
		ep.Size = new Size(623, 196);
		ep.TabIndex = 10;
		ep.TabStop = false;
		ep.Text = "参数选择";
		d5.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
		d5.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
		d5.Location = new Point(6, 15);
		d5.Name = "dataGridView_datasetting";
		d5.RowTemplate.Height = 23;
		d5.Size = new Size(608, 178);
		d5.TabIndex = 0;
		d5.CellMouseClick += c;
		d1.BackColor = SystemColors.Control;
		d1.Controls.Add(gr);
		d1.Controls.Add(gi);
		d1.Controls.Add(gh);
		d1.Controls.Add(ee);
		d1.Location = new Point(4, 22);
		d1.Name = "tabPage_datacurve";
		d1.Padding = new Padding(3);
		d1.Size = new Size(870, 548);
		d1.TabIndex = 0;
		d1.Text = " Data Curve ";
		gr.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		gr.Controls.Add(gp);
		gr.Controls.Add(gj);
		gr.Controls.Add(gq);
		gr.Controls.Add(gk);
		gr.Controls.Add(gn);
		gr.Controls.Add(gg);
		gr.Controls.Add(gf);
		gr.Controls.Add(gm);
		gr.Controls.Add(go);
		gr.Controls.Add(gl);
		gr.Location = new Point(603, 140);
		gr.Name = "groupBox_XaxisOption";
		gr.Size = new Size(219, 187);
		gr.TabIndex = 8;
		gr.TabStop = false;
		gr.Text = "横轴设置（横轴为数字时设置有效）";
		gp.AutoSize = true;
		gp.Location = new Point(8, 62);
		gp.Name = "label_maxgridcount";
		gp.Size = new Size(65, 12);
		gp.TabIndex = 12;
		gp.Text = "最大网格数";
		gj.AutoSize = true;
		gj.Location = new Point(10, 121);
		gj.Name = "checkBox_CurveFixedStart";
		gj.Size = new Size(72, 16);
		gj.TabIndex = 6;
		gj.Text = "固定起点";
		gj.UseVisualStyleBackColor = true;
		gj.CheckedChanged += ai;
		gq.AutoSize = true;
		gq.Location = new Point(8, 34);
		gq.Name = "label_mingridcount";
		gq.Size = new Size(65, 12);
		gq.TabIndex = 11;
		gq.Text = "最小网格数";
		gk.AutoSize = true;
		gk.Location = new Point(11, 94);
		gk.Name = "checkBox_CurveFixedInterval";
		gk.Size = new Size(72, 16);
		gk.TabIndex = 5;
		gk.Text = "固定间隔";
		gk.UseVisualStyleBackColor = true;
		gn.Location = new Point(135, 147);
		gn.Name = "textBox_CurveFixedEnd";
		gn.Size = new Size(74, 21);
		gn.TabIndex = 10;
		gn.Text = "100";
		gg.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		gg.Location = new Point(135, 32);
		gg.Minimum = new decimal(new int[4] { 1, 0, 0, 0 });
		gg.Name = "numericUpDown_minXAxisGridNum";
		gg.Size = new Size(74, 21);
		gg.TabIndex = 2;
		gg.Value = new decimal(new int[4] { 10, 0, 0, 0 });
		gf.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		gf.Location = new Point(135, 60);
		gf.Minimum = new decimal(new int[4] { 1, 0, 0, 0 });
		gf.Name = "numericUpDown_maxXAxisGridNum";
		gf.Size = new Size(74, 21);
		gf.TabIndex = 3;
		gf.Value = new decimal(new int[4] { 20, 0, 0, 0 });
		gm.Location = new Point(135, 93);
		gm.Name = "textBox_CurveFixedInterval";
		gm.Size = new Size(74, 21);
		gm.TabIndex = 7;
		gm.Text = "1";
		go.AutoSize = true;
		go.Location = new Point(10, 148);
		go.Name = "checkBox_CurveFixedEnd";
		go.Size = new Size(72, 16);
		go.TabIndex = 9;
		go.Text = "固定终点";
		go.UseVisualStyleBackColor = true;
		go.CheckedChanged += ah;
		gl.Location = new Point(135, 120);
		gl.Name = "textBox_CurveFixedStart";
		gl.Size = new Size(74, 21);
		gl.TabIndex = 8;
		gl.Text = "0";
		gi.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		gi.Controls.Add(ge);
		gi.Location = new Point(603, 51);
		gi.Name = "groupBox_XaxisSelect";
		gi.Size = new Size(219, 68);
		gi.TabIndex = 7;
		gi.TabStop = false;
		gi.Text = "横轴参数选择";
		ge.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		ge.FormattingEnabled = true;
		ge.Location = new Point(11, 29);
		ge.Name = "comboBox_XAxis";
		ge.Size = new Size(198, 20);
		ge.TabIndex = 1;
		ge.SelectedIndexChanged += ak;
		gh.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		gh.Checked = true;
		gh.CheckState = CheckState.Checked;
		gh.Location = new Point(626, 6);
		gh.Name = "checkBox_SeperateCurve";
		gh.Size = new Size(196, 24);
		gh.TabIndex = 4;
		gh.Text = "单独显示每个下位机的曲线";
		gh.UseVisualStyleBackColor = true;
		gh.CheckedChanged += aj;
		ee.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
		ee.AntiAliasing = AntiAliasingStyles.None;
		chartArea.Name = "ChartArea1";
		ee.ChartAreas.Add(chartArea);
		ee.IsSoftShadows = false;
		legend.Name = "Legend1";
		ee.Legends.Add(legend);
		ee.Location = new Point(3, 3);
		ee.Name = "chart1";
		series.ChartArea = "ChartArea1";
		series.Legend = "Legend1";
		series.Name = "Series1";
		ee.Series.Add(series);
		ee.Size = new Size(594, 536);
		ee.TabIndex = 0;
		ee.Text = "chart1";
		ee.TextAntiAliasingQuality = TextAntiAliasingQuality.Normal;
		ee.GetToolTipText += a;
		ee.AxisViewChanged += a;
		ee.MouseClick += c;
		fj.BackColor = SystemColors.Control;
		fj.Controls.Add(g3);
		fj.Controls.Add(g4);
		fj.Controls.Add(f9);
		fj.Controls.Add(f2);
		fj.Controls.Add(f1);
		fj.Controls.Add(f5);
		fj.Controls.Add(f3);
		fj.Controls.Add(f0);
		fj.Controls.Add(f4);
		fj.Location = new Point(4, 22);
		fj.Name = "tabPage_stepprogram";
		fj.Size = new Size(870, 548);
		fj.TabIndex = 4;
		fj.Text = "Script";
		g3.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		g3.AutoSize = true;
		g3.Location = new Point(664, 269);
		g3.Margin = new Padding(2, 0, 2, 0);
		g3.Name = "label_scriptInterval";
		g3.Size = new Size(53, 12);
		g3.TabIndex = 10;
		g3.Text = "Interval";
		g4.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		g4.DecimalPlaces = 1;
		g4.Increment = new decimal(new int[4] { 1, 0, 0, 65536 });
		g4.Location = new Point(759, 268);
		g4.Margin = new Padding(2);
		g4.Maximum = new decimal(new int[4] { 10000, 0, 0, 0 });
		g4.Minimum = new decimal(new int[4] { 2, 0, 0, 65536 });
		g4.Name = "numericUpDown_scriptInterval";
		g4.Size = new Size(60, 21);
		g4.TabIndex = 9;
		g4.Value = new decimal(new int[4] { 5, 0, 0, 65536 });
		g4.ValueChanged += r;
		f9.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		f9.Location = new Point(659, 205);
		f9.Name = "button_newscript";
		f9.Size = new Size(160, 30);
		f9.TabIndex = 8;
		f9.Text = "新建";
		f9.UseVisualStyleBackColor = true;
		f9.Click += a5;
		f2.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		f2.Location = new Point(659, 25);
		f2.Name = "button_startscript";
		f2.Size = new Size(160, 40);
		f2.TabIndex = 2;
		f2.Text = "开始";
		f2.UseVisualStyleBackColor = true;
		f2.Click += a4;
		f1.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		f1.Location = new Point(659, 71);
		f1.Name = "button_stopscript";
		f1.Size = new Size(160, 40);
		f1.TabIndex = 3;
		f1.Text = "停止";
		f1.UseVisualStyleBackColor = true;
		f1.Click += a3;
		f5.AutoSize = true;
		f5.Location = new Point(7, 6);
		f5.Name = "label_scriptfilename";
		f5.Size = new Size(53, 12);
		f5.TabIndex = 5;
		f5.Text = "文件名称";
		f3.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		f3.Location = new Point(659, 169);
		f3.Name = "button_savescriptfile";
		f3.Size = new Size(160, 30);
		f3.TabIndex = 5;
		f3.Text = "保存";
		f3.UseVisualStyleBackColor = true;
		f3.Click += a7;
		f0.AllowDrop = true;
		f0.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
		f0.AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.AllCells;
		f0.ImeMode = ImeMode.NoControl;
		f0.Location = new Point(3, 25);
		f0.Name = "dataGridView_script";
		f0.RowHeadersWidthSizeMode = DataGridViewRowHeadersWidthSizeMode.AutoSizeToAllHeaders;
		f0.RowTemplate.Height = 23;
		f0.ShowCellToolTips = false;
		f0.Size = new Size(637, 517);
		f0.TabIndex = 0;
		f0.CellBeginEdit += a;
		f0.CellEnter += a;
		f0.CellMouseEnter += c;
		f0.CellMouseLeave += b;
		f0.CellMouseMove += a;
		f0.CellValidating += a;
		f0.RowPostPaint += a;
		f0.DragDrop += a;
		f0.DragEnter += b;
		f0.MouseDown += b;
		f0.MouseUp += a;
		f4.Anchor = AnchorStyles.Top | AnchorStyles.Right;
		f4.Location = new Point(659, 133);
		f4.Name = "button_openscriptfile";
		f4.Size = new Size(160, 30);
		f4.TabIndex = 4;
		f4.Text = "打开";
		f4.UseVisualStyleBackColor = true;
		f4.Click += a6;
		fk.BackColor = SystemColors.Control;
		fk.Controls.Add(fw);
		fk.Controls.Add(fx);
		fk.Controls.Add(fz);
		fk.Controls.Add(fl);
		fk.Controls.Add(ft);
		fk.Location = new Point(4, 22);
		fk.Margin = new Padding(4);
		fk.Name = "tabPage_filetransfer";
		fk.Padding = new Padding(4);
		fk.Size = new Size(870, 548);
		fk.TabIndex = 4;
		fk.Text = "File Transfer";
		fw.Location = new Point(111, 315);
		fw.Margin = new Padding(4);
		fw.Name = "progressBar_filetransfer";
		fw.Size = new Size(978, 29);
		fw.TabIndex = 6;
		fx.AutoSize = true;
		fx.Location = new Point(16, 320);
		fx.Margin = new Padding(4, 0, 4, 0);
		fx.Name = "label_transferstatus";
		fx.Size = new Size(113, 12);
		fx.TabIndex = 7;
		fx.Text = "Transfer Progress:";
		fz.AutoSize = true;
		fz.Location = new Point(16, 349);
		fz.Margin = new Padding(4, 0, 4, 0);
		fz.Name = "label_transferindicator";
		fz.Size = new Size(0, 12);
		fz.TabIndex = 8;
		fl.Controls.Add(fm);
		fl.Controls.Add(fq);
		fl.Controls.Add(fr);
		fl.Controls.Add(fs);
		fl.Controls.Add(fu);
		fl.Controls.Add(fv);
		fl.Location = new Point(16, 15);
		fl.Margin = new Padding(4);
		fl.Name = "groupBox_file_operations";
		fl.Padding = new Padding(4);
		fl.Size = new Size(1073, 285);
		fl.TabIndex = 0;
		fl.TabStop = false;
		fl.Text = "File List & Operations";
		fm.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
		fm.Columns.AddRange(new ColumnHeader[3] { fn, fo, fp });
		fm.FullRowSelect = true;
		fm.HideSelection = false;
		fm.Location = new Point(12, 25);
		fm.Margin = new Padding(4);
		fm.Name = "listView_filelist";
		fm.Size = new Size(932, 249);
		fm.TabIndex = 0;
		fm.UseCompatibleStateImageBehavior = false;
		fm.View = View.Details;
		fn.Text = "Filename";
		fn.Width = 450;
		fo.Text = "Size";
		fo.Width = 150;
		fp.Text = "CRC";
		fp.Width = 0;
		fq.Location = new Point(957, 25);
		fq.Margin = new Padding(4);
		fq.Name = "button_refreshfilelist";
		fq.Size = new Size(103, 38);
		fq.TabIndex = 1;
		fq.Text = "Refresh List";
		fq.UseVisualStyleBackColor = true;
		fq.Click += e;
		fr.Enabled = false;
		fr.Location = new Point(957, 71);
		fr.Margin = new Padding(4);
		fr.Name = "button_deletefile";
		fr.Size = new Size(103, 38);
		fr.TabIndex = 2;
		fr.Text = "Delete File";
		fr.UseVisualStyleBackColor = true;
		fr.Click += d;
		fs.Enabled = false;
		fs.Location = new Point(957, 117);
		fs.Margin = new Padding(4);
		fs.Name = "button_calculatecrc";
		fs.Size = new Size(103, 38);
		fs.TabIndex = 3;
		fs.Text = "Calculate CRC";
		fs.UseVisualStyleBackColor = true;
		fs.Visible = false;
		fs.Click += c;
		fu.Enabled = false;
		fu.Location = new Point(957, 163);
		fu.Margin = new Padding(4);
		fu.Name = "button_uploadfile";
		fu.Size = new Size(103, 38);
		fu.TabIndex = 4;
		fu.Text = "Upload File";
		fu.UseVisualStyleBackColor = true;
		fu.Click += b;
		fv.Enabled = false;
		fv.Location = new Point(957, 209);
		fv.Margin = new Padding(4);
		fv.Name = "button_downloadfile";
		fv.Size = new Size(103, 38);
		fv.TabIndex = 5;
		fv.Text = "Download File";
		fv.UseVisualStyleBackColor = true;
		fv.Click += a;
		ft.Controls.Add(fy);
		ft.Location = new Point(16, 370);
		ft.Margin = new Padding(4);
		ft.Name = "groupBox_log";
		ft.Padding = new Padding(4);
		ft.Size = new Size(1073, 305);
		ft.TabIndex = 9;
		ft.TabStop = false;
		ft.Text = "Transfer Log";
		fy.Location = new Point(12, 25);
		fy.Margin = new Padding(4);
		fy.Name = "richTextBox_filetransferlog";
		fy.Size = new Size(1047, 269);
		fy.TabIndex = 7;
		fy.Text = "";
		e6.ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize;
		e6.Location = new Point(896, 6);
		e6.Name = "dataGridView_faultlist";
		e6.RowTemplate.Height = 23;
		e6.Size = new Size(110, 78);
		e6.TabIndex = 33;
		e6.MouseLeave += av;
		d3.ImageScalingSize = new Size(32, 32);
		d3.Items.AddRange(new ToolStripItem[4] { ef, he, eg, eh });
		d3.Location = new Point(0, 508);
		d3.Name = "statusStrip1";
		d3.Size = new Size(962, 22);
		d3.TabIndex = 25;
		d3.Text = "    ";
		ef.AutoToolTip = true;
		ef.Font = new Font("宋体", 9f, FontStyle.Regular, GraphicsUnit.Point, 134);
		ef.Name = "toolStripStatusLabel_MSG";
		ef.Size = new Size(617, 17);
		ef.Spring = true;
		ef.TextAlign = System.Drawing.ContentAlignment.MiddleLeft;
		ef.ToolTipText = "上位机的错误日志，或者下位机对上位机命令操作结果的回复信息。";
		ef.MouseEnter += au;
		he.Name = "toolStripStatusLabel_reply";
		he.Size = new Size(0, 17);
		he.TextAlign = System.Drawing.ContentAlignment.MiddleRight;
		eg.AutoSize = false;
		eg.AutoToolTip = true;
		eg.Font = new Font("宋体", 9f, FontStyle.Regular, GraphicsUnit.Point, 134);
		eg.Name = "toolStripStatusLabel_ConnectionInfo";
		eg.RightToLeft = RightToLeft.No;
		eg.Size = new Size(330, 17);
		eg.ToolTipText = "连接状态相关信息";
		eh.Name = "toolStripStatusLabel_RecordDataCount";
		eh.RightToLeft = RightToLeft.Yes;
		eh.Size = new Size(0, 17);
		d6.ImageScalingSize = new Size(32, 32);
		d6.Items.AddRange(new ToolStripItem[8] { gb, f8, d7, d8, ga, f6, hm, hn });
		d6.Name = "contextMenuStrip_datasetting";
		d6.Size = new Size(165, 276);
		gb.Name = "toolStripMenuItem_autoupdate";
		gb.Size = new Size(164, 38);
		gb.Text = "自动更新";
		gb.Click += be;
		f8.Image = (Image)componentResourceManager.GetObject("toolStripMenuItem_alwaysupdate.Image");
		f8.Name = "toolStripMenuItem_alwaysupdate";
		f8.Size = new Size(164, 38);
		f8.Text = "始终更新";
		f8.Click += bd;
		d7.Image = (Image)componentResourceManager.GetObject("toolStripMenuItem_recorddata.Image");
		d7.Name = "toolStripMenuItem_recorddata";
		d7.Size = new Size(164, 38);
		d7.Text = "记录历史数据";
		d7.Click += bc;
		d8.Image = (Image)componentResourceManager.GetObject("toolStripMenuItem_datacurve.Image");
		d8.Name = "toolStripMenuItem_datacurve";
		d8.Size = new Size(164, 38);
		d8.Text = "绘制数据曲线";
		d8.Click += bb;
		ga.Name = "toolStripSeparator2";
		ga.Size = new Size(161, 6);
		f6.Image = (Image)componentResourceManager.GetObject("toolStripMenuItem_stepprogram.Image");
		f6.Name = "toolStripMenuItem_stepprogram";
		f6.Size = new Size(164, 38);
		f6.Text = "加入定时编程";
		f6.Click += ba;
		hm.Name = "toolStripMenuItem_mark";
		hm.Size = new Size(164, 38);
		hm.Text = "重点标记";
		hm.Click += a9;
		hn.Name = "toolStripMenuItem_unmark";
		hn.Size = new Size(164, 38);
		hn.Text = "取消标记";
		hn.Click += a8;
		e4.FileOk += b;
		e5.FileName = "openFileDialog1";
		e5.FileOk += a;
		f7.FileName = "openFileDialog1";
		f7.FileOk += c;
		gu.ImageScalingSize = new Size(32, 32);
		gu.Items.AddRange(new ToolStripItem[10] { gw, gx, gy, gz, gv, g0, g1, g2, hb, hc });
		gu.Name = "contextMenuStrip_script";
		gu.Size = new Size(194, 224);
		gw.Name = "tsmItem_scriptUp";
		gw.Size = new Size(193, 22);
		gw.Text = "Up";
		gw.Click += aa;
		gx.Name = "tsmItem_scriptDown";
		gx.Size = new Size(193, 22);
		gx.Text = "Down";
		gx.Click += z;
		gy.Name = "tsmItem_scriptCopy";
		gy.Size = new Size(193, 22);
		gy.Text = "Copy";
		gy.Click += y;
		gz.Name = "tsmItem_scriptPaste";
		gz.Size = new Size(193, 22);
		gz.Text = "Paste";
		gz.Click += x;
		gv.Name = "tsmItem_scriptRemove";
		gv.Size = new Size(193, 22);
		gv.Text = "Remove";
		gv.Click += ab;
		g0.Name = "tsmItem_scriptAddGo";
		g0.Size = new Size(193, 22);
		g0.Text = "AddGo";
		g0.Click += w;
		g1.Name = "tsmItem_scriptAddDelay";
		g1.Size = new Size(193, 22);
		g1.Text = "AddDelay";
		g1.Click += v;
		g2.Name = "tsmItem_scriptAddBlank";
		g2.Size = new Size(193, 22);
		g2.Text = "AddBlank";
		g2.Click += u;
		hb.Name = "tsmItem_scriptStartFromHere";
		hb.Size = new Size(193, 22);
		hb.Text = "Start From Here";
		hb.Click += t;
		hc.Name = "tsmItem_scriptContinueFromHere";
		hc.Size = new Size(193, 22);
		hc.Text = "Continue From Here";
		hc.Click += s;
		g6.ImageScalingSize = new Size(32, 32);
		g6.Items.AddRange(new ToolStripItem[5] { g7, g8, g9, ha, hd });
		g6.Name = "contextMenuStrip_datalevel";
		g6.Size = new Size(163, 114);
		g7.Name = "tsm_datalevel_autoupdate";
		g7.Size = new Size(162, 22);
		g7.Text = "Auto Update";
		g7.Click += bj;
		g8.Name = "tsm_datalevel_alwaysupdate";
		g8.Size = new Size(162, 22);
		g8.Text = "Always Update";
		g8.Click += bi;
		g9.Name = "tsm_datalevel_datarecord";
		g9.Size = new Size(162, 22);
		g9.Text = "Record Data";
		g9.Click += bh;
		ha.Name = "tsm_datalevel_drawcurve";
		ha.Size = new Size(162, 22);
		ha.Text = "DrawCurve";
		ha.Click += bg;
		hd.Name = "tsm_datalevel_statistics";
		hd.Size = new Size(162, 22);
		hd.Text = "Statistics";
		hd.Click += bf;
		hg.Location = new Point(6, 69);
		hg.Name = "button_adddevice";
		hg.Size = new Size(75, 25);
		hg.TabIndex = 33;
		hg.Text = "Add DEV";
		hg.UseVisualStyleBackColor = true;
		hg.Click += j;
		hh.ImageAlign = System.Drawing.ContentAlignment.MiddleLeft;
		hh.Location = new Point(102, 69);
		hh.Name = "button_removedevice";
		hh.Size = new Size(75, 25);
		hh.TabIndex = 34;
		hh.Text = "Remove DEV";
		hh.TextImageRelation = TextImageRelation.ImageBeforeText;
		hh.UseVisualStyleBackColor = true;
		hh.Click += i;
		ho.Location = new Point(6, 29);
		ho.Margin = new Padding(6);
		ho.Name = "commSelector1";
		ho.Size = new Size(352, 27);
		ho.TabIndex = 43;
		hp.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left;
		hp.Font = new Font("宋体", 11.25f);
		hp.HideSelection = false;
		hp.Location = new Point(6, 100);
		hp.MultiSelect = false;
		hp.Name = "listView_Device";
		hp.Size = new Size(171, 478);
		hp.TabIndex = 45;
		hp.UseCompatibleStateImageBehavior = false;
		hp.View = View.List;
		hp.DrawItem += a;
		hp.SelectedIndexChanged += l;
		base.AutoScaleDimensions = new SizeF(6f, 12f);
		base.AutoScaleMode = AutoScaleMode.Font;
		base.ClientSize = new Size(962, 530);
		base.Controls.Add(e6);
		base.Controls.Add(du);
		base.Controls.Add(ds);
		base.Controls.Add(dt);
		base.Controls.Add(hp);
		base.Controls.Add(ho);
		base.Controls.Add(hh);
		base.Controls.Add(hg);
		base.Controls.Add(dn);
		base.Controls.Add(dr);
		base.Controls.Add(dp);
		base.Controls.Add(dq);
		base.Controls.Add(d3);
		base.Controls.Add(d0);
		base.Controls.Add(dl);
		base.Icon = (Icon)componentResourceManager.GetObject("$this.Icon");
		base.MainMenuStrip = dl;
		MinimumSize = new Size(957, 503);
		base.Name = "Form1";
		base.StartPosition = FormStartPosition.CenterScreen;
		Text = "EasyHost -业贤科技- www.yexian.com";
		base.Activated += ag;
		base.Deactivate += af;
		base.FormClosing += a;
		base.Load += bp;
		base.Resize += q;
		dl.ResumeLayout(performLayout: false);
		dl.PerformLayout();
		((ISupportInitialize)dm).EndInit();
		((ISupportInitialize)dp).EndInit();
		((ISupportInitialize)dq).EndInit();
		((ISupportInitialize)dr).EndInit();
		dv.ResumeLayout(performLayout: false);
		dv.PerformLayout();
		d0.ResumeLayout(performLayout: false);
		ei.ResumeLayout(performLayout: false);
		ei.PerformLayout();
		e9.ResumeLayout(performLayout: false);
		e9.PerformLayout();
		d2.ResumeLayout(performLayout: false);
		gs.ResumeLayout(performLayout: false);
		gs.PerformLayout();
		((ISupportInitialize)ea).EndInit();
		ff.ResumeLayout(performLayout: false);
		ff.PerformLayout();
		fg.ResumeLayout(performLayout: false);
		fg.PerformLayout();
		fi.ResumeLayout(performLayout: false);
		eo.ResumeLayout(performLayout: false);
		((ISupportInitialize)d4).EndInit();
		ep.ResumeLayout(performLayout: false);
		((ISupportInitialize)d5).EndInit();
		d1.ResumeLayout(performLayout: false);
		gr.ResumeLayout(performLayout: false);
		gr.PerformLayout();
		((ISupportInitialize)gg).EndInit();
		((ISupportInitialize)gf).EndInit();
		gi.ResumeLayout(performLayout: false);
		((ISupportInitialize)ee).EndInit();
		fj.ResumeLayout(performLayout: false);
		fj.PerformLayout();
		((ISupportInitialize)g4).EndInit();
		((ISupportInitialize)f0).EndInit();
		fk.ResumeLayout(performLayout: false);
		fk.PerformLayout();
		fl.ResumeLayout(performLayout: false);
		ft.ResumeLayout(performLayout: false);
		((ISupportInitialize)e6).EndInit();
		d3.ResumeLayout(performLayout: false);
		d3.PerformLayout();
		d6.ResumeLayout(performLayout: false);
		gu.ResumeLayout(performLayout: false);
		g6.ResumeLayout(performLayout: false);
		ResumeLayout(performLayout: false);
		PerformLayout();
	}

	[CompilerGenerated]
	private void r()
	{
		this.m_u = global::f.a.a;
		fq.Enabled = true;
		fu.Enabled = true;
		fr.Enabled = true;
		fs.Enabled = true;
		fv.Enabled = fm.Items.Count > 0;
		if (this.m_y != null)
		{
			this.m_y();
			this.m_y = null;
		}
		v();
	}

	[CompilerGenerated]
	private void q()
	{
		this.m_u = global::f.a.a;
		fq.Enabled = true;
		fu.Enabled = true;
		fr.Enabled = true;
		fs.Enabled = true;
	}

	[CompilerGenerated]
	private void p()
	{
		av();
		aw();
		a4();
		try
		{
			eg.Text = co + q(a8());
		}
		catch
		{
		}
		az();
	}

	[CompilerGenerated]
	private void o()
	{
		if (b5 != null && b5.c() && this.m_aq.Count > 0 && gd.SelectedIndex > 0)
		{
			int num = 0;
			DataRow dataRow = this.m_j.NewRow();
			dataRow[1] = DateTime.Now;
			try
			{
				for (num = 2; num < Math.Min(this.m_aq.Count + 2, this.m_j.Columns.Count); num++)
				{
					dataRow[num] = this.m_aq[num - 2].Row["ParamValue"];
				}
			}
			catch
			{
				a("Error: when recording data！", Color.Red);
			}
			try
			{
				this.m_j.Rows.Add(dataRow);
				if (this.m_j.Rows.Count > 1)
				{
					this.m_j.Rows[this.m_j.Rows.Count - 1][0] = Convert.ToInt32(this.m_j.Rows[this.m_j.Rows.Count - 2][0]) + 1;
				}
				else
				{
					this.m_j.Rows[this.m_j.Rows.Count - 1][0] = 0;
				}
			}
			catch (Exception)
			{
			}
			if (br != null && bu)
			{
				try
				{
					for (int num2 = 0; num2 < this.m_j.Columns.Count; num2++)
					{
						bs.Append(dataRow[num2].ToString() + "\t");
					}
					br.WriteLine(bs.ToString());
					bs.Clear();
				}
				catch (Exception ex2)
				{
					a("Error: when autosave or autoremove！" + ex2.Message, Color.Red);
				}
			}
			if (el.Checked && (double)this.m_j.Rows.Count > Convert.ToDouble(en.Text))
			{
				d4.RowCount = 0;
				for (int num3 = 0; num3 < ee.Series.Count; num3++)
				{
					ee.Series[num3].Points.Clear();
				}
				try
				{
					int val = this.m_j.Rows.Count - 1;
					for (int num4 = 0; num4 < Math.Min(1000, val); num4++)
					{
						this.m_j.Rows[0].Delete();
					}
					this.m_j.AcceptChanges();
				}
				catch (Exception ex3)
				{
					a("Error: when autosave or autoremove！" + ex3.Message, Color.Red);
				}
			}
			eh.Text = g("status_text_datacount", "Data Count") + ": " + Convert.ToString(this.m_j.Rows.Count);
			if (gd.SelectedIndex == 2)
			{
				gd.SelectedIndex = 0;
			}
		}
		d4.RowCount = this.m_j.Rows.Count;
		if (!this.m_a2 || d0.SelectedTab != d1)
		{
			return;
		}
		if (!c8 && this.m_ar.Count > 0)
		{
			a7();
			int num5 = 0;
			ee.ChartAreas[0].AxisY.ScaleView.SmallScrollMinSize = ee.ChartAreas[0].AxisY.ScaleView.Size / 10.0;
			ee.ChartAreas[0].AxisX.ScaleView.SmallScrollMinSize = ee.ChartAreas[0].AxisX.ScaleView.Size / 10.0;
			for (int num6 = 0; num6 < this.m_j.Columns.Count - 2; num6++)
			{
				try
				{
					if (Convert.ToInt32(this.m_aq[num6].Row["DataLevel"]) == 3 && d4.Columns[num6 + 2].DataPropertyName != c9 && (Convert.ToInt32(this.m_aq[num6].Row["Addr"]) == a2() || !gh.Checked))
					{
						ee.Series[num5].Points.DataBind(this.m_ar, c9, d4.Columns[num6 + 2].DataPropertyName, "");
						num5++;
					}
				}
				catch (Exception)
				{
				}
			}
		}
		else
		{
			a(g("status_text_help", "Tips") + ": " + g("curve_text_help", "If No Curve, please right click the parameter row in Parameters Tab Page at first"), Color.Red);
		}
	}

	[CompilerGenerated]
	private void n()
	{
		if (ci < this.m_p.Rows.Count)
		{
			if (f0.SelectedRows.Count != 0)
			{
				int index = f0.SelectedRows[0].Index;
				f0.Rows[index].Selected = false;
			}
			if (ci <= this.m_p.Rows.Count)
			{
				f0.Rows[ci].Selected = true;
				f0.CurrentCell = f0.Rows[ci].Cells[1];
			}
			if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "=")
			{
				double num = 0.0;
				string text = "";
				try
				{
					num = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
					text = this.m_p.Rows[ci]["ValueText"].ToString();
				}
				catch
				{
					text = Convert.ToString(this.m_p.Rows[ci]["ValueText"]);
				}
				if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTDELAY")
				{
					if (Convert.ToDouble(this.m_p.Rows[ci]["Count"]) < num)
					{
						this.m_p.Rows[ci]["Count"] = Convert.ToDouble(this.m_p.Rows[ci]["Count"]) + cj;
					}
					else
					{
						this.m_p.Rows[ci]["Count"] = 0;
						ci++;
					}
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTGO")
				{
					if (Convert.ToInt32(this.m_p.Rows[ci]["Count"]) < Convert.ToInt32(this.m_p.Rows[ci]["Var"]))
					{
						this.m_p.Rows[ci]["Count"] = Convert.ToInt32(this.m_p.Rows[ci]["Count"]) + 1;
						ci = Convert.ToInt32(num);
					}
					else
					{
						this.m_p.Rows[ci]["Count"] = 0;
						ci++;
					}
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTCLEARDATA")
				{
					if (Convert.ToInt32(num) == 1)
					{
						d4.RowCount = 0;
						this.m_j.Clear();
					}
					ci++;
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTRECORDDATA")
				{
					int num2 = Convert.ToInt32(num);
					if (num2 <= 2 && num2 >= 0)
					{
						gd.SelectedIndex = num2;
					}
					ci++;
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTCONFIRM")
				{
					ch.Enabled = false;
					MessageBox.Show(text, "Confirm");
					_ = 1;
					ch.Enabled = true;
					ci++;
				}
				else if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTREG")
				{
					int num3 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					if (num3 >= 0)
					{
						cl[num3] = text;
					}
					ci++;
				}
				else
				{
					int num4 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					if (num4 >= 0)
					{
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "=" + Convert.ToString(cl[num4]) + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
					else
					{
						cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "=" + text + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					}
					ci++;
				}
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "?")
			{
				int num5 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
				double num6 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
				if (num5 >= 0)
				{
					try
					{
						num6 = Convert.ToDouble(cl[num5]);
					}
					catch
					{
					}
				}
				double num7 = ck - num6;
				this.m_p.Rows[ci]["Count"] = Convert.ToDouble(num7.ToString("G5"));
				if (Math.Abs(Convert.ToDouble(this.m_p.Rows[ci]["Count"])) < Convert.ToDouble(this.m_p.Rows[ci]["Var"]))
				{
					f0.Rows[ci].DefaultCellStyle.SelectionForeColor = Color.Black;
					ck = Math.Pow(10.0, 14.0);
					ci++;
				}
				else
				{
					f0.Rows[ci].DefaultCellStyle.SelectionForeColor = Color.Red;
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				}
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "^")
			{
				cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "^" + Convert.ToString(this.m_p.Rows[ci]["ValueText"]) + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				ci++;
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "!")
			{
				cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "!@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				ci++;
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "+=")
			{
				if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTREG")
				{
					int num8 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					if (num8 >= 0)
					{
						double num9 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
						cl[num8] += num9;
					}
					ci++;
				}
				else if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
				{
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				}
				else
				{
					int num10 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					double num11 = ck + Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
					if (num10 >= 0)
					{
						try
						{
							num11 = ck + Convert.ToDouble(cl[num10]);
						}
						catch
						{
						}
					}
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "=" + num11 + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
					ck = Math.Pow(10.0, 14.0);
					ci++;
				}
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == ">>")
			{
				ch.Enabled = false;
				string text2 = a("Input", this.m_p.Rows[ci]["ValueText"].ToString(), Convert.ToString(this.m_p.Rows[ci]["Count"]));
				if (Convert.ToString(this.m_p.Rows[ci]["ParamName"]) == "SCRIPTREG")
				{
					int num12 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
					if (num12 >= 0)
					{
						cl[num12] = text2;
					}
				}
				else
				{
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "=" + text2 + "@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				}
				ch.Enabled = true;
				ci++;
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "??")
			{
				int num13 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
				double num14 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
				if (num13 >= 0)
				{
					try
					{
						num14 = Convert.ToDouble(cl[num13]);
					}
					catch
					{
					}
				}
				double value = ck - num14;
				this.m_p.Rows[ci]["Count"] = Convert.ToDouble(value.ToString("G5"));
				if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
				{
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				}
				else if (Math.Abs(value) < Convert.ToDouble(this.m_p.Rows[ci]["Var"]))
				{
					ck = Math.Pow(10.0, 14.0);
					ci++;
				}
				else
				{
					ck = Math.Pow(10.0, 14.0);
					ci--;
				}
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "?=")
			{
				int num15 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
				double num16 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
				if (num15 >= 0)
				{
					try
					{
						num16 = Convert.ToDouble(cl[num15]);
					}
					catch
					{
					}
				}
				double value2 = ck - num16;
				this.m_p.Rows[ci]["Count"] = Convert.ToDouble(value2.ToString("G5"));
				if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
				{
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				}
				else if (Math.Abs(value2) < Convert.ToDouble(this.m_p.Rows[ci]["Var"]))
				{
					ck = Math.Pow(10.0, 14.0);
					ci += 2;
				}
				else
				{
					ck = Math.Pow(10.0, 14.0);
					ci++;
				}
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "?>")
			{
				int num17 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
				double num18 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
				if (num17 >= 0)
				{
					try
					{
						num18 = Convert.ToDouble(cl[num17]);
					}
					catch
					{
					}
				}
				double num19 = ck - num18;
				this.m_p.Rows[ci]["Count"] = Convert.ToDouble(num19.ToString("G5"));
				if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
				{
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				}
				else if (num19 > 0.0)
				{
					ck = Math.Pow(10.0, 14.0);
					ci += 2;
				}
				else
				{
					ck = Math.Pow(10.0, 14.0);
					ci++;
				}
			}
			else if (Convert.ToString(this.m_p.Rows[ci]["Cmd"]) == "?<")
			{
				int num20 = Convert.ToInt32(this.m_p.Rows[ci]["Reg"]);
				double num21 = Convert.ToDouble(this.m_p.Rows[ci]["ValueText"]);
				if (num20 >= 0)
				{
					try
					{
						num21 = Convert.ToDouble(cl[num20]);
					}
					catch
					{
					}
				}
				double num22 = ck - num21;
				this.m_p.Rows[ci]["Count"] = Convert.ToDouble(num22.ToString("G5"));
				if (Math.Abs(ck - Math.Pow(10.0, 14.0)) < 10.0)
				{
					cp.Enqueue(Convert.ToString(this.m_p.Rows[ci]["ModuleName"]) + ":" + Convert.ToString(this.m_p.Rows[ci]["ParamName"]) + "?@" + Convert.ToString(this.m_p.Rows[ci]["Addr"]));
				}
				else if (num22 < 0.0)
				{
					ck = Math.Pow(10.0, 14.0);
					ci += 2;
				}
				else
				{
					ck = Math.Pow(10.0, 14.0);
					ci++;
				}
			}
		}
		else
		{
			ch.Enabled = false;
			ci = 0;
			f2.BackColor = SystemColors.Control;
			f2.Text = "开始";
		}
		if (ch.Enabled)
		{
			cm++;
			if (cm % 2 == 0)
			{
				f2.BackColor = Color.LightGreen;
			}
			else
			{
				f2.BackColor = Color.GreenYellow;
			}
		}
		else
		{
			cm = 0u;
		}
	}

	[CompilerGenerated]
	private void m()
	{
		if (MessageBox.Show(this, g("device_autoreload", "Wrong model file. Auto reload the correct model file?"), g("text_confirm", "Confirm"), MessageBoxButtons.YesNo, MessageBoxIcon.Question) == DialogResult.Yes && this.m_g.Rows.Count > 0)
		{
			h(255);
		}
	}

	[CompilerGenerated]
	private void l()
	{
		for (int num = 0; num < this.m_h.Rows.Count; num++)
		{
			this.m_h.Rows[num]["ledsw"] = 0;
			this.m_h.Rows[num]["lederr"] = 0;
			this.m_h.Rows[num]["constatus"] = 0;
		}
	}

	[CompilerGenerated]
	private void k()
	{
		ee.Series.Clear();
		a7();
		int num = 0;
		for (int num2 = 0; num2 < Math.Min(this.m_aq.Count, this.m_j.Columns.Count - 2); num2++)
		{
			if (Convert.ToInt32(this.m_aq[num2].Row["DataLevel"]) != 3 || !(d4.Columns[num2 + 2].DataPropertyName != c9) || (Convert.ToInt32(this.m_aq[num2].Row["Addr"]) != a2() && gh.Checked))
			{
				continue;
			}
			Series series = new Series
			{
				ChartType = SeriesChartType.Line,
				XValueType = da
			};
			try
			{
				if (num < c4.Length)
				{
					series.Color = c4[num];
				}
				else if (num < 2 * c4.Length)
				{
					series.Color = c4[num - c4.Length];
					series.BorderDashStyle = ChartDashStyle.Dot;
				}
				series.BorderWidth = dc;
				series.LegendText = this.m_aq[num2].Row["FolderName"].ToString() + ": " + this.m_aq[num2].Row["ParamText"].ToString() + " @" + this.m_aq[num2].Row["Addr"].ToString();
				series.Points.DataBind(this.m_ar, c9, d4.Columns[num2 + 2].DataPropertyName, "");
				ee.Series.Add(series);
				num++;
			}
			catch (Exception)
			{
			}
		}
	}

	[CompilerGenerated]
	private bool j()
	{
		b("文件列表刷新完成", Color.Blue);
		return true;
	}

	[CompilerGenerated]
	private void i()
	{
		fq.Enabled = false;
		fu.Enabled = false;
		fr.Enabled = false;
		fs.Enabled = false;
	}

	[CompilerGenerated]
	private void h()
	{
		fm.Items.Clear();
	}

	[CompilerGenerated]
	private bool a(ListViewItem A_0)
	{
		return A_0.Text.Equals(this.m_w.b(), StringComparison.OrdinalIgnoreCase);
	}

	[CompilerGenerated]
	private bool g()
	{
		b("文件CRC获取成功", Color.Blue);
		return true;
	}

	[CompilerGenerated]
	private bool f()
	{
		b("文件删除成功", Color.Blue);
		return true;
	}

	[CompilerGenerated]
	private bool e()
	{
		b("文件上传成功", Color.Blue);
		return true;
	}

	[CompilerGenerated]
	private void d()
	{
		fq.Enabled = true;
		fu.Enabled = true;
		fr.Enabled = true;
		fs.Enabled = true;
		fv.Enabled = true;
		if (this.m_y != null)
		{
			this.m_y();
			this.m_y = null;
		}
		fw.Value = 0;
	}

	[CompilerGenerated]
	private void c()
	{
		fq.Enabled = true;
		fu.Enabled = true;
		fr.Enabled = true;
		fs.Enabled = true;
		fv.Enabled = true;
		if (this.m_y != null)
		{
			this.m_y();
			this.m_y = null;
		}
		fw.Value = 0;
	}

	[CompilerGenerated]
	private bool b()
	{
		b("文件下载成功", Color.Blue);
		return true;
	}

	[CompilerGenerated]
	private void a()
	{
		fw.Value = 100;
	}
}
