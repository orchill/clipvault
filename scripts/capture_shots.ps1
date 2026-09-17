$ErrorActionPreference = "Stop"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class SH {
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

function Shot-Rect([System.Drawing.RectangleF]$r, [string]$out) {
  $w = [int]($r.Right - $r.Left); $hh = [int]($r.Bottom - $r.Top)
  $bmp = New-Object System.Drawing.Bitmap $w, $hh
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen([int]$r.Left, [int]$r.Top, 0, 0, (New-Object System.Drawing.Size ($w), ($hh)))
  $g.Dispose()
  $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
}

$root = Split-Path -Parent $PSScriptRoot
$p = Start-Process (Join-Path $root "ClipVault.exe") -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1600

# seed a couple of nice history entries for the screenshot
for ($i = 0; $i -lt 6; $i++) { try { Set-Clipboard -Value "ClipVault keeps everything you copy — text, links, code, images" -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 300 } }
Start-Sleep -Milliseconds 700
for ($i = 0; $i -lt 6; $i++) { try { Set-Clipboard -Value "build.bat  ->  one 1 MB exe, no dependencies" -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 300 } }
Start-Sleep -Milliseconds 700
for ($i = 0; $i -lt 6; $i++) { try { Set-Clipboard -Value "https://github.com/" -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 300 } }
Start-Sleep -Milliseconds 700

# open popup and capture
$popup = [SH]::Find("ClipVaultPopup")
[SH]::PostMessageW($popup, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
$r = New-Object SH+RECT
[SH]::GetWindowRect($popup, [ref]$r) | Out-Null
Shot-Rect (New-Object System.Drawing.RectangleF $r.L, $r.T, ($r.R - $r.L), ($r.B - $r.T)) (Join-Path $root "screenshots\popup.png")
Write-Host "popup captured"

# close popup, open settings, capture
[SH]::PostMessageW($popup, 0x8003, [IntPtr]5, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 400
[SH]::PostMessageW($popup, 0x8006, [IntPtr]::Zero, [IntPtr]0x434C5631) | Out-Null
Start-Sleep -Milliseconds 1000
$settings = [SH]::Find("ClipVaultSettings")
$r = New-Object SH+RECT
[SH]::GetWindowRect($settings, [ref]$r) | Out-Null
Shot-Rect (New-Object System.Drawing.RectangleF $r.L, $r.T, ($r.R - $r.L), ($r.B - $r.T)) (Join-Path $root "screenshots\settings.png")
Write-Host "settings captured"

[SH]::PostMessageW($settings, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 300
Stop-Process -Id $p.Id -Force
Write-Host "done"
