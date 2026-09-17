Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class CB {
  [DllImport("user32.dll")] public static extern bool OpenClipboard(IntPtr h);
  [DllImport("user32.dll")] public static extern bool CloseClipboard();
  [DllImport("user32.dll")] public static extern uint EnumClipboardFormats(uint f);
  [DllImport("user32.dll")] public static extern int GetClipboardFormatName(uint f, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool IsClipboardFormatAvailable(uint f);
}
"@
[CB]::OpenClipboard([IntPtr]::Zero) | Out-Null
$f = 0
$names = @()
while (($f = [CB]::EnumClipboardFormats($f)) -ne 0) {
  if ($f -ge 0xC000) {
    $sb = New-Object System.Text.StringBuilder 256
    [CB]::GetClipboardFormatName($f, $sb, 256) | Out-Null
    $names += $sb.ToString()
  } else {
    $names += "std:$f"
  }
}
[CB]::CloseClipboard() | Out-Null
Write-Host ("RAW FORMATS: " + ($names -join " | "))
