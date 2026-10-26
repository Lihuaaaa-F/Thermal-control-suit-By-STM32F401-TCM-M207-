public class m
{
	private static int m_a = 64;

	private string[] m_b = new string[m.m_a];

	private int m_c;

	private int m_d;

	private bool b()
	{
		return this.m_c == this.m_d;
	}

	private bool a()
	{
		return (this.m_d + 1) % m.m_a == this.m_c;
	}

	public bool a(string A_0)
	{
		if (a())
		{
			return false;
		}
		this.m_b[this.m_d] = A_0;
		this.m_d = (this.m_d + 1) % m.m_a;
		return true;
	}

	public string c()
	{
		if (b())
		{
			return null;
		}
		string result = this.m_b[this.m_c];
		this.m_c = (this.m_c + 1) % m.m_a;
		return result;
	}

	public void d()
	{
		this.m_c = this.m_d;
	}
}
