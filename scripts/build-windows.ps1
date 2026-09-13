#Requires -Version 5.1
<#
  Configures, builds and tests Metin2 Rigging Studio (native C++).
  Usage:
    powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1 [-Preset windows-debug] [-NoTests]
  Works from any shell: CMake auto-detects Visual Studio 2022 (no PATH setup needed).
#>
param(
  [string]$Preset = "windows-debug",
  [switch]$NoTests,
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $PSCommandPath)

if ($Clean) {
  $dir = Join-Path $root "build"
  if (Test-Path -LiteralPath $dir) {
    Write-Host "Removing $dir"
    Remove-Item -LiteralPath $dir -Recurse -Force
  }
}

Write-Host "==> cmake configure ($Preset)"
& cmake --preset $Preset 2>&1 | ForEach-Object { $_ }
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }

Write-Host "==> cmake build ($Preset)"
& cmake --build --preset $Preset 2>&1 | ForEach-Object { $_ }
if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }

if (-not $NoTests) {
  Write-Host "==> ctest ($Preset)"
  & ctest --preset $Preset 2>&1 | ForEach-Object { $_ }
  if ($LASTEXITCODE -ne 0) { throw "ctest failed" }
}

Write-Host "OK: $Preset built successfully."
