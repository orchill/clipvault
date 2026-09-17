Add-Type -AssemblyName System.Windows.Forms
$rtf = '{\rtf1\ansi{\b bold} and {\i italic} and {\cf2 colored} text}'
$plain = 'bold and italic and colored text'
$ok = $false
for ($i = 0; $i -lt 8; $i++) {
  try {
    $do = New-Object System.Windows.Forms.DataObject
    $do.SetData([System.Windows.Forms.DataFormats]::Rtf, $rtf)
    $do.SetData([System.Windows.Forms.DataFormats]::UnicodeText, $plain)
    [System.Windows.Forms.Clipboard]::SetDataObject($do, $true)   # flush: survives owner exit
    $ok = $true
    break
  } catch { Start-Sleep -Milliseconds 500 }
}
if (-not $ok) { Write-Host "RTF SetDataObject FAILED"; exit 1 }
