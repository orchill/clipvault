$ErrorActionPreference = "Stop"
for ($i = 0; $i -lt 6; $i++) { try { Set-Clipboard -Value 'Fullwidth test: Ｆｕｌｌｗｉｄｔｈ | Script: 𝓗𝓮𝓵𝓵𝓸 | Emoji: 😀🚀' -ErrorAction Stop; break } catch { Start-Sleep -Milliseconds 400 } }
Write-Host "seeded"
