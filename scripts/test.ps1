# test.ps1 - automated smoke/perf tests for ClipVault.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = "Stop"
$exe = Join-Path $Root "ClipVault.exe"
$data = Join-Path $env:LOCALAPPDATA "ClipVault"
$shots = Join-Path $Root "build"

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public class W {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t);
  public static IntPtr Hwnd;
  public static bool Cb(IntPtr h, IntPtr l) {
    var sb = new StringBuilder(64);
    GetClassName(h, sb, 64);
    if (sb.ToString() == "ClipVaultPopup") { Hwnd = h; return false; }
    return true;
  }
  public static IntPtr Find() {
    Hwnd = IntPtr.Zero;
    EnumWindows(Cb, IntPtr.Zero);
    return Hwnd;
  }
  public static bool Visible() {
    IntPtr h = Find();
    return h != IntPtr.Zero && IsWindowVisible(h);
  }
}
"@

if (-not (Test-Path $exe)) { throw "exe not found: $exe" }
if (Test-Path "$data\items.json") { Remove-Item "$data\items.json" -Force }
if (Test-Path "$data\blobs") { Remove-Item "$data\blobs\*" -Force }

Write-Host "== 1. launch =="
$proc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1500
if ($proc.HasExited) { throw "App exited early: code $($proc.ExitCode)" }
Write-Host "PASS: process running, PID $($proc.Id)"

function Set-ClipboardRetry([string]$value) {
  for ($i = 0; $i -lt 6; $i++) {
    try { Set-Clipboard -Value $value -ErrorAction Stop; return } catch { Start-Sleep -Milliseconds 400 }
  }
  throw "Set-Clipboard failed repeatedly for: $value"
}

Write-Host "== 2. text/unicode/emoji capture =="
Set-ClipboardRetry "hello world from test"
Start-Sleep -Milliseconds 500
Set-ClipboardRetry "héllo wörld ünïcode ✓ 𝓗𝓮𝓵𝓵𝓸 Ｆｕｌｌｗｉｄｔｈ"
Start-Sleep -Milliseconds 500
Set-ClipboardRetry "emoji test 😀🎉🚀 ok"
Start-Sleep -Milliseconds 500
Set-ClipboardRetry ("long text " + ("x" * 200000))
Start-Sleep -Milliseconds 800
Set-ClipboardRetry "hello world from test"   # duplicate: should move to newest, not add
Start-Sleep -Milliseconds 800

Write-Host "== 3. RTF + image capture (STA) =="
powershell -NoProfile -STA -ExecutionPolicy Bypass -File (Join-Path $Root "scripts\sta_rtf.ps1")
Start-Sleep -Milliseconds 800
powershell -NoProfile -STA -ExecutionPolicy Bypass -File (Join-Path $Root "scripts\sta_image.ps1")
Start-Sleep -Milliseconds 500
Start-Sleep -Milliseconds 2500   # image worker + persist debounce

Write-Host "== 4. persistence check =="
$items = Get-Content (Join-Path $data "items.json") -Raw -Encoding UTF8
$failures = @()
foreach ($needle in @("hello world from test", "𝓗𝓮𝓵𝓵𝓸", "Ｆｕｌｌｗｉｄｔｈ", "😀", "bold and italic and colored text")) {
  if ($items.Contains($needle)) { Write-Host "  found: $needle" } else { $failures += $needle }
}
if ($failures.Count) { throw "MISSING from items.json: $($failures -join ', ')" }
$blobs = @(Get-ChildItem (Join-Path $data "blobs") -File -ErrorAction SilentlyContinue)
$imgBlobs = @($blobs | Where-Object { $_.Extension -match "\.(png|jpg)$" })
$rtfBlobs = @($blobs | Where-Object { $_.Extension -eq ".rtf" })
$items = $items + "|" + (($rtfBlobs | ForEach-Object { $_.Name }) -join ",")
Write-Host ("  blobs: {0} file(s), {1} image(s)" -f $blobs.Count, $imgBlobs.Count)
if (-not $imgBlobs.Count) { throw "no image blob stored" }
# duplicate check: "hello world from test" must appear exactly once
$occurrences = ([regex]::Matches($items, [regex]::Escape("hello world from test"))).Count
Write-Host "  duplicate occurrences in items.json: $occurrences"
if ($occurrences -ne 1) { throw "duplicate handling failed ($occurrences occurrences)" }
Write-Host "PASS: capture + persistence"

Write-Host "== 5. idle resource usage =="
$proc.Refresh()
$cpu1 = $proc.TotalProcessorTime.TotalMilliseconds
$ws = $proc.WorkingSet64
$privateMem = $proc.PrivateMemorySize64
Start-Sleep -Seconds 6
$proc.Refresh()
$cpuDelta = $proc.TotalProcessorTime.TotalMilliseconds - $cpu1
Write-Host ("  WorkingSet={0:N1} MB, Private={1:N1} MB, CPU over 6s idle={2:N0} ms" -f ($ws/1MB), ($privateMem/1MB), $cpuDelta)
if ($cpuDelta -gt 250) { throw "idle CPU too high: $cpuDelta ms / 6s" }
Write-Host "PASS: idle usage"

Write-Host "== 6. hotkey opens popup =="
# trigger the hotkey path directly (no global keyboard input)
$hwnd = [W]::Find()
if ($hwnd -eq [IntPtr]::Zero) { throw "main window not found" }
[W]::PostMessageW($hwnd, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null   # WM_HOTKEY
Start-Sleep -Milliseconds 900
$visible = [W]::Visible()
if (-not $visible) { throw "popup did not appear" }
Write-Host "PASS: popup visible"

# screenshot for visual review
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
$bmp.Save((Join-Path $shots "popup.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host "  screenshot saved to build\popup.png"

# type a search query via WM_CHAR posts to the search edit (no global input)
$edit = [W]::FindWindowExW($hwnd, [IntPtr]::Zero, "EDIT", $null)
foreach ($ch in "hello".ToCharArray()) {
  [W]::PostMessageW($edit, 0x0102, [IntPtr][char]$ch, [IntPtr]::Zero) | Out-Null   # WM_CHAR
  Start-Sleep -Milliseconds 60
}
Start-Sleep -Milliseconds 700
$bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
$bmp.Save((Join-Path $shots "popup_search.png"), [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
# hide popup via Esc key command
[W]::PostMessageW($hwnd, 0x8003, [IntPtr]5, [IntPtr]::Zero) | Out-Null     # WM_APP_KEY, KeyCmd::Esc
Start-Sleep -Milliseconds 400

Write-Host "== 7. Esc hides popup =="
$visible2 = [W]::Visible()
if ($visible2) { throw "popup still visible after Esc" }
Write-Host "PASS: popup hidden"

Write-Host "== 8. restart persistence =="
$proc.Kill(); $proc.WaitForExit()
$proc = Start-Process -FilePath $exe -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1500
if ($proc.HasExited) { throw "App failed on restart" }
Set-ClipboardRetry "after restart check"
Start-Sleep -Seconds 3
$items2 = Get-Content (Join-Path $data "items.json") -Raw -Encoding UTF8
if (-not $items2.Contains("hello world from test") -or -not $items2.Contains("after restart check")) {
  throw "history did not survive restart"
}
Write-Host "PASS: persistence across restart"
$proc.Refresh()
Write-Host ("  post-restart WS={0:N1} MB" -f ($proc.WorkingSet64/1MB))

Write-Host "== 9. clean exit =="
$proc.Kill(); $proc.WaitForExit()
Write-Host "ALL TESTS PASSED"
