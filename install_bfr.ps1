# BFR mod pack: installs the pack's data folder and the desktop shortcut "Phantom Dust BFR". Close the game first.
$ErrorActionPreference = 'Stop'
if (Get-Process PDUWP -ErrorAction SilentlyContinue) { throw 'Close Phantom Dust first, then run INSTALL BFR.bat again.' }
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MSEsper_8wekyb3d8bbwe'
if (-not (Test-Path -LiteralPath $pkg)) { throw 'Phantom Dust (Microsoft Store) is not installed for this Windows user.' }
# 1.50.43 your own saved game (arsenals, ratings, story progress, wins and losses) is copied to your desktop first (1.50.46: the first install only)
& (Join-Path $here 'backup_save.ps1') -Why 'made by INSTALL BFR before installing' -Auto
$man = Get-Content -LiteralPath (Join-Path $here 'MANIFEST.json') -Raw | ConvertFrom-Json
foreach ($p in $man.files.PSObject.Properties) {
  $f = Join-Path $here $p.Name
  if (-not (Test-Path -LiteralPath $f)) { throw "missing pack file: $($p.Name)" }
  if ((Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower() -ne $p.Value) { throw "damaged pack file: $($p.Name) - download the pack again" }
}
$data = Join-Path $pkg 'AC\Temp\CwoLoaderBFR'
New-Item -ItemType Directory -Force -Path $data | Out-Null
# 1.49: every file of yours that the pack replaces is kept first (AC\Temp\CwoLoaderBFR_backup_<date>), never deleted
$src = Join-Path $here 'data'
$bk = $data + '_backup_' + (Get-Date -Format 'yyyy-MM-dd_HHmmss')
$kept = 0
Get-ChildItem -LiteralPath $src -Recurse -File | ForEach-Object {
  $rel = $_.FullName.Substring($src.Length + 1)
  $t = Join-Path $data $rel
  if ((Test-Path -LiteralPath $t) -and ((Get-FileHash -LiteralPath $t -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash)) {
    $b = Join-Path $bk $rel
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
    Copy-Item -LiteralPath $t -Destination $b -Force
    $kept++
  }
}
# 1.49.1: game files the loader builds from the pack's patches at the first start (stages, outfits, menus) - a different
# copy of yours is kept too (the loader itself keeps your data-folder models as <name>.before_pack)
foreach ($p in $man.patches) {
  if ($p.file.StartsWith('_data/')) { continue }
  $t = Join-Path $data ('game\' + ($p.file -replace '/', '\'))
  if ((Test-Path -LiteralPath $t) -and ((Get-FileHash -LiteralPath $t -Algorithm SHA256).Hash.ToLower() -ne $p.result_sha256)) {
    $b = Join-Path $bk ('game\' + ($p.file -replace '/', '\'))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
    Copy-Item -LiteralPath $t -Destination $b -Force
    $kept++
  }
}
# 1.50.9: a patch file an older pack version had and this one does not (a removed stage) goes to the backup too, so the
# loader does not build that file again
$srcp = Join-Path $src 'patches'; $dstp = Join-Path $data 'patches'
if (Test-Path -LiteralPath $dstp) {
  Get-ChildItem -LiteralPath $dstp -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($dstp.Length + 1)
    if (-not (Test-Path -LiteralPath (Join-Path $srcp $rel))) {
      $b = Join-Path $bk ('patches\' + $rel)
      New-Item -ItemType Directory -Force -Path (Split-Path -Parent $b) | Out-Null
      Move-Item -LiteralPath $_.FullName -Destination $b -Force
      $kept++
    }
  }
}
if ($kept) { Write-Host "$kept of your own files that the pack replaces were kept in $bk" }
Get-ChildItem -LiteralPath $src | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $data -Recurse -Force }
# the CREATE editor's font: made on this PC from Windows' own Segoe UI fonts (never shipped)
& (Join-Path $here 'launcher\CwoLaunch.exe') --bake-font (Join-Path $data 'CwoUiFont.bin') | Out-Null
$names = Join-Path $data 'CwoOutfitNames.txt'   # 1.45: COLORS shows COLOR 1-15 again
if (Test-Path -LiteralPath $names) { Move-Item -LiteralPath $names -Destination ($names + '.old') -Force }
$lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Phantom Dust BFR.lnk'
$s = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
$s.TargetPath = Join-Path $here 'launcher\CwoLaunch.exe'
$s.Arguments = '--edition BFR'
$s.WorkingDirectory = Join-Path $here 'launcher'
$s.Description = 'Phantom Dust - BFR mod pack ' + $man.version
$s.Save()
Write-Host "BFR mod pack $($man.version) installed."
Write-Host "Start the game with 'Phantom Dust BFR' on your desktop. The first start builds the pack's menus from your own game files."
Write-Host "Loader log: $data\cwo_loader.log"
