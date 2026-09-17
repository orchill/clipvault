$ErrorActionPreference = "Stop"
$data = Join-Path $env:LOCALAPPDATA "ClipVault"
$exe = '$PSScriptRoot\..\ClipVault.exe'

Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class R {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  public static IntPtr H;
  public static bool Cb(IntPtr h, IntPtr l) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == "ClipVaultPopup") { H = h; return false; } return true; }
  public static IntPtr Find() { H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
}
"@

function Stop-App { Get-Process ClipVault -ErrorAction SilentlyContinue | Stop-Process -Force; Start-Sleep -Milliseconds 600 }
function Launch {
  $p = Start-Process $exe -PassThru -WindowStyle Hidden
  Start-Sleep -Milliseconds 1600
  if ($p.HasExited) { throw "app exited at launch" }
  return $p
}
function Set-ClipboardRetry([string]$v) {
  for ($i = 0; $i -lt 6; $i++) { try { Set-Clipboard -Value $v -ErrorAction Stop; return } catch { Start-Sleep -Milliseconds 400 } }
  throw "Set-Clipboard failed"
}

Write-Host "== restore: text =="
Stop-App
# autoPaste off so no keys are ever sent to the foreground app
Set-Content (Join-Path $data "config.json") '{"autoPaste":false}' -Encoding UTF8
Remove-Item (Join-Path $data "items.json") -Force -ErrorAction SilentlyContinue
$p = Launch
Set-ClipboardRetry "restore test A"
Start-Sleep -Milliseconds 900
$hwnd = [R]::Find()
[R]::PostMessageW($hwnd, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null      # WM_HOTKEY -> show
Start-Sleep -Milliseconds 700
if (-not [R]::IsWindowVisible($hwnd)) { throw "popup not visible" }
# first row is "restore test A"; activate it (Enter)
[R]::PostMessageW($hwnd, 0x8003, [IntPtr]4, [IntPtr]::Zero) | Out-Null      # WM_APP_KEY Enter
Start-Sleep -Milliseconds 700
if ([R]::IsWindowVisible($hwnd)) { throw "popup should hide after activation" }
$got = Get-Clipboard -Raw
Write-Host ("clipboard after restore: '" + $got + "'")
if ($got -ne "restore test A") { throw "text restore failed: got '$got'" }
Write-Host "PASS: text restored to clipboard"
Stop-App

Write-Host "== restore: image =="
& powershell -NoProfile -STA -ExecutionPolicy Bypass -File '$PSScriptRoot\sta_image.ps1'
Start-Sleep -Milliseconds 2000   # capture + worker encode
$p = Launch   # restart so history top = image item, popup fresh
Start-Sleep -Milliseconds 500
$hwnd = [R]::Find()
[R]::PostMessageW($hwnd, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
[R]::PostMessageW($hwnd, 0x8003, [IntPtr]4, [IntPtr]::Zero) | Out-Null      # Enter -> restore image
Start-Sleep -Milliseconds 900
$sta = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList '-NoProfile','-STA','-ExecutionPolicy','Bypass','-Command', 'Add-Type -AssemblyName System.Windows.Forms; Add-Type -AssemblyName System.Drawing; if ([Windows.Forms.Clipboard]::ContainsImage()) { $img = [Windows.Forms.Clipboard]::GetImage(); $img.Save("$PSScriptRoot\..\build\restored_image.png"); Write-Host ("restored image: " + $img.Width + "x" + $img.Height) } else { Write-Host "NO IMAGE ON CLIPBOARD"; exit 1 }'
$sta.WaitForExit()
if ($sta.ExitCode -ne 0) { throw "image restore failed" }
Write-Host "PASS: image restored to clipboard"
Stop-App

Write-Host "== pause monitoring =="
Set-Content (Join-Path $data "config.json") '{"autoPaste":false,"pauseMonitoring":true}' -Encoding UTF8
$itemsBefore = Get-Content (Join-Path $data "items.json") -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
$p = Launch
Set-ClipboardRetry "paused marker should not be recorded"
Start-Sleep -Milliseconds 2500
$itemsAfter = Get-Content (Join-Path $data "items.json") -Raw -Encoding UTF8 -ErrorAction SilentlyContinue
if ($itemsAfter -and $itemsAfter.Contains("paused marker")) { throw "paused app still recorded clipboard" }
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
Write-Host "ALL RESTORE/PAUSE TESTS PASSED"
