# BFR mod pack: removes the desktop shortcut and moves the pack's data folder aside (kept, renamed, never deleted).
$ErrorActionPreference = 'Stop'
if (Get-Process PDUWP -ErrorAction SilentlyContinue) { throw 'Close Phantom Dust first, then run UNINSTALL BFR.bat again.' }
$pkg = Join-Path $env:LOCALAPPDATA 'Packages\Microsoft.MSEsper_8wekyb3d8bbwe'
$data = Join-Path $pkg 'AC\Temp\CwoLoaderBFR'
if (Test-Path -LiteralPath $data) {
  $aside = $data + '_removed_' + (Get-Date -Format 'yyyy-MM-dd_HHmmss')
  Move-Item -LiteralPath $data -Destination $aside
  Write-Host "Pack data moved to $aside (your BFR arsenals and stages are in there; delete it yourself if you do not need it)."
}
$lnk = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Phantom Dust BFR.lnk'
if (Test-Path -LiteralPath $lnk) { Remove-Item -LiteralPath $lnk -Force }
Write-Host 'BFR mod pack removed. The game itself and your saves were never changed.'
