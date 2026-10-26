using System;
using System.ComponentModel;
using System.IO.Ports;
using System.Runtime.CompilerServices;
using System.Text;

public class n : g
{
	private SerialPort m_a;

	private m m_e;

	private BackgroundWorker f;

	public bool a(string A_0, m A_1)
	{
		try
		{
			string[] array = A_0.Split(',');
			if (this.m_a == null)
			{
				this.m_a = new SerialPort(array[1], int.Parse(array[2]));
			}
			else
			{
				this.m_a.PortName = array[1];
				this.m_a.BaudRate = int.Parse(array[2]);
			}
			this.m_a.StopBits = StopBits.One;
			this.m_a.Parity = Parity.None;
			this.m_a.Encoding = Encoding.UTF8;
			this.m_a.NewLine = "\r";
			this.m_a.WriteTimeout = 50;
			this.m_a.ReadTimeout = 100;
			this.m_a.ReceivedBytesThreshold = 1;
			this.m_a.ReadBufferSize = 1024;
			this.m_a.WriteBufferSize = 1024;
			this.m_a.Open();
			this.m_a.DataReceived += a;
			f = new BackgroundWorker();
			f.WorkerReportsProgress = false;
			f.WorkerSupportsCancellation = false;
			f.DoWork += a;
			this.m_e = A_1;
			return true;
		}
		catch
		{
			return false;
		}
	}

	private void a(object A_0, SerialDataReceivedEventArgs A_1)
	{
		try
		{
			string text = this.m_a.ReadTo("\r");
			if (text.Length > 0)
			{
				this.m_e.a(text);
			}
		}
		catch (Exception)
		{
		}
	}

	public void b()
	{
		this.m_a.Close();
	}

	[SpecialName]
	public bool c()
	{
		if (this.m_a != null)
		{
			return this.m_a.IsOpen;
		}
		return false;
	}

	[SpecialName]
	public bool d()
	{
		if (this.m_a.BytesToWrite > 0)
		{
			return false;
		}
		return true;
	}

	public void e(string A_0)
	{
		if (this.m_a != null)
		{
			try
			{
				this.m_a.WriteLine(A_0);
			}
			catch (Exception)
			{
			}
		}
	}

	public void a(string A_0)
	{
		if (this.m_a.IsOpen)
		{
			f.RunWorkerAsync(A_0);
		}
	}

	private void a(object A_0, DoWorkEventArgs A_1)
	{
		string text = (string)A_1.Argument;
		if (this.m_a.IsOpen)
		{
			this.m_a.WriteLine(text);
		}
	}
}
