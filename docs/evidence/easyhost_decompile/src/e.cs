using System;
using System.Collections.Generic;
using System.Data;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;

internal class e
{
	[CompilerGenerated]
	private sealed class a
	{
		public string a;

		internal bool b(DataRow A_0)
		{
			return A_0.Field<string>("Connection") == a;
		}
	}

	[CompilerGenerated]
	private sealed class b
	{
		public List<DataRow> a;

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

		public static Func<DataRow, string> b;

		public static Func<DataRow, object> c;

		public static Func<IGrouping<string, DataRow>, IEnumerable<DataRow>> d;

		internal string a(DataRow A_0)
		{
			return A_0["Type"].ToString();
		}

		internal IEnumerable<DataRow> a(IGrouping<string, DataRow> A_0)
		{
			return A_0.OrderByDescending((DataRow dataRow) => dataRow["Time"]).Take(8);
		}

		internal object b(DataRow A_0)
		{
			return A_0["Time"];
		}
	}

	private string m_a = "connectionhistory.xml";

	private string m_b = Environment.CurrentDirectory + "\\LogFile\\";

	private DataTable m_c = new DataTable("Table_ConnHistory");

	private DataView d;

	public e()
	{
		this.m_c.Columns.Add("Connection", Type.GetType("System.String"));
		this.m_c.Columns.Add("Time", Type.GetType("System.DateTime"));
		this.m_c.Columns.Add("Type", Type.GetType("System.String"));
		d = new DataView(this.m_c);
		d.Sort = "Type ASC, Time DESC";
		this.m_c.DefaultView.Sort = "Time DESC";
		if (File.Exists(this.m_b + this.m_a))
		{
			this.m_c.ReadXml(this.m_b + this.m_a);
		}
	}

	public void a(string A_0, DateTime A_1)
	{
		DataRow dataRow = this.m_c.Rows.Cast<DataRow>().FirstOrDefault((DataRow row) => row.Field<string>("Connection") == A_0);
		if (dataRow == null)
		{
			string text = A_0.Replace(" ", "").Split(',')[0].ToLower();
			if (!(text == "tcp"))
			{
				if (!(text == "serial"))
				{
					throw new ArgumentException("Unsupported communication type.");
				}
				this.m_c.Rows.Add(A_0, A_1, "Serial");
			}
			else
			{
				this.m_c.Rows.Add(A_0, A_1, "TCP");
			}
		}
		else
		{
			dataRow["Time"] = A_1;
			this.m_c.AcceptChanges();
		}
	}

	public void a(string A_0)
	{
		string filterExpression = "Connection = '" + A_0 + "'";
		DataRow[] array = this.m_c.Select(filterExpression);
		for (int num = 0; num < array.Length; num++)
		{
			array[num].Delete();
		}
		this.m_c.AcceptChanges();
	}

	public void a()
	{
		if (this.m_c.Rows.Count > 16)
		{
			this.m_c.DefaultView.Sort = "Time DESC";
			DateTime dateTime = (DateTime)this.m_c.DefaultView[15]["Time"];
			for (int num = this.m_c.Rows.Count - 1; num >= 0; num--)
			{
				if ((DateTime)this.m_c.Rows[num]["Time"] < dateTime)
				{
					this.m_c.Rows.RemoveAt(num);
				}
			}
			this.m_c.AcceptChanges();
		}
		List<DataRow> a2 = (from A_0 in this.m_c.AsEnumerable()
			group A_0 by A_0["Type"].ToString()).SelectMany((IGrouping<string, DataRow> A_0) => A_0.OrderByDescending((DataRow dataRow) => dataRow["Time"]).Take(8)).ToList();
		foreach (DataRow item in (from A_0 in this.m_c.AsEnumerable()
			where !a2.Contains(A_0)
			select A_0).ToList())
		{
			this.m_c.Rows.Remove(item);
		}
	}

	public void b()
	{
		a();
		this.m_c.WriteXml(this.m_b + this.m_a);
	}

	public DataView b(string A_0)
	{
		a();
		return d;
	}

	public DataView c(string A_0)
	{
		a();
		d.RowFilter = "Type = '" + A_0 + "'";
		return d;
	}
}
