$ErrorActionPreference = "Stop"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class RZ {
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
}
"@
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$p = Start-Process '$PSScriptRoot\..\ClipVault.exe' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1600
$popup = [RZ]::FindPopup()
[RZ]::PostMessageW($popup, 0x8006, [IntPtr]::Zero, [IntPtr]0x434C5631) | Out-Null
Start-Sleep -Milliseconds 900
$h = [RZ]::Find()
Write-Host ("settings hwnd: " + $h)
[RZ]::SetWindowPos($h, [IntPtr]::Zero, 200, 100, 1300, 950, 0x0004) | Out-Null   # resize wide
Start-Sleep -Milliseconds 900
$r = New-Object RZ+RECT
[RZ]::GetWindowRect($h, [ref]$r) | Out-Null
# capture the whole window
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size ($w), ($hh)))
$g.Dispose()
$bmp.Save('$PSScriptRoot\..\build\settings_wide.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "wide shot saved"

# resize back to default and capture bottom row (button anchors)
[RZ]::SetWindowPos($h, [IntPtr]::Zero, 200, 100, 0, 0, 0x0001 -bor 0x0004) | Out-Null  # NOSIZE restore? keep size reset via full size:
Start-Sleep -Milliseconds 600
[RZ]::GetWindowRect($h, [ref]$r) | Out-Null
[RZ]::SetWindowPos($h, [IntPtr]::Zero, 200, 100, 500, 770, 0x0004) | Out-Null
Start-Sleep -Milliseconds 800
[RZ]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size ($w), ($hh)))
$g.Dispose()
$bmp.Save('$PSScriptRoot\..\build\settings_normal.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "normal shot saved"
Stop-Process -Id $p.Id -Force
