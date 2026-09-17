$ErrorActionPreference = "Stop"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class RS {
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

function Stop-App { Get-Process ClipVault -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 600 }
function Set-ClipboardRetry([string]$v) {
  for ($i = 0; $i -lt 6; $i++) { try { Set-Clipboard -Value $v -ErrorAction Stop; return } catch { Start-Sleep -Milliseconds 400 } }
  throw "Set-Clipboard failed"
}

Stop-App
$data = Join-Path $env:LOCALAPPDATA "ClipVault"
Remove-Item (Join-Path $data "items.json") -Force -ErrorAction SilentlyContinue
Get-ChildItem (Join-Path $data "blobs") -File -ErrorAction SilentlyContinue | Remove-Item -Force

$p = Start-Process 'C:\Users\caner\.zcode\workspace\default\ClipVault.exe' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1600
$popup = [RS]::Find("ClipVaultPopup")

# 1. github link -> pin it (shows pinned section + pin indicator in the shot)
Set-ClipboardRetry "https://github.com/orchill/clipvault"
Start-Sleep -Milliseconds 800
[RS]::PostMessageW($popup, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
[RS]::PostMessageW($popup, 0x8003, [IntPtr]7, [IntPtr]::Zero) | Out-Null   # Ctrl+P -> pin
Start-Sleep -Milliseconds 400
[RS]::PostMessageW($popup, 0x8003, [IntPtr]5, [IntPtr]::Zero) | Out-Null   # Esc -> hide
Start-Sleep -Milliseconds 500

# 2. demo image (worker encodes it off-thread)
& powershell -NoProfile -STA -ExecutionPolicy Bypass -File 'C:\Users\caner\.zcode\workspace\default\scripts\sta_image.ps1'
Start-Sleep -Milliseconds 2200

# 3. emoji / fancy unicode row
Set-ClipboardRetry "Unicode safe: Ｔｕｌｌｗｉｄｔｈ 你好 Привет مرحبا 𝓗𝓮𝓵𝓵𝓸 😀🚀"
Start-Sleep -Milliseconds 800

# 4. code row
Set-ClipboardRetry "for (auto& entry : history) pin(entry);"
Start-Sleep -Milliseconds 800

# 5. tagline at top
Set-ClipboardRetry "ClipVault keeps everything you copy — text, code, links, images"
Start-Sleep -Milliseconds 900

# open popup and capture the window rect only
$popup = [RS]::Find("ClipVaultPopup")
[RS]::PostMessageW($popup, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
$r = New-Object RS+RECT
[RS]::GetWindowRect($popup, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size ($w), ($hh)))
$g.Dispose()
$bmp.Save('C:\Users\caner\.zcode\workspace\default\screenshots\popup.png', [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
Write-Host "popup re-captured"
[RS]::PostMessageW($popup, 0x8003, [IntPtr]5, [IntPtr]::Zero) | Out-Null
Stop-Process -Id $p.Id -Force
Write-Host "done"
