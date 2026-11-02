# BFR mod pack: installs the pack's data folder and the desktop shortcut "Phantom Dust BFR". Close the game first.
$ErrorActionPreference = 'Stop'
if (Get-Process PDUWP -ErrorAction SilentlyContinue) { throw 'Close Phantom Dust first, then run INSTALL BFR.bat again.' }
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MSEsper_8wekyb3d8bbwe'
if (-not (Test-Path -LiteralPath $pkg)) { throw 'Phantom Dust (Microsoft Store) is not installed for this Windows user.' }
$man = Get-Content -LiteralPath (Join-Path $here 'MANIFEST.json') -Raw | ConvertFrom-Json
foreach ($p in $man.files.PSObject.Properties) {
  $f = Join-Path $here $p.Name
  if (-not (Test-Path -LiteralPath $f)) { throw "missing pack file: $($p.Name)" }
  if ((Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash.ToLower() -ne $p.Value) { throw "damaged pack file: $($p.Name) - download the pack again" }
}
$data = Join-Path $pkg 'AC\Temp\CwoLoaderBFR'
New-Item -ItemType Directory -Force -Path $data | Out-Null
Get-ChildItem -LiteralPath (Join-Path $here 'data') | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $data -Recurse -Force }
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
