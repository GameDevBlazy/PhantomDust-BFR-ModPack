r"""The player's own Phantom Dust save (owner 2026-11-16: "back up the user's profile, their non-modded profile ... arsenals,
rating, story progress, wins, loss ratio ... I think it's called the WSG folder ... put it on their desktop").
The Store game keeps its save in the package's SystemAppData\wgs folder (Xbox saved-game storage; 14 files, ~0.5 MB on the
owner's PC). BACKUP_PS1 copies it to the desktop, checks every file's SHA-256 against the original, and writes a restore
button into the backup (RESTORE_PS1: keeps the current save first, then puts the backup back). Used by build_bfr_pack.py
(BACKUP MY SAVE.bat + inside INSTALL BFR.bat) and the MODS repo installer. Independently authored; Windows PowerShell 5.1."""

BACKUP_PS1 = r'''# Phantom Dust: copies your own saved game (arsenals, ratings, story progress, wins and losses) to your desktop.
# It only reads your save; nothing in the game is changed. Close Phantom Dust first.
# v2 (owner 2026-11-16 "go"): one desktop folder "Phantom Dust save backups" keeps the newest 3 backups; the installers make
# one automatically only the first time (-Auto); after that BACKUP MY SAVE.bat or MOD LIST > X (the launcher runs this
# before the game starts) make another. -ResultFile: one line for the game's MOD LIST.
param([string]$Why = '', [switch]$Auto, [string]$ResultFile = '')
$ErrorActionPreference = 'Stop'
function Say-Result([string]$t) { if ($ResultFile) { try { Set-Content -LiteralPath $ResultFile -Value $t -Encoding ASCII } catch {} } }
if (Get-Process PDUWP -ErrorAction SilentlyContinue) { Say-Result 'not made: the game was running'; throw 'Close Phantom Dust first, then run BACKUP MY SAVE.bat again.' }
$pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MSEsper_8wekyb3d8bbwe'
$wgs = Join-Path $pkg 'SystemAppData\wgs'
$mark = Join-Path $pkg 'AC\Temp\CwoSaveBackupAuto.txt'
if ($Auto -and (Test-Path -LiteralPath $mark)) { Write-Host 'Your save was backed up once already. For another: BACKUP MY SAVE.bat, or MOD LIST > X in the game.'; return }
if (-not (Test-Path -LiteralPath $wgs)) { Say-Result 'not made: no save found'; Write-Host 'No Phantom Dust save was found for this Windows user (start the game once and sign in).'; return }
$desk = [Environment]::GetFolderPath('Desktop')
$root = Join-Path $desk 'Phantom Dust save backups'
New-Item -ItemType Directory -Force -Path $root | Out-Null
foreach ($old in Get-ChildItem -LiteralPath $desk -Directory -Filter 'Phantom Dust save backup *') {   # v1 backups move in
  $to = Join-Path $root ($old.Name.Substring('Phantom Dust save backup '.Length))
  if (-not (Test-Path -LiteralPath $to)) { Move-Item -LiteralPath $old.FullName -Destination $to }
}
$dest = Join-Path $root (Get-Date -Format 'yyyy-MM-dd HHmm')
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
if ($bad) { Say-Result 'not made: the copy did not check out'; throw "The backup is not complete ($bad of $count files differ). Nothing was changed in your game; try again." }
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
$all = @(Get-ChildItem -LiteralPath $root -Directory | Where-Object { $_.Name -match '^\d{4}-\d\d-\d\d \d{4}( \(\d+\))?$' } | Sort-Object Name -Descending)
$gone = 0
if ($all.Count -gt 3) { foreach ($d in $all[3..($all.Count - 1)]) { Remove-Item -LiteralPath $d.FullName -Recurse -Force; $gone++ } }
if ($Auto) { Set-Content -LiteralPath $mark -Value (Get-Date -Format 'yyyy-MM-dd HH:mm') -Encoding ASCII }
Say-Result "$(Split-Path -Leaf $dest), $count files checked"
Write-Host "Your Phantom Dust save is backed up: desktop > Phantom Dust save backups > $(Split-Path -Leaf $dest) ($count files, all checked).$(if ($gone) { " The oldest backup was removed (the newest 3 are kept)." })"
'''
