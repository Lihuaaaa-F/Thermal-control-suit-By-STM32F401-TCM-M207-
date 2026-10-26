using System;

public static class d
{
	public static g a(string A_0, m A_1)
	{
		A_0 = A_0.Replace(" ", "");
		string text = A_0.Split(',')[0].ToLower();
		if (!(text == "tcp"))
		{
			if (text == "serial")
			{
				n obj = new n();
				obj.a(A_0, A_1);
				return obj;
			}
			throw new ArgumentException("Unsupported communication type.");
		}
		o obj2 = new o();
		obj2.a(A_0, A_1);
		return obj2;
	}
}
