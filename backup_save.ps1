# Phantom Dust: copies your own saved game (arsenals, ratings, story progress, wins and losses) to your desktop.
# It only reads your save; nothing in the game is changed. Close Phantom Dust first.
param([string]$Why = '')
$ErrorActionPreference = 'Stop'
if (Get-Process PDUWP -ErrorAction SilentlyContinue) { throw 'Close Phantom Dust first, then run BACKUP MY SAVE.bat again.' }
$pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MSEsper_8wekyb3d8bbwe'
$wgs = Join-Path $pkg 'SystemAppData\wgs'
if (-not (Test-Path -LiteralPath $wgs)) { Write-Host 'No Phantom Dust save was found for this Windows user (start the game once and sign in).'; return }
$desk = [Environment]::GetFolderPath('Desktop')
$dest = Join-Path $desk ('Phantom Dust save backup ' + (Get-Date -Format 'yyyy-MM-dd HHmm'))
$n = 1; $base = $dest
while (Test-Path -LiteralPath $dest) { $n++; $dest = "$base ($n)" }
New-Item -ItemType Directory -Force -Path $dest | Out-Null
& robocopy $wgs (Join-Path $dest 'wgs') /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null   # robocopy: long paths too
$bad = 0; $count = 0
foreach ($f in Get-ChildItem -LiteralPath $wgs -Recurse -File -Force) {
  $rel = $f.FullName.Substring($wgs.Length).TrimStart('\')
  $c = Join-Path (Join-Path $dest 'wgs') $rel
  $count++
  if (-not (Test-Path -LiteralPath $c) -or (Get-FileHash -LiteralPath $c -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA256).Hash) { $bad++ }
}
if ($bad) { throw "The backup is not complete ($bad of $count files differ). Nothing was changed in your game; try again." }
$restore = @'
# Puts this backup of your Phantom Dust save back. Your save as it is now is kept first (folder "save before restore ...").
$ErrorActionPreference = 'Stop'
if (Get-Process PDUWP -ErrorAction SilentlyContinue) { throw 'Close Phantom Dust first, then run RESTORE THIS SAVE.bat again.' }
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$wgs = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MSEsper_8wekyb3d8bbwe\SystemAppData\wgs'
$from = Join-Path $here 'wgs'
if (-not (Test-Path -LiteralPath $from)) { throw 'This folder has no wgs backup in it.' }
$ans = Read-Host 'Replace your current Phantom Dust save with this backup? Type YES to go on'
if ($ans -ne 'YES') { Write-Host 'Nothing was changed.'; return }
$sad = Split-Path -Parent $wgs
New-Item -ItemType Directory -Force -Path $sad | Out-Null
$new = Join-Path $sad 'wgs_restoring'
if (Test-Path -LiteralPath $new) { Remove-Item -LiteralPath $new -Recurse -Force }
& robocopy $from $new /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null   # robocopy: long paths too
$want = @(Get-ChildItem -LiteralPath $from -Recurse -File -Force | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash } | Sort-Object)
$got = @(if (Test-Path -LiteralPath $new) { Get-ChildItem -LiteralPath $new -Recurse -File -Force | ForEach-Object { (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash } | Sort-Object })
if ($want.Count -eq 0 -or ($want -join ',') -ne ($got -join ',')) {
  if (Test-Path -LiteralPath $new) { Remove-Item -LiteralPath $new -Recurse -Force }
  throw 'The backup could not be copied back completely. Your current save was NOT changed.'
}
if (Test-Path -LiteralPath $wgs) {
  $keep = Join-Path $here ('save before restore ' + (Get-Date -Format 'yyyy-MM-dd HHmm'))
  & robocopy $wgs $keep /E /R:1 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
  if ($LASTEXITCODE -ge 8) { Remove-Item -LiteralPath $new -Recurse -Force; throw 'Your current save could not be kept first. Nothing was changed.' }
  Rename-Item -LiteralPath $wgs -NewName ('wgs_old_' + (Get-Date -Format 'yyyyMMddHHmmss'))
}
Rename-Item -LiteralPath $new -NewName 'wgs'
Get-ChildItem -LiteralPath $sad -Directory -Filter 'wgs_old_*' | Remove-Item -Recurse -Force
Write-Host 'Your save is back. If the Xbox app asks which save to keep when the game starts, choose the one on this PC.'
'@
Set-Content -LiteralPath (Join-Path $dest 'restore_save.ps1') -Value $restore -Encoding UTF8
Set-Content -LiteralPath (Join-Path $dest 'RESTORE THIS SAVE.bat') -Value "@echo off`r`npowershell -NoProfile -ExecutionPolicy Bypass -File `"%~dp0restore_save.ps1`"`r`npause" -Encoding ASCII
$note = "Phantom Dust save backup`r`nMade: $(Get-Date -Format 'yyyy-MM-dd HH:mm')$(if ($Why) { " ($Why)" })`r`n`r`n" +
        "The wgs folder is a copy of your own saved game (arsenals, ratings, story progress, wins and losses), $count files, each checked.`r`n" +
        "To put it back: close Phantom Dust and run RESTORE THIS SAVE.bat. Your save as it is then is kept first.`r`n" +
        "Keep this folder somewhere safe (a USB stick or cloud drive too, if you like). It holds only your save, nothing of the mods.`r`n"
Set-Content -LiteralPath (Join-Path $dest 'README.txt') -Value $note -Encoding ASCII
Write-Host "Your Phantom Dust save is backed up on your desktop: $(Split-Path -Leaf $dest) ($count files, all checked)."
