$ErrorActionPreference = "Stop"
# deterministic config: defaults (persist on, autoPaste off, monitor on)
Remove-Item (Join-Path $env:LOCALAPPDATA 'ClipVault\config.json') -Force -ErrorAction SilentlyContinue
Add-Type @"
using System; using System.Runtime.InteropServices; using System.Text;
public class OW {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder b, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  public struct RECT { public int L, T, R, B; }
  public static IntPtr H;
  public static string Want = "";
  public static bool Cb(IntPtr h, IntPtr l) { var sb = new StringBuilder(64); GetClassName(h, sb, 64); if (sb.ToString() == Want && (!RequireVisible || IsWindowVisible(h))) { H = h; return false; } return true; }
  public static bool RequireVisible = false;
  public static IntPtr Find(string cls) { Want = cls; RequireVisible = false; H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
  public static IntPtr FindVisible(string cls) { Want = cls; RequireVisible = true; H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
  public static IntPtr FindAnyDialog() { Want = "#32770"; RequireVisible = true; H = IntPtr.Zero; EnumWindows(Cb, IntPtr.Zero); return H; }
}
"@
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

function Shot([string]$out) {
  $b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen(0, 0, 0, 0, $bmp.Size)
  $g.Dispose(); $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
}
function ClickAt([int]$x, [int]$y) {
  [OW]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 200
  [OW]::mouse_event(2,0,0,0,[UIntPtr]::Zero); [OW]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
}

$p = Start-Process 'C:\Users\caner\.zcode\workspace\default\ClipVault.exe' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 1600
# seed a fresh image entry (newest -> row 0)
& powershell -NoProfile -STA -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'sta_image.ps1')
Start-Sleep -Milliseconds 2500
$popup = [OW]::Find("ClipVaultPopup")
[OW]::PostMessageW($popup, 0x0312, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 800
$popup = [OW]::FindVisible("ClipVaultPopup")
$r = New-Object OW+RECT
[OW]::GetWindowRect($popup, [ref]$r) | Out-Null
# right-click row 1 (image item: below the pinned github row)
$cx = $r.L + [int](($r.R - $r.L) * 0.45); $cy = $r.T + 130
[OW]::SetCursorPos($cx, $cy) | Out-Null; Start-Sleep -Milliseconds 300
[OW]::mouse_event(8,0,0,0,[UIntPtr]::Zero); [OW]::mouse_event(16,0,0,0,[UIntPtr]::Zero)
Start-Sleep -Milliseconds 700
Shot 'C:\Users\caner\.zcode\workspace\default\build\menu_openwith.png'
Write-Host "menu shot saved"

# the menu appears near the cursor; "Open with..." is the 4th item (~3 items above it)
# menu item height ~22px; Paste(0) Copy again(1) sep(2) Open in Photos(3) Open with...(4)
$my = $cy + 78
ClickAt ($cx + 60) $my
Start-Sleep -Milliseconds 1200
Shot 'C:\Users\caner\.zcode\workspace\default\build\openwith_dialog.png'
Write-Host "dialog shot saved"
$dlg = [OW]::FindAnyDialog()
Write-Host ("dialog found: " + $dlg + " | popup still visible: " + [OW]::IsWindowVisible($popup))
if ($dlg -ne [IntPtr]::Zero) {
  $d = New-Object OW+RECT
  [OW]::GetWindowRect($dlg, [ref]$d) | Out-Null
  ClickAt ($d.R - 18) ($d.T + 15)   # close the picker
  Start-Sleep -Milliseconds 500
}
Write-Host ("popup visible after picker: " + [OW]::IsWindowVisible($popup))
Stop-Process -Id $p.Id -Force
