using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;

[CompilerGenerated]
internal sealed class a<a, b, c, d, e, f>
{
	[DebuggerBrowsable(DebuggerBrowsableState.Never)]
	private readonly a m_a;

	[DebuggerBrowsable(DebuggerBrowsableState.Never)]
	private readonly b b;

	[DebuggerBrowsable(DebuggerBrowsableState.Never)]
	private readonly c c;

	[DebuggerBrowsable(DebuggerBrowsableState.Never)]
	private readonly d d;

	[DebuggerBrowsable(DebuggerBrowsableState.Never)]
	private readonly e e;

	[DebuggerBrowsable(DebuggerBrowsableState.Never)]
	private readonly f f;

	public a Model => this.a;

	public b HardwareMajor => this.b;

	public c HardwareMinor => this.c;

	public d FirmwareMajor => this.d;

	public e FirmwareMinor => this.e;

	public f BuildDate => this.f;

	[DebuggerHidden]
	public a(a A_0, b A_1, c A_2, d A_3, e A_4, f A_5)
	{
		this.a = A_0;
		this.b = A_1;
		this.c = A_2;
		this.d = A_3;
		this.e = A_4;
		this.f = A_5;
	}

	[DebuggerHidden]
	public override bool Equals(object value)
	{
		if (value is global::a<a, b, c, d, e, f> a2 && EqualityComparer<a>.Default.Equals(this.a, a2.a) && EqualityComparer<b>.Default.Equals(this.b, a2.b) && EqualityComparer<c>.Default.Equals(this.c, a2.c) && EqualityComparer<d>.Default.Equals(this.d, a2.d) && EqualityComparer<e>.Default.Equals(this.e, a2.e))
		{
			return EqualityComparer<f>.Default.Equals(this.f, a2.f);
		}
		return false;
	}

	[DebuggerHidden]
	public override int GetHashCode()
	{
		return (((((1253409733 * -1521134295 + EqualityComparer<a>.Default.GetHashCode(this.a)) * -1521134295 + EqualityComparer<b>.Default.GetHashCode(this.b)) * -1521134295 + EqualityComparer<c>.Default.GetHashCode(this.c)) * -1521134295 + EqualityComparer<d>.Default.GetHashCode(this.d)) * -1521134295 + EqualityComparer<e>.Default.GetHashCode(this.e)) * -1521134295 + EqualityComparer<f>.Default.GetHashCode(this.f);
	}

	[DebuggerHidden]
	public override string ToString()
	{
		return string.Format(null, "{{ Model = {0}, HardwareMajor = {1}, HardwareMinor = {2}, FirmwareMajor = {3}, FirmwareMinor = {4}, BuildDate = {5} }}", this.a?.ToString(), this.b?.ToString(), this.c?.ToString(), this.d?.ToString(), this.e?.ToString(), this.f?.ToString());
	}
}
