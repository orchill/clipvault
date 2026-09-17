$ErrorActionPreference = "Stop"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class RT {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
  public static IntPtr H;
  public static string Want = "";
  public static bool Cb(IntPtr h, IntPtr l) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == Want) { H = h; return false; } return true; }
  public static IntPtr Find(string cls) { Want = cls; H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
}
"@
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$popup = [RT]::Find("ClipVaultPopup")
if ($popup -eq [IntPtr]::Zero) { throw "app not running" }
[RT]::PostMessageW($popup, 0x8006, [IntPtr]::Zero, [IntPtr]0x434C5631) | Out-Null
Start-Sleep -Milliseconds 1100
$settings = [RT]::Find("ClipVaultSettings")
$r = New-Object RT+RECT
[RT]::GetWindowRect($settings, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size ($w), ($hh)))
$g.Dispose()
$bmp.Save('C:\Users\caner\.zcode\workspace\default\screenshots\settings.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "settings re-captured ($w x $hh)"
[RT]::PostMessageW($settings, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
