$ErrorActionPreference = "Stop"
# deterministic config: defaults (persist on, autoPaste off, monitor on)
Remove-Item (Join-Path $env:LOCALAPPDATA 'ClipVault\config.json') -Force -ErrorAction SilentlyContinue
$data = Join-Path $env:LOCALAPPDATA "ClipVault"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class F {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool OpenClipboard(IntPtr h);
  [DllImport("user32.dll")] public static extern bool CloseClipboard();
  [DllImport("user32.dll")] public static extern IntPtr GetClipboardData(uint f);
  [DllImport("kernel32.dll")] public static extern UIntPtr GlobalSize(IntPtr h);
  [DllImport("user32.dll")] public static extern uint EnumClipboardFormats(uint f);
  public static IntPtr H;
  public static bool Cb(IntPtr h, IntPtr l) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "ClipVaultPopup") { H = h; return false; } return true; }
  public static IntPtr Find() { H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
}
"@
function Stop-App { Get-Process ClipVault -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 600 }
function Launch {
  $p = Start-Process '$PSScriptRoot\..\ClipVault.exe' -PassThru -WindowStyle Hidden
  Start-Sleep -Milliseconds 1600
  if ($p.HasExited) { throw "app exited at launch" }
  return $p
}
function Set-ClipboardRetry([string]$v) {
  for ($i = 0; $i -lt 6; $i++) { try { Set-Clipboard -Value $v -ErrorAction Stop; return } catch { Start-Sleep -Milliseconds 400 } }
  throw "Set-Clipboard failed"
}

Write-Host "== restore: image (raw CF_DIB size check) =="
Stop-App
Set-Content (Join-Path $data "config.json") '{"autoPaste":false}' -Encoding UTF8
# deterministic: fresh history so the seeded image is the newest entry (row 0)
Remove-Item (Join-Path $data "items.json") -Force -ErrorAction SilentlyContinue
$p = Launch
& powershell -NoProfile -STA -ExecutionPolicy Bypass -File '$PSScriptRoot\sta_image.ps1'
# wait until the worker has committed the image item (big encodes take seconds)
$deadline = (Get-Date).AddSeconds(20)
while ((Get-Date) -lt $deadline) {
  $j = Get-Content (Join-Path $env:LOCALAPPDATA 'ClipVault\items.json') -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
  if ($j -and $j.Contains('"type":3')) { break }
  Start-Sleep -Milliseconds 400
}
$hwnd = [F]::Find()
[F]::PostMessageW($hwnd, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
[F]::PostMessageW($hwnd, 0x8003, [IntPtr]4, [IntPtr]::Zero) | Out-Null   # Enter -> restore image
Start-Sleep -Milliseconds 1200
$fmts = @()
if ([F]::OpenClipboard([IntPtr]::Zero)) {
  $hdib = [F]::GetClipboardData(8)     # CF_DIB
  $sz = if ($hdib -ne [IntPtr]::Zero) { [F]::GlobalSize($hdib).ToUInt64() } else { [uint64]0 }
  $f2 = 0
  while (($f2 = [F]::EnumClipboardFormats($f2)) -ne 0) { $fmts += $f2 }
  [F]::CloseClipboard() | Out-Null
    Write-Host ("  CF_DIB on clipboard: " + $sz + " bytes")
  Write-Host ("  formats: " + ($fmts -join ","))
  if ($sz -lt 1000) { throw "image DIB too small / missing" }
} else { throw "could not open clipboard for check" }
Write-Host "PASS: image restored (CF_DIB present)"
Stop-App

Write-Host "== pause monitoring =="
Set-Content (Join-Path $data "config.json") '{"autoPaste":false,"pauseMonitoring":true}' -Encoding UTF8
Remove-Item (Join-Path $data "items.json") -Force -ErrorAction SilentlyContinue
$p = Launch
Set-ClipboardRetry "paused marker should not be recorded"
Start-Sleep -Milliseconds 2500
$items = Get-Content (Join-Path $data "items.json") -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
if ($items -and $items.Contains("paused marker")) { throw "paused app still recorded clipboard" }
Write-Host "PASS: monitoring paused (nothing recorded)"
Stop-App

Write-Host "== resume monitoring =="
Set-Content (Join-Path $data "config.json") '{"autoPaste":false}' -Encoding UTF8
$p = Launch
Set-ClipboardRetry "resumed capture works"
Start-Sleep -Milliseconds 2500
$items = Get-Content (Join-Path $data "items.json") -Raw -Encoding UTF8
if (-not $items.Contains("resumed capture works")) { throw "capture did not resume" }
Write-Host "PASS: monitoring resumed"
Stop-App
Write-Host "ALL FINAL TESTS PASSED"
