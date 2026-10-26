using System.CodeDom.Compiler;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.Resources;
using System.Runtime.CompilerServices;

[CompilerGenerated]
[GeneratedCode("System.Resources.Tools.StronglyTypedResourceBuilder", "4.0.0.0")]
[DebuggerNonUserCode]
internal class q
{
	private static ResourceManager m_a;

	private static CultureInfo m_b;

	internal q()
	{
	}

	[SpecialName]
	internal static ResourceManager c()
	{
		if (q.m_a == null)
		{
			q.m_a = new ResourceManager("q", typeof(q).Assembly);
		}
		return q.m_a;
	}

	[SpecialName]
	internal static CultureInfo b()
	{
		return q.m_b;
	}

	[SpecialName]
	internal static void a(CultureInfo A_0)
	{
		q.m_b = A_0;
	}

	[SpecialName]
	internal static Icon a()
	{
		return (Icon)c().GetObject("shine", q.m_b);
	}
}
