using System;
using System.Collections.Generic;
using System.Data;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text.RegularExpressions;

internal class h
{
	private class a
	{
		[CompilerGenerated]
		private string m_a;

		[CompilerGenerated]
		private int m_b;

		[CompilerGenerated]
		private int m_c;

		[CompilerGenerated]
		private int m_d;

		[CompilerGenerated]
		private int m_e;

		[CompilerGenerated]
		private int m_f;

		[CompilerGenerated]
		private int m_g;

		[SpecialName]
		[CompilerGenerated]
		public string a()
		{
			return this.m_a;
		}

		[SpecialName]
		[CompilerGenerated]
		public void a(string A_0)
		{
			this.m_a = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public int b()
		{
			return this.m_b;
		}

		[SpecialName]
		[CompilerGenerated]
		public void a(int A_0)
		{
			this.m_b = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public int c()
		{
			return this.m_c;
		}

		[SpecialName]
		[CompilerGenerated]
		public void b(int A_0)
		{
			this.m_c = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public int d()
		{
			return this.m_d;
		}

		[SpecialName]
		[CompilerGenerated]
		public void c(int A_0)
		{
			this.m_d = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public int e()
		{
			return this.m_e;
		}

		[SpecialName]
		[CompilerGenerated]
		public void d(int A_0)
		{
			this.m_e = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public int f()
		{
			return this.m_f;
		}

		[SpecialName]
		[CompilerGenerated]
		public void e(int A_0)
		{
			this.m_f = A_0;
		}

		[SpecialName]
		[CompilerGenerated]
		public int g()
		{
			return this.m_g;
		}

		[SpecialName]
		[CompilerGenerated]
		public void f(int A_0)
		{
			this.m_g = A_0;
		}
	}

	[CompilerGenerated]
	private sealed class b
	{
		public HashSet<DataRow> a;

		internal bool b(DataRow A_0)
		{
			return !a.Contains(A_0);
		}
	}

	[Serializable]
	[CompilerGenerated]
	private sealed class c
	{
		public static readonly c a = new c();

		public static Func<DataRow, global::a<string, int, int, int, int, int>> b;

		public static Func<DataRow, int> c;

		public static Func<IGrouping<global::a<string, int, int, int, int, int>, DataRow>, DataRow> d;

		internal global::a<string, int, int, int, int, int> a(DataRow A_0)
		{
			return new global::a<string, int, int, int, int, int>(A_0.Field<string>("Model"), A_0.Field<int>("HardwareMajor"), A_0.Field<int>("HardwareMinor"), A_0.Field<int>("FirmwareMajor"), A_0.Field<int>("FirmwareMinor"), A_0.Field<int>("BuildDate"));
		}

		internal DataRow a(IGrouping<global::a<string, int, int, int, int, int>, DataRow> A_0)
		{
			return A_0.OrderByDescending((DataRow row) => row.Field<int>("FileVer")).First();
		}

		internal int b(DataRow A_0)
		{
			return A_0.Field<int>("FileVer");
		}
	}

	private DataTable m_a = new DataTable("Table_ModelFile");

	public h(string A_0)
	{
		this.m_a.Columns.Add("Model", Type.GetType("System.String"));
		this.m_a.Columns.Add("HardwareMajor", typeof(int));
		this.m_a.Columns.Add("HardwareMinor", typeof(int));
		this.m_a.Columns.Add("FirmwareMajor", typeof(int));
		this.m_a.Columns.Add("FirmwareMinor", typeof(int));
		this.m_a.Columns.Add("BuildDate", typeof(int));
		this.m_a.Columns.Add("FileVer", typeof(int));
		this.m_a.Columns.Add("TryCount", typeof(int));
		this.m_a.Columns.Add("FullFileName", Type.GetType("System.String"));
		string[] files = Directory.GetFiles(A_0);
		for (int num = 0; num < files.Length; num++)
		{
			string text = Path.GetFileNameWithoutExtension(files[num]).ToString();
			a a2 = b(text);
			if (a2 != null)
			{
				DataRow dataRow = this.m_a.NewRow();
				dataRow["Model"] = a2.a();
				dataRow["HardwareMajor"] = Convert.ToInt32(a2.b());
				dataRow["HardwareMinor"] = Convert.ToInt32(a2.c());
				dataRow["FirmwareMajor"] = Convert.ToInt32(a2.d());
				dataRow["FirmwareMinor"] = Convert.ToInt32(a2.e());
				dataRow["BuildDate"] = Convert.ToInt32(a2.f());
				dataRow["FileVer"] = Convert.ToInt32(a2.g());
				dataRow["TryCount"] = 0;
				dataRow["FullFileName"] = text;
				this.m_a.Rows.Add(dataRow);
			}
		}
		IEnumerable<DataRow> collection = from row2 in this.m_a.AsEnumerable()
			group row2 by new global::a<string, int, int, int, int, int>(row2.Field<string>("Model"), row2.Field<int>("HardwareMajor"), row2.Field<int>("HardwareMinor"), row2.Field<int>("FirmwareMajor"), row2.Field<int>("FirmwareMinor"), row2.Field<int>("BuildDate")) into source
			select source.OrderByDescending((DataRow row2) => row2.Field<int>("FileVer")).First();
		HashSet<DataRow> a3 = new HashSet<DataRow>(collection);
		DataRow[] array = (from item in this.m_a.Select()
			where !a3.Contains(item)
			select item).ToArray();
		foreach (DataRow row in array)
		{
			this.m_a.Rows.Remove(row);
		}
	}

	public void c(string A_0)
	{
		this.m_a.DefaultView.RowFilter = "Model = '" + A_0 + "'";
		foreach (DataRowView item in this.m_a.DefaultView)
		{
			if (item["Model"].ToString() == A_0)
			{
				item["TryCount"] = Convert.ToInt32(item["TryCount"]) + 1;
			}
		}
	}

	public void a()
	{
		for (int num = 1; num < this.m_a.Rows.Count; num++)
		{
			this.m_a.Rows[num]["TryCount"] = 0;
		}
	}

	public int d(string A_0)
	{
		this.m_a.DefaultView.RowFilter = "Model = '" + A_0 + "'";
		foreach (DataRowView item in this.m_a.DefaultView)
		{
			if (item["Model"].ToString() == A_0)
			{
				return Convert.ToInt32(item["TryCount"]);
			}
		}
		return 0;
	}

	public int e(string A_0)
	{
		this.m_a.DefaultView.RowFilter = "Model = '" + A_0 + "'";
		return this.m_a.DefaultView.Count;
	}

	public string f(string A_0)
	{
		this.m_a.DefaultView.RowFilter = "Model = '" + A_0 + "'";
		this.m_a.DefaultView.Sort = "HardwareMajor DESC, HardwareMinor DESC, FirmwareMajor DESC, FirmwareMinor DESC, BuildDate DESC";
		if (this.m_a.DefaultView.Count > 0)
		{
			return this.m_a.DefaultView[0]["FullFileName"].ToString();
		}
		return "";
	}

	public string g(string A_0)
	{
		a a2 = a(A_0);
		if (a2 == null)
		{
			return "";
		}
		this.m_a.DefaultView.RowFilter = "TryCount > 0";
		this.m_a.DefaultView.Sort = "HardwareMajor DESC, HardwareMinor DESC, FirmwareMajor DESC, FirmwareMinor DESC, BuildDate DESC";
		if (this.m_a.DefaultView.Count == 0)
		{
			return "";
		}
		int num = 0;
		for (num = 0; num < this.m_a.DefaultView.Count && a2.b() <= Convert.ToInt32(this.m_a.DefaultView[num]["HardwareMajor"]) && (a2.b() != Convert.ToInt32(this.m_a.DefaultView[num]["HardwareMajor"]) || (a2.c() <= Convert.ToInt32(this.m_a.DefaultView[num]["HardwareMinor"]) && (a2.c() != Convert.ToInt32(this.m_a.DefaultView[num]["HardwareMinor"]) || (a2.d() <= Convert.ToInt32(this.m_a.DefaultView[num]["FirmwareMajor"]) && (a2.d() != Convert.ToInt32(this.m_a.DefaultView[num]["FirmwareMajor"]) || (a2.e() <= Convert.ToInt32(this.m_a.DefaultView[num]["FirmwareMinor"]) && (a2.e() != Convert.ToInt32(this.m_a.DefaultView[num]["FirmwareMinor"]) || a2.f() < Convert.ToInt32(this.m_a.DefaultView[num]["BuildDate"])))))))); num++)
		{
		}
		if (num >= this.m_a.DefaultView.Count)
		{
			return this.m_a.DefaultView[num - 1]["FullFileName"].ToString();
		}
		return this.m_a.DefaultView[num]["FullFileName"].ToString();
	}

	public List<string> h(string A_0)
	{
		List<string> list = new List<string>();
		if (A_0 == "")
		{
			this.m_a.DefaultView.RowFilter = "";
		}
		else
		{
			this.m_a.DefaultView.RowFilter = "Model LIKE '%" + A_0 + "%'";
		}
		DataTable dataTable = this.m_a.DefaultView.ToTable(true, "Model");
		dataTable.DefaultView.Sort = "Model ASC";
		for (int num = 0; num < dataTable.Rows.Count; num++)
		{
			list.Add(dataTable.DefaultView[num]["Model"].ToString());
		}
		return list;
	}

	public List<string> i(string A_0)
	{
		List<string> list = new List<string>();
		this.m_a.DefaultView.RowFilter = "Model = '" + A_0 + "'";
		this.m_a.DefaultView.Sort = "HardwareMajor DESC, HardwareMinor DESC, FirmwareMajor DESC, FirmwareMinor DESC, BuildDate DESC";
		for (int num = 0; num < this.m_a.DefaultView.Count; num++)
		{
			list.Add(this.m_a.DefaultView[num]["FullFileName"].ToString());
		}
		return list;
	}

	private static a b(string A_0)
	{
		a obj = new a();
		obj.a(0);
		obj.b(0);
		obj.c(0);
		obj.d(0);
		obj.e(19700101);
		obj.f(0);
		a a2 = obj;
		Match match = new Regex("^(?<model>[A-Z0-9\\-]+)_V(?<hwMajor>\\d+)\\.(?<hwMinor>\\d+)(?:\\.(?<fwMajor>\\d+)\\.(?<fwMinor>\\d+))?\\.(?<buildDate>\\d{8})(?:\\.(?<configVersion>\\d+))?$", RegexOptions.IgnoreCase).Match(A_0);
		if (match.Success)
		{
			a obj2 = new a();
			obj2.a(match.Groups["model"].Value);
			obj2.a(Convert.ToInt32(match.Groups["hwMajor"].Value));
			obj2.b(Convert.ToInt32(match.Groups["hwMinor"].Value));
			obj2.c(match.Groups["fwMajor"].Success ? Convert.ToInt32(match.Groups["fwMajor"].Value) : a2.d());
			obj2.d(match.Groups["fwMinor"].Success ? Convert.ToInt32(match.Groups["fwMinor"].Value) : a2.e());
			obj2.e(Convert.ToInt32(match.Groups["buildDate"].Value));
			obj2.f(match.Groups["configVersion"].Success ? Convert.ToInt32(match.Groups["configVersion"].Value) : a2.g());
			return obj2;
		}
		match = new Regex("^(?<model>[A-Z0-9\\-]+)_V(?<hwMajor>\\d+)\\.(?<hwMinor>\\d+)\\.(?<buildDate>\\d{8})(?:\\.(?<configVersion>\\d+))?$", RegexOptions.IgnoreCase).Match(A_0);
		if (match.Success)
		{
			a obj3 = new a();
			obj3.a(match.Groups["model"].Value);
			obj3.a(Convert.ToInt32(match.Groups["hwMajor"].Value));
			obj3.b(Convert.ToInt32(match.Groups["hwMinor"].Value));
			obj3.c(a2.d());
			obj3.d(a2.e());
			obj3.e(Convert.ToInt32(match.Groups["buildDate"].Value));
			obj3.f(match.Groups["configVersion"].Success ? Convert.ToInt32(match.Groups["configVersion"].Value) : a2.g());
			return obj3;
		}
		match = new Regex("^(?<model>[A-Z0-9\\-]+)$", RegexOptions.IgnoreCase).Match(A_0);
		if (match.Success)
		{
			a obj4 = new a();
			obj4.a(match.Groups["model"].Value);
			obj4.a(a2.b());
			obj4.b(a2.c());
			obj4.c(a2.d());
			obj4.d(a2.e());
			obj4.e(a2.f());
			obj4.f(a2.g());
			return obj4;
		}
		return null;
	}

	private static a a(string A_0)
	{
		a obj = new a();
		obj.a(0);
		obj.b(0);
		obj.c(0);
		obj.d(0);
		obj.e(19700101);
		obj.f(0);
		a a2 = obj;
		Match match = new Regex("^(?<hwMajor>\\d+)\\.(?<hwMinor>\\d+)(?:\\.(?<fwMajor>\\d+)\\.(?<fwMinor>\\d+))?\\.(?<buildDate>\\d{8})$", RegexOptions.IgnoreCase).Match(A_0);
		if (match.Success)
		{
			a obj2 = new a();
			obj2.a(Convert.ToInt32(match.Groups["hwMajor"].Value));
			obj2.b(Convert.ToInt32(match.Groups["hwMinor"].Value));
			obj2.c(match.Groups["fwMajor"].Success ? Convert.ToInt32(match.Groups["fwMajor"].Value) : a2.d());
			obj2.d(match.Groups["fwMinor"].Success ? Convert.ToInt32(match.Groups["fwMinor"].Value) : a2.e());
			obj2.e(Convert.ToInt32(match.Groups["buildDate"].Value));
			return obj2;
		}
		match = new Regex("^(?<model>[A-Z0-9\\-]+)_V(?<hwMajor>\\d+)\\.(?<hwMinor>\\d+)\\.(?<buildDate>\\d{8})$", RegexOptions.IgnoreCase).Match(A_0);
		if (match.Success)
		{
			a obj3 = new a();
			obj3.a(Convert.ToInt32(match.Groups["hwMajor"].Value));
			obj3.b(Convert.ToInt32(match.Groups["hwMinor"].Value));
			obj3.c(a2.d());
			obj3.d(a2.e());
			obj3.e(Convert.ToInt32(match.Groups["buildDate"].Value));
			return obj3;
		}
		return null;
	}
}
