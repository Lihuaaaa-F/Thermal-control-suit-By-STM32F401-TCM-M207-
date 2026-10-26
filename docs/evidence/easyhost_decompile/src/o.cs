using System;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Text;

public class o : g
{
	private TcpClient m_a;

	private NetworkStream m_e;

	private m f;

	private byte[] g;

	public bool a(string A_0, m A_1)
	{
		try
		{
			string[] array = A_0.Split(',')[1].Split(':');
			this.m_a = new TcpClient(array[0], int.Parse(array[1]));
			g = new byte[1024];
			f = A_1;
			this.m_e = this.m_a.GetStream();
			this.m_e.BeginRead(g, 0, g.Length, a, null);
		}
		catch
		{
		}
		return true;
	}

	private void a(IAsyncResult A_0)
	{
		try
		{
			int num = this.m_e.EndRead(A_0);
			if (num > 0)
			{
				string text = Encoding.UTF8.GetString(g);
				int length;
				while ((length = text.IndexOf('\r')) > 0)
				{
					text = text.Substring(0, length);
					f.a(text);
					byte[] bytes = Encoding.UTF8.GetBytes(text);
					num -= bytes.Length + 1;
					Array.Copy(g, 0, g, bytes.Length + 1, num);
				}
			}
			this.m_e.BeginRead(g, num, g.Length - num, a, null);
		}
		catch (Exception)
		{
		}
	}

	public void b()
	{
		if (this.m_e != null)
		{
			this.m_e.Close();
		}
		if (this.m_a != null)
		{
			this.m_a.Close();
		}
	}

	[SpecialName]
	public bool c()
	{
		if (this.m_a != null)
		{
			return this.m_a.Connected;
		}
		return false;
	}

	[SpecialName]
	public bool d()
	{
		return true;
	}

	public void e(string A_0)
	{
		if (this.m_e != null)
		{
			try
			{
				byte[] bytes = Encoding.UTF8.GetBytes(A_0 + "\r");
				this.m_e.Write(bytes, 0, bytes.Length);
			}
			catch (Exception)
			{
			}
		}
	}
}
