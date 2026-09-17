$ErrorActionPreference = "Stop"
$data = Join-Path $env:LOCALAPPDATA "ClipVault"
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class R4 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
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
function Show-Popup($hwnd) { [R4]::PostMessageW($hwnd, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 700 }

Write-Host "== restore: image =="
Stop-App
$p = Launch
& powershell -NoProfile -STA -ExecutionPolicy Bypass -File '$PSScriptRoot\sta_image.ps1'
Start-Sleep -Milliseconds 2200   # capture + worker encode + thumbnail
$hwnd = [R4]::Find()
Show-Popup $hwnd
[R4]::PostMessageW($hwnd, 0x8003, [IntPtr]4, [IntPtr]::Zero) | Out-Null   # Enter restores top item (the image)
Start-Sleep -Milliseconds 1000
$sta = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList '-NoProfile','-STA','-ExecutionPolicy','Bypass','-Command', 'Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing; if ([Windows.Forms.Clipboard]::ContainsImage()) { $img = [Windows.Forms.Clipboard]::GetImage(); $img.Save("$PSScriptRoot\..\build\restored_image.png"); Write-Host ("restored image: " + $img.Width + "x" + $img.Height); exit 0 } else { Write-Host "NO IMAGE ON CLIPBOARD"; exit 1 }'
$sta.WaitForExit()
if ($sta.ExitCode -ne 0) { throw "image restore failed" }
Write-Host "PASS: image restored to clipboard"
Stop-App

Write-Host "== pause monitoring =="
Set-Content (Join-Path $data "config.json") '{"autoPaste":false,"pauseMonitoring":true}' -Encoding UTF8
$before = Get-Content (Join-Path $data "items.json") -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
$p = Launch
Set-ClipboardRetry "paused marker should not be recorded"
Start-Sleep -Milliseconds 2500
$after = Get-Content (Join-Path $data "items.json") -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
if ($after -and $after.Contains("paused marker")) { throw "paused app still recorded clipboard" }
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
Write-Host "IMAGE + PAUSE + RESUME: ALL PASSED"
