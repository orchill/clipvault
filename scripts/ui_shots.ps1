$ErrorActionPreference = "Continue"
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections.Generic;
public class UI {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindowExW(IntPtr p, IntPtr a, string c, string t); // noqa
  public static IntPtr Popup, Settings;
  public static bool Cb(IntPtr h, IntPtr l) {
    var sb = new StringBuilder(64);
    GetClassName(h, sb, 64);
    if (sb.ToString() == "ClipVaultPopup") Popup = h;
    if (sb.ToString() == "ClipVaultSettings") Settings = h;
    return true;
  }
  public static void FindAll() { Popup = IntPtr.Zero; Settings = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); }
  public static void Shot(string path) {
    var b = System.Windows.Forms.Screen.PrimaryScreen.Bounds;
    var bmp = new System.Drawing.Bitmap(b.Width, b.Height);
    var g = System.Drawing.Graphics.FromImage(bmp);
    g.CopyFromScreen(0, 0, 0, 0, bmp.Size);
    bmp.Save(path, System.Drawing.Imaging.ImageFormat.Png);
    g.Dispose(); bmp.Dispose();
  }
}
"@ -ReferencedAssemblies System.Windows.Forms,System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$exe = '$PSScriptRoot\..\ClipVault.exe'
$out = '$PSScriptRoot\..\build'
$p = Start-Process $exe -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1800
[UI]::FindAll()
$hwnd = [UI]::Popup
Write-Host "popup hwnd: $hwnd"

# 1. open popup via hotkey message
[UI]::PostMessageW($hwnd, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 900
[UI]::Shot("$out\shot_main.png"); Write-Host 'shot: main'; $p.Refresh(); Write-Host ('  alive: ' + (-not $p.HasExited))

# 2. pin first two rows (Ctrl+P command, Down, Ctrl+P)
[UI]::PostMessageW($hwnd, 0x8003, [IntPtr]7, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 350
[UI]::PostMessageW($hwnd, 0x8003, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 200
[UI]::PostMessageW($hwnd, 0x8003, [IntPtr]7, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 450
[UI]::Shot("$out\shot_pinned.png"); Write-Host 'shot: pinned'; $p.Refresh(); Write-Host ('  alive: ' + (-not $p.HasExited))

# 3. search filter via WM_CHAR to the edit
$edit = [UI]::FindWindowExW($hwnd, [IntPtr]::Zero, "EDIT", $null)
Write-Host "edit hwnd: $edit"
foreach ($ch in "hello".ToCharArray()) {
  [UI]::PostMessageW($edit, 0x0102, [IntPtr][char]$ch, [IntPtr]::Zero) | Out-Null
  Start-Sleep -Milliseconds 70
}
Start-Sleep -Milliseconds 700
[UI]::Shot("$out\shot_search.png"); Write-Host 'shot: search'; $p.Refresh(); Write-Host ('  alive: ' + (-not $p.HasExited))
# clear search + hide popup (Esc)
foreach ($i in 1..5) { [UI]::PostMessageW($edit, 0x0102, [IntPtr]0x08, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 50 }
[UI]::PostMessageW($hwnd, 0x8003, [IntPtr]5, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 400

# 4. settings window
[UI]::PostMessageW($hwnd, 0x8006, [IntPtr]::Zero, [IntPtr]0x434C5631) | Out-Null
Start-Sleep -Milliseconds 1000
[UI]::FindAll()
Write-Host "settings hwnd: $($([UI]::Settings))"
[UI]::Shot("$out\shot_settings.png"); Write-Host 'shot: settings'; $p.Refresh(); Write-Host ('  alive: ' + (-not $p.HasExited))

# 5. close settings, hide popup, exit app
if ([UI]::Settings -ne [IntPtr]::Zero) { [UI]::PostMessageW([UI]::Settings, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }  # WM_CLOSE
Start-Sleep -Milliseconds 400
[UI]::PostMessageW($hwnd, 0x8003, [IntPtr]5, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 300
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Write-Host 'ui shots complete'
