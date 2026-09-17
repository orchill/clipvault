$ErrorActionPreference = "Stop"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class HV {
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
}
"@
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$p = Start-Process 'C:\Users\caner\.zcode\workspace\default\ClipVault.exe' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1600
# seed an image entry (app is running -> it gets captured)
& powershell -NoProfile -STA -ExecutionPolicy Bypass -File 'C:\Users\caner\.zcode\workspace\default\scripts\sta_image.ps1'
Start-Sleep -Milliseconds 2500

$hwnd = [HV]::Find()
[HV]::PostMessageW($hwnd, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
$r = New-Object HV+RECT
[HV]::GetWindowRect($hwnd, [ref]$r) | Out-Null
# hover over row 0 (the image item) center, left of the action buttons
$hx = $r.L + [int](($r.R - $r.L) * 0.45)
$hy = $r.T + 130
[HV]::SetCursorPos($hx, $hy) | Out-Null
Start-Sleep -Milliseconds 2300   # dwell 1.5s + margin

# capture popup + area to its right where the preview should appear
$capW = ($r.R - $r.L) + 520
$capH = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $capW, $capH
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size ($capW), ($capH)))
$g.Dispose()
$bmp.Save('C:\Users\caner\.zcode\workspace\default\build\hover_test.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "hover capture saved"
Stop-Process -Id $p.Id -Force
