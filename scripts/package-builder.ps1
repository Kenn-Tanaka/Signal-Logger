[CmdletBinding()]
param([string]$Version='2.0.0', [string]$OutputDir='')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common-drive.ps1"
Assert-ValidPackageVersion $Version
$root = (Resolve-Path "$PSScriptRoot/..").Path
if (!$OutputDir) { $OutputDir = Join-Path $root 'dist' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$stage = Join-Path $OutputDir ('.builder-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
foreach ($rel in Get-BuilderFiles $root) {
    $dest = Join-Path $stage $rel
    New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root $rel) -Destination $dest
}
& "$PSScriptRoot/test-packaging.ps1" -Directory $stage -Kind Builder
Complete-Package $stage (Join-Path $OutputDir "DataLogger-v$Version-builder.zip")
