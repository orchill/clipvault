$ErrorActionPreference = "Stop"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class ZR {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
  public static IntPtr H;
  public static bool Cb(IntPtr h, IntPtr l) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "ClipVaultPopup") { H = h; return false; } return true; }
  public static IntPtr Find() { H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
}
"@
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$popup = [ZR]::Find()
if ($popup -eq [IntPtr]::Zero) { throw "popup not found" }
[ZR]::PostMessageW($popup, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object ZR+RECT
[ZR]::GetWindowRect($popup, [ref]$r) | Out-Null
$x = $r.L; $y = $r.T + 110; $w = $r.R - $r.L; $hh = 240
$cap = New-Object System.Drawing.Bitmap $w, $hh
$g2 = [System.Drawing.Graphics]::FromImage($cap)
$g2.CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size ($w), ($hh)))
$g2.Dispose()
$big = New-Object System.Drawing.Bitmap ($w * 3), ($hh * 3)
$g3 = [System.Drawing.Graphics]::FromImage($big)
$g3.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g3.DrawImage($cap, 0, 0, $w * 3, $hh * 3)
$g3.Dispose()
$big.Save((Join-Path $PSScriptRoot '..\build\row_zoom.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$cap.Dispose(); $big.Dispose()
Write-Host "zoom saved"
[ZR]::PostMessageW($popup, 0x8003, [IntPtr]5, [IntPtr]::Zero) | Out-Null
