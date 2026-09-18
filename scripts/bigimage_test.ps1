$ErrorActionPreference = "Stop"
# deterministic config: defaults (persist on, autoPaste off, monitor on)
Remove-Item (Join-Path $env:LOCALAPPDATA 'ClipVault\config.json') -Force -ErrorAction SilentlyContinue
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class BI {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  public struct RECT { public int L, T, R, B; }
  public static IntPtr H;
  public static bool Cb(IntPtr h, IntPtr l) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "ClipVaultPopup") { H = h; return false; } return true; }
  public static IntPtr Find() { H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
  public static IntPtr P;
  public static bool Cb2(IntPtr h, IntPtr l) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "ClipVaultPreview") { P = h; return false; } return true; }
  public static IntPtr FindPreview() { P = IntPtr.Zero; EnumWindows(Cb2, IntPtr.Zero); return P; }
}
"@
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class DPI { [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v); }
"@
[DPI]::SetProcessDpiAwarenessContext([IntPtr](-4)) | Out-Null   # PerMonitorV2
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$p = Start-Process 'C:\Users\caner\.zcode\workspace\default\ClipVault.exe' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1600

# seed a FULL-DESKTOP-sized image (work area size)
$b = $null
$sta = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList '-NoProfile','-STA','-Command', 'Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing; $wa=[System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea; $b=New-Object System.Drawing.Bitmap $wa.Width, $wa.Height; $g=[System.Drawing.Graphics]::FromImage($b); $g.Clear([System.Drawing.Color]::FromArgb(30,60,110)); $f=New-Object System.Drawing.Font("Segoe UI", 48); $g.DrawString("FULL DESKTOP TEST IMAGE", $f, [System.Drawing.Brushes]::White, 200, 450); $do=New-Object System.Windows.Forms.DataObject; $do.SetImage($b); [Windows.Forms.Clipboard]::SetDataObject($do, $true)'
$sta.WaitForExit()
Start-Sleep -Milliseconds 3000   # capture + worker encode of the big image

$hwnd = [BI]::Find()
[BI]::PostMessageW($hwnd, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object BI+RECT
[BI]::GetWindowRect($hwnd, [ref]$r) | Out-Null
# find the image row: probe downward until the hover preview window appears
$previewShown = $false
$py = 120
while ($py -lt 500) {
  [BI]::SetCursorPos($r.L + [int](($r.R - $r.L) * 0.45), $r.T + $py) | Out-Null
  Start-Sleep -Milliseconds 1900
  $pv = [BI]::FindPreview()
  if ($pv -ne [IntPtr]::Zero -and $pv -ne $null) { $previewShown = $true; break }
  $py += 30
}
Write-Host ("preview shown: " + $previewShown + " at probe y=" + $py)
if (-not $previewShown) { throw "hover preview never appeared" }

# capture the full screen (preview may be large/centered)
$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
$g.Dispose()
$bmp.Save('C:\Users\caner\.zcode\workspace\default\build\bigimage_hover.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "hover capture saved"
Stop-Process -Id $p.Id -Force
Write-Host "done"
