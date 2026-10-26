using System;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using JoyD.Windows.CS;

public static class p
{
	public static byte[] a(char A_0, params object[] A_1)
	{
		using MemoryStream memoryStream = new MemoryStream();
		memoryStream.WriteByte((byte)A_0);
		foreach (object obj in A_1)
		{
			if (obj is long num2)
			{
				byte value = (byte)((num2 >> 16) & 0xFF);
				byte value2 = (byte)((num2 >> 8) & 0xFF);
				byte value3 = (byte)(num2 & 0xFF);
				memoryStream.WriteByte(value);
				memoryStream.WriteByte(value2);
				memoryStream.WriteByte(value3);
			}
			else if (obj is int num3)
			{
				byte value4 = (byte)((num3 >> 8) & 0xFF);
				byte value5 = (byte)(num3 & 0xFF);
				memoryStream.WriteByte(value4);
				memoryStream.WriteByte(value5);
			}
			else if (obj is int num4)
			{
				byte value6 = (byte)((num4 >>> 24) & 0xFF);
				byte value7 = (byte)((num4 >>> 16) & 0xFF);
				byte value8 = (byte)((num4 >>> 8) & 0xFF);
				byte value9 = (byte)(num4 & 0xFF);
				memoryStream.WriteByte(value6);
				memoryStream.WriteByte(value7);
				memoryStream.WriteByte(value8);
				memoryStream.WriteByte(value9);
			}
			else if (obj is string)
			{
				string s = (string)obj;
				byte[] bytes = Encoding.ASCII.GetBytes(s);
				memoryStream.Write(bytes, 0, bytes.Length);
			}
			else if (obj is byte[])
			{
				byte[] array = (byte[])obj;
				memoryStream.Write(array, 0, array.Length);
			}
		}
		byte[] array2 = memoryStream.ToArray();
		ushort num5 = Utils.CalculateCRC16(array2, 0, array2.Length);
		byte value10 = (byte)((num5 >> 8) & 0xFF);
		byte value11 = (byte)(num5 & 0xFF);
		memoryStream.WriteByte(value10);
		memoryStream.WriteByte(value11);
		return memoryStream.ToArray();
	}

	public static string a(byte[] A_0)
	{
		string text = Convert.ToBase64String(A_0);
		if (!a(text))
		{
			throw new Exception("检测到非法Base64字符");
		}
		return "set(LFMGR: FTPC, \"" + text + "\")";
	}

	public static bool a(string A_0)
	{
		if (string.IsNullOrEmpty(A_0))
		{
			return false;
		}
		string pattern = "^[A-Za-z0-9+/]+[=]{0,2}$";
		return Regex.IsMatch(A_0, pattern);
	}
}
