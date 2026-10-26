using System;
using System.Collections.Generic;
using System.Data;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;

public class k
{
	[CompilerGenerated]
	private sealed class a
	{
		public int a;

		internal bool b(DataRow A_0)
		{
			return A_0.Field<int>("Addr") == a;
		}
	}

	[CompilerGenerated]
	private sealed class b
	{
		public int a;

		public string b;

		public int c;

		internal bool d(DataRow A_0)
		{
			if (A_0.Field<int>("Addr") == a && A_0.Field<string>("ParamType") == b)
			{
				return A_0.Field<int>("ParamValue") == c;
			}
			return false;
		}
	}

	[CompilerGenerated]
	private sealed class c
	{
		public int a;

		public string b;

		public int c;

		internal bool d(DataRow A_0)
		{
			if (A_0.Field<int>("Addr") == a && A_0.Field<string>("ParamType") == b)
			{
				return A_0.Field<int>("ParamValue") == c;
			}
			return false;
		}
	}

	[CompilerGenerated]
	private sealed class d
	{
		public string a;

		public int b;

		internal bool c(DataRow A_0)
		{
			if (A_0.Field<int>("Addr") == 255 && A_0.Field<string>("ParamType") == a)
			{
				return A_0.Field<int>("ParamValue") == b;
			}
			return false;
		}
	}

	[Serializable]
	[CompilerGenerated]
	private sealed class e
	{
		public static readonly e a = new e();

		public static Func<DataRow, bool> b;

		public static Func<DataRow, bool> c;

		internal bool a(DataRow A_0)
		{
			return A_0.Field<int>("Addr") != 255;
		}

		internal bool b(DataRow A_0)
		{
			return A_0.Field<int>("Addr") == 255;
		}
	}

	[CompilerGenerated]
	private sealed class f
	{
		public string a;

		public int b;

		public int c;

		internal bool d(DataRow A_0)
		{
			if (A_0.Field<string>("ParamType") == a && A_0.Field<int>("ParamValue") == b)
			{
				if (c != 255 && A_0.Field<int>("Addr") != c)
				{
					return A_0.Field<int>("Addr") == 255;
				}
				return true;
			}
			return false;
		}
	}

	[CompilerGenerated]
	private sealed class g
	{
		public string a;

		public int b;

		public int c;

		internal bool d(DataRow A_0)
		{
			if (A_0.Field<string>("ParamType") == a && A_0.Field<int>("ParamValue") == b)
			{
				if (c != 255 && A_0.Field<int>("Addr") != c)
				{
					return A_0.Field<int>("Addr") == 255;
				}
				return true;
			}
			return false;
		}

		internal bool e(DataRow A_0)
		{
			if (A_0.Field<string>("ParamType") == a && A_0.Field<int>("ParamValue") == 65536)
			{
				if (c != 255 && A_0.Field<int>("Addr") != c)
				{
					return A_0.Field<int>("Addr") == 255;
				}
				return true;
			}
			return false;
		}
	}

	private string m_a;

	private DataTable m_b = new DataTable("Table_ParamType");

	private DataView m_c;

	private string m_d = "";

	private string m_e = "Option\\";

	private string m_f = "";

	public k(string A_0)
	{
		this.m_a = "";
		this.m_b.Columns.Add("ParamType", Type.GetType("System.String"));
		this.m_b.Columns.Add("ParamValue", typeof(int));
		this.m_b.Columns.Add("ValueText", Type.GetType("System.String"));
		this.m_b.Columns.Add("CmdText", Type.GetType("System.String"));
		this.m_b.Columns.Add("Addr", typeof(int));
		this.m_c = new DataView(this.m_b);
		this.m_d = A_0;
		FileInfo[] files = new DirectoryInfo(this.m_d + this.m_e).GetFiles();
		for (int num = 0; num < files.Length; num++)
		{
			this.m_f = this.m_f + Path.GetFileNameWithoutExtension(files[num].FullName) + ",";
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
				if (text.Length <= 0)
				{
					continue;
				}
				string[] array3 = text.Split('@');
				if (array3.Length >= 2)
				{
					if (Convert.ToInt32(array3[1]) != A_0)
					{
						this.m_a = this.m_a + text + ",";
					}
				}
				else
				{
					this.m_a = this.m_a + text + ",";
				}
			}
			foreach (DataRow item in (from row in this.m_b.AsEnumerable()
				where row.Field<int>("Addr") == A_0
				select row).ToList())
			{
				item.Delete();
			}
			this.m_b.AcceptChanges();
		}
		a();
	}

	public void a(string A_0)
	{
		if (A_0 == "")
		{
			string[] array = this.m_f.Split(',', ' ');
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
			A_0 = this.m_f;
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

	public void a(DataTable A_0)
	{
		if (!A_0.Columns.Contains("Addr"))
		{
			A_0.Columns.Add("Addr", typeof(int));
		}
		foreach (DataRow row in A_0.Rows)
		{
			int a2;
			if (Convert.IsDBNull(row["Addr"]))
			{
				a2 = 255;
			}
			else
			{
				a2 = Convert.ToInt32(row["Addr"]);
			}
			string b2 = row["ParamType"].ToString();
			int c2 = Convert.ToInt32(row["ParamValue"]);
			foreach (DataRow item in (from row in this.m_b.AsEnumerable()
				where row.Field<int>("Addr") == a2 && row.Field<string>("ParamType") == b2 && row.Field<int>("ParamValue") == c2
				select row).ToList())
			{
				item.Delete();
			}
			this.m_b.AcceptChanges();
			DataRow dataRow2 = this.m_b.NewRow();
			dataRow2["ParamType"] = row["ParamType"];
			dataRow2["ParamValue"] = row["ParamValue"];
			dataRow2["ValueText"] = row["ValueText"];
			dataRow2["CmdText"] = row["CmdText"];
			dataRow2["Addr"] = a2;
			this.m_b.Rows.Add(dataRow2);
		}
	}

	private void a()
	{
		try
		{
			if (string.IsNullOrWhiteSpace(this.m_a))
			{
				a("");
				return;
			}
			DataTable dataTable = this.m_b.Clone();
			DataTable dataTable2 = this.m_b.Clone();
			foreach (DataRow item in (from A_0 in this.m_b.AsEnumerable()
				where A_0.Field<int>("Addr") != 255
				select A_0).ToList())
			{
				dataTable.ImportRow(item);
			}
			List<DataRow> list = (from A_0 in this.m_b.AsEnumerable()
				where A_0.Field<int>("Addr") == 255
				select A_0).ToList();
			foreach (DataRow item2 in list)
			{
				string text = item2.Field<string>("ParamType");
				if (text.StartsWith("OPT_"))
				{
					string text2 = text.Substring(4).ToLower() + "_opt.xml";
					if (!File.Exists(this.m_d + this.m_e + text2))
					{
						dataTable2.ImportRow(item2);
					}
				}
			}
			foreach (DataRow item3 in list)
			{
				item3.Delete();
			}
			this.m_b.AcceptChanges();
			DataTable dataTable3 = this.m_b.Clone();
			string[] array = this.m_a.Split(',', ' ');
			foreach (string text3 in array)
			{
				if (text3.Length <= 0)
				{
					continue;
				}
				dataTable3.Clear();
				string[] array2 = text3.Split('@');
				if (!File.Exists(this.m_d + this.m_e + array2[0] + ".xml"))
				{
					continue;
				}
				try
				{
					dataTable3.ReadXml(this.m_d + this.m_e + array2[0] + ".xml");
					int result = 255;
					if (array2.Length >= 2)
					{
						int.TryParse(array2[1], out result);
					}
					foreach (DataRow row2 in dataTable3.Rows)
					{
						row2["Addr"] = result;
					}
					this.m_b.Merge(dataTable3);
				}
				catch (Exception)
				{
				}
			}
			DataTable dataTable4 = new DataView(this.m_b).ToTable(true, "ParamType", "ParamValue", "ValueText", "CmdText", "Addr");
			this.m_b.Clear();
			foreach (DataRow row3 in dataTable4.Rows)
			{
				this.m_b.ImportRow(row3);
			}
			foreach (DataRow row4 in dataTable.Rows)
			{
				string b2 = row4["ParamType"].ToString();
				int c2 = Convert.ToInt32(row4["ParamValue"]);
				int a2 = Convert.ToInt32(row4["Addr"]);
				if ((from A_0 in this.m_b.AsEnumerable()
					where A_0.Field<int>("Addr") == a2 && A_0.Field<string>("ParamType") == b2 && A_0.Field<int>("ParamValue") == c2
					select A_0).Count() == 0)
				{
					this.m_b.ImportRow(row4);
				}
			}
			foreach (DataRow row5 in dataTable2.Rows)
			{
				string a3 = row5["ParamType"].ToString();
				int b3 = Convert.ToInt32(row5["ParamValue"]);
				if ((from A_0 in this.m_b.AsEnumerable()
					where A_0.Field<int>("Addr") == 255 && A_0.Field<string>("ParamType") == a3 && A_0.Field<int>("ParamValue") == b3
					select A_0).Count() == 0)
				{
					this.m_b.ImportRow(row5);
				}
			}
			this.m_b.AcceptChanges();
		}
		catch (Exception)
		{
		}
	}

	public DataView b(string A_0, int A_1 = 255)
	{
		DataView dataView = new DataView(this.m_b);
		if (A_1 == 255)
		{
			string rowFilter = "ParamType = '" + A_0 + "'";
			dataView.RowFilter = rowFilter;
			dataView.Sort = "ParamValue ASC";
			return dataView;
		}
		string rowFilter2 = "ParamType = '" + A_0 + "' AND (Addr = " + A_1 + " OR Addr = 255)";
		dataView.RowFilter = rowFilter2;
		dataView.Sort = "ParamValue ASC, Addr ASC";
		DataTable dataTable = dataView.ToTable();
		DataTable dataTable2 = dataTable.Clone();
		Dictionary<int, DataRow> dictionary = new Dictionary<int, DataRow>();
		foreach (DataRow row in dataTable.Rows)
		{
			int key = Convert.ToInt32(row["ParamValue"]);
			if (!dictionary.ContainsKey(key) || (dictionary[key].Field<int>("Addr") == 255 && row.Field<int>("Addr") != 255))
			{
				dictionary[key] = row;
			}
		}
		foreach (DataRow value in dictionary.Values)
		{
			dataTable2.ImportRow(value);
		}
		dataTable2.DefaultView.Sort = "ParamValue ASC";
		return dataTable2.DefaultView;
	}

	public string a(string A_0, int A_1, int A_2 = 255)
	{
		string result = A_1.ToString();
		if (A_2 == 255)
		{
			_ = "ParamType = '" + A_0 + "' AND ParamValue = " + A_1;
		}
		else
		{
			_ = "ParamType = '" + A_0 + "' AND ParamValue = " + A_1 + " AND (Addr = " + A_2 + " OR Addr = 255)";
		}
		using (IEnumerator<DataRow> enumerator = (from row in this.m_b.AsEnumerable()
			where row.Field<string>("ParamType") == A_0 && row.Field<int>("ParamValue") == A_1 && (A_2 == 255 || row.Field<int>("Addr") == A_2 || row.Field<int>("Addr") == 255)
			select row).GetEnumerator())
		{
			if (enumerator.MoveNext())
			{
				result = enumerator.Current["ValueText"].ToString();
			}
		}
		return result;
	}

	public string b(string A_0, int A_1, int A_2 = 255)
	{
		string result = A_1.ToString();
		if (A_2 == 255)
		{
			_ = "ParamType = '" + A_0 + "' AND ParamValue = " + A_1;
		}
		else
		{
			_ = "ParamType = '" + A_0 + "' AND ParamValue = " + A_1 + " AND (Addr = " + A_2 + " OR Addr = 255)";
		}
		EnumerableRowCollection<DataRow> enumerableRowCollection = from row in this.m_b.AsEnumerable()
			where row.Field<string>("ParamType") == A_0 && row.Field<int>("ParamValue") == A_1 && (A_2 == 255 || row.Field<int>("Addr") == A_2 || row.Field<int>("Addr") == 255)
			select row;
		bool flag = true;
		using (IEnumerator<DataRow> enumerator = enumerableRowCollection.GetEnumerator())
		{
			if (enumerator.MoveNext())
			{
				result = enumerator.Current["ValueText"].ToString();
				flag = false;
			}
		}
		if (flag)
		{
			using IEnumerator<DataRow> enumerator = (from row in this.m_b.AsEnumerable()
				where row.Field<string>("ParamType") == A_0 && row.Field<int>("ParamValue") == 65536 && (A_2 == 255 || row.Field<int>("Addr") == A_2 || row.Field<int>("Addr") == 255)
				select row).GetEnumerator();
			if (enumerator.MoveNext())
			{
				result = enumerator.Current["ValueText"].ToString();
			}
		}
		return result;
	}
}
