using System;
using System.Collections;
using System.Data;
using System.IO;

internal class j
{
	private string m_a;

	private DataTable m_b = new DataTable("Table_Help");

	private DataView m_c;

	private string m_d = "";

	private string e = "Help\\";

	private string f = "";

	private Hashtable g = new Hashtable();

	public j(string A_0)
	{
		this.m_a = "";
		this.m_b.Columns.Add("ParamName", Type.GetType("System.String"));
		this.m_b.Columns.Add("HelpText", Type.GetType("System.String"));
		this.m_b.Columns.Add("PicName", Type.GetType("System.String"));
		DataColumn[] primaryKey = new DataColumn[1] { this.m_b.Columns["ParamName"] };
		this.m_b.PrimaryKey = primaryKey;
		this.m_c = new DataView(this.m_b);
		this.m_d = A_0;
		FileInfo[] files = new DirectoryInfo(this.m_d + e).GetFiles();
		for (int num = 0; num < files.Length; num++)
		{
			f = f + Path.GetFileNameWithoutExtension(files[num].FullName) + ",";
		}
	}

	public void a(int A_0)
	{
		if (A_0 == 255)
		{
			this.m_a = "";
		}
		else
		{
			string[] array = this.m_a.Split(',', ' ');
			this.m_a = "";
			string[] array2 = array;
			foreach (string text in array2)
			{
				if (text.Length > 0 && Convert.ToInt32(text.Split('@')[1]) != A_0)
				{
					this.m_a = this.m_a + text + ",";
				}
			}
		}
		a();
	}

	public void a(string A_0)
	{
		if (A_0 == "")
		{
			string[] array = f.Split(',', ' ');
			foreach (string text in array)
			{
				if (text.Length > 0)
				{
					this.m_a = this.m_a + text + "@" + 255 + ",";
				}
			}
		}
		else
		{
			this.m_a = A_0;
		}
		a();
	}

	public void a(string A_0, int A_1)
	{
		if (A_0 == "")
		{
			A_0 = f;
		}
		string[] array = A_0.Split(',', ' ');
		foreach (string text in array)
		{
			if (text.Length > 0)
			{
				this.m_a = this.m_a + text + "@" + A_1 + ",";
			}
		}
		a();
	}

	private void a()
	{
		this.m_b.Clear();
		DataTable dataTable = this.m_b.Clone();
		string[] array = this.m_a.Split(',', ' ');
		foreach (string text in array)
		{
			if (text.Length > 0)
			{
				dataTable.Clear();
				string[] array2 = text.Split('@');
				if (File.Exists(this.m_d + e + array2[0] + ".xml"))
				{
					dataTable.ReadXml(this.m_d + e + array2[0] + ".xml");
					this.m_b.Merge(dataTable);
				}
			}
		}
		DataTable dataTable2 = new DataView(this.m_b).ToTable(true, "ParamName", "HelpText", "PicName");
		this.m_b.Clear();
		for (int num2 = 0; num2 < dataTable2.Rows.Count; num2++)
		{
			this.m_b.Rows.Add(dataTable2.Rows[num2].ItemArray);
		}
		g.Clear();
		for (int num3 = 0; num3 < this.m_b.Rows.Count; num3++)
		{
			g.Add(this.m_b.Rows[num3]["ParamName"].ToString(), "Y");
		}
	}

	public string b(string A_0)
	{
		this.m_c.RowFilter = "ParamName = '" + A_0 + "'";
		if (this.m_c.Count > 0)
		{
			return this.m_c[0]["HelpText"].ToString();
		}
		return "";
	}

	public string c(string A_0)
	{
		this.m_c.RowFilter = "ParamName = '" + A_0 + "'";
		if (this.m_c.Count > 0)
		{
			return this.m_c[0]["PicName"].ToString();
		}
		return "";
	}

	public bool d(string A_0)
	{
		return g.Contains(A_0);
	}
}
