Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$ok = $false
for ($i = 0; $i -lt 8; $i++) {
  try {
    $b = New-Object System.Drawing.Bitmap(320, 200)
    $g = [System.Drawing.Graphics]::FromImage($b)
    $g.Clear([System.Drawing.Color]::SteelBlue)
    $g.FillEllipse([System.Drawing.Brushes]::Orange, 60, 40, 200, 120)
    $do = New-Object System.Windows.Forms.DataObject
    $do.SetImage($b)
    [System.Windows.Forms.Clipboard]::SetDataObject($do, $true)   # flush: survives owner exit
    $ok = $true
    break
  } catch { Start-Sleep -Milliseconds 500 }
}
if (-not $ok) { Write-Host "image SetDataObject FAILED"; exit 1 }
