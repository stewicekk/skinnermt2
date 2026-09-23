# Portable package for Metin2 Rigging Studio (Windows x64).
# Stages the built exe + CLI + docs into dist/ and zips it.
# Usage: powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1 [-Preset windows-release]
param([string]$Preset = "windows-release")
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
if ($Preset -notin @("windows-debug", "windows-release")) {
	throw "Unsupported preset '$Preset'. Use windows-debug or windows-release."
}
$configuration = if ($Preset -eq "windows-debug") { "Debug" } else { "Release" }
$buildDir = Join-Path $root ("build/{0}" -f ($Preset -replace "windows-", ""))
$binDir = Join-Path $buildDir $configuration
$dist = Join-Path $root "dist/Metin2RiggingStudio-win64"
New-Item -ItemType Directory -Path $dist -Force | Out-Null
Copy-Item (Join-Path $binDir "Metin2RiggingStudio.exe") $dist
Copy-Item (Join-Path $binDir "m2rig_cli.exe") $dist
Copy-Item (Join-Path $root "docs/USER_GUIDE.md") (Join-Path $dist "README.txt")
Copy-Item (Join-Path $root "docs/USER_GUIDE.md") (Join-Path $dist "USER_GUIDE.txt")
New-Item -ItemType Directory -Path (Join-Path $dist "config") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $dist "logs") -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $dist "projects") -Force | Out-Null
$zip = Join-Path $root "dist/Metin2RiggingStudio-win64.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $zip
Get-FileHash $zip -Algorithm SHA256 | Format-List
Write-Output "Package: $zip"
Write-Output "Note: Data/Models + noesis/ bridges are NOT bundled (see docs/DEPENDENCIES.md)."
