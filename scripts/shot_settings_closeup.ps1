$ErrorActionPreference = "Stop"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class SC {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int hh, uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, R, B; }
  public static IntPtr H;
  public static string Want = "ClipVaultSettings";
  public static bool Cb(IntPtr h, IntPtr l) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == Want) { H = h; return false; } return true; }
  public static IntPtr Find() { H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
  public static IntPtr FindPopup() { Want = "ClipVaultPopup"; IntPtr r = Find(); Want = "ClipVaultSettings"; return r; }
  public static IntPtr FindSettings() { Want = "ClipVaultSettings"; return Find(); }
}
"@
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$p = Start-Process '$PSScriptRoot\..\ClipVault.exe' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1600
$popup = [SC]::FindPopup()
Write-Host ("popup: " + $popup)
[SC]::PostMessageW($popup, 0x8006, [IntPtr]::Zero, [IntPtr]0x434C5631) | Out-Null
Start-Sleep -Milliseconds 900
$h = [SC]::FindSettings()
$r = New-Object SC+RECT
[SC]::GetWindowRect($h, [ref]$r) | Out-Null
[SC]::SetWindowPos($h, [IntPtr]::Zero, 350, 120, 0, 0, 0x0001 -bor 0x0004) | Out-Null  # NOSIZE|NOZORDER
Start-Sleep -Milliseconds 600
[SC]::GetWindowRect($h, [ref]$r) | Out-Null
Write-Host ("settings rect: " + $r.L + "," + $r.T + " - " + $r.R + "," + $r.B)

# capture the window area with margin, upscale 2x for close inspection
$m = 12
$w = $r.R - $r.L + $m * 2
$hh = $r.B - $r.T + $m * 2
$bmp = New-Object System.Drawing.Bitmap ($w * 2), ($hh * 2)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.CopyFromScreen($r.L - $m, $r.T - $m, 0, 0, (New-Object System.Drawing.Size ($w), ($hh)))
$g.Dispose()
$big = New-Object System.Drawing.Bitmap ($w * 2), ($hh * 2)
$g2 = [System.Drawing.Graphics]::FromImage($big)
$g2.DrawImage($bmp, 0, 0, $w * 2, $hh * 2)
$g2.Dispose()
$big.Save('$PSScriptRoot\..\build\settings_closeup.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose(); $big.Dispose()
Write-Host "closeup saved"
Stop-Process -Id $p.Id -Force
