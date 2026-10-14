# Data-Logger Suite Release Packaging Script
[CmdletBinding()]
param(
    [string]$Version = "2.0.0",
    [switch]$SkipTest,
    [string]$OutputDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..").Path

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Data-Logger Suite Release Packaging Script" -ForegroundColor Cyan
Write-Host " Version:      $Version" -ForegroundColor Cyan
Write-Host " Project root: $ProjectRoot" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# 1. Virtual drive mapping for path compatibility if needed
$WorkingRoot = $ProjectRoot
$VirtDrive = "X:"
if ($ProjectRoot -match '[^\x00-\x7F]| ') {
    $usedDrives = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Name
    $substList = cmd.exe /c "subst"
    if ($usedDrives -contains "X") {
        if ($substList -match "X:\\: => (.*)") {
            $target = $Matches[1].Trim()
            if ($target.TrimEnd('\') -eq $ProjectRoot.TrimEnd('\')) {
                $WorkingRoot = "X:\"
            }
        }
    }
}

$BuildDir = Join-Path $WorkingRoot "build"
$LoggerExe = Join-Path $BuildDir "DataLogger.exe"
$ConverterExe = Join-Path $BuildDir "DlogToCsv.exe"

# 2. Verify Executables
if (!(Test-Path $LoggerExe) -or !(Test-Path $ConverterExe)) {
    throw "Executables not found in '$BuildDir'. Please build them first using '.\build.ps1 -Test'."
}

# 3. Automated Test Verification
if (!$SkipTest) {
    Write-Host "[1/5] Running test suites before packaging..." -ForegroundColor Yellow
    & ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "CTest test suites failed! Aborting packaging to prevent releasing broken binaries."
    }
} else {
    Write-Host "[1/5] Skipping test suites as requested (-SkipTest)." -ForegroundColor Gray
}

# 4. PE DLL Dependency Inspection (Validate strict static linking)
Write-Host "[2/5] Inspecting executable DLL dependencies..." -ForegroundColor Yellow

$AllowedDllPatterns = @(
    '^KERNEL32\.dll$',
    '^USER32\.dll$',
    '^GDI32\.dll$',
    '^ADVAPI32\.dll$',
    '^SHELL32\.dll$',
    '^SHLWAPI\.dll$',
    '^ole32\.dll$',
    '^OLEAUT32\.dll$',
    '^OLEACC\.dll$',
    '^COMCTL32\.dll$',
    '^comdlg32\.dll$',
    '^UxTheme\.dll$',
    '^VERSION\.dll$',
    '^WINMM\.dll$',
    '^WINSPOOL\.DRV$',
    '^ws2_32\.dll$',
    '^setupapi\.dll$',
    '^dsound\.dll$',
    '^api-ms-win-crt-.*\.dll$',
    '^ucrtbase\.dll$'
)

function Test-ExecutableDlls {
    param([string]$ExePath)
    $objdumpOutput = & objdump -p $ExePath 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to run objdump on '$ExePath'."
    }

    $dllNames = @()
    foreach ($line in $objdumpOutput) {
        if ($line -match 'DLL Name:\s*(.+)$') {
            $dllNames += $Matches[1].Trim()
        }
    }

    $unexpectedDlls = @()
    foreach ($dll in $dllNames) {
        $matched = $false
        foreach ($pattern in $AllowedDllPatterns) {
            if ($dll -match "(?i)$pattern") {
                $matched = $true
                break
            }
        }
        if (!$matched) {
            $unexpectedDlls += $dll
        }
    }

    if ($unexpectedDlls.Count -gt 0) {
        throw "Executable '$ExePath' has unexpected DLL dependencies: $($unexpectedDlls -join ', '). Static linking check FAILED."
    }
    Write-Host "  [OK] $(Split-Path $ExePath -Leaf): $(($dllNames | Sort-Object -Unique).Count) allowable OS/UCRT DLL dependencies." -ForegroundColor Green
}

Test-ExecutableDlls -ExePath $LoggerExe
Test-ExecutableDlls -ExePath $ConverterExe

# 5. Staging Release Package
Write-Host "[3/5] Assembling package contents..." -ForegroundColor Yellow

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $WorkingRoot "dist"
}
if (!(Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

$PackageName = "DataLogger-v$Version-win64"
$StagingDir = Join-Path $OutputDir $PackageName

if (Test-Path $StagingDir) {
    cmd.exe /c "rmdir /s /q `"$StagingDir`" >nul 2>&1"
    if (Test-Path $StagingDir) {
        Remove-Item -Recurse -Force $StagingDir -ErrorAction SilentlyContinue
    }
}
New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null

# Explicit copy of allowable public release files only
Copy-Item -LiteralPath $LoggerExe -Destination (Join-Path $StagingDir "DataLogger.exe")
Copy-Item -LiteralPath $ConverterExe -Destination (Join-Path $StagingDir "DlogToCsv.exe")

$LicenseFile = Join-Path $WorkingRoot "LICENSE"
if (Test-Path $LicenseFile) {
    Copy-Item -LiteralPath $LicenseFile -Destination (Join-Path $StagingDir "LICENSE")
}

$LicensesMd = Join-Path $WorkingRoot "LICENSES.md"
if (Test-Path $LicensesMd) {
    Copy-Item -LiteralPath $LicensesMd -Destination (Join-Path $StagingDir "LICENSES.md")
}

# 3rd-party license texts
$InstallLicenses = Join-Path $WorkingRoot ".deps\install\licenses"
if (Test-Path $InstallLicenses) {
    $DestLicenses = Join-Path $StagingDir "licenses"
    Copy-Item -Recurse -LiteralPath $InstallLicenses -Destination $DestLicenses
}

# Documentation
$DocsDir = Join-Path $WorkingRoot "docs"
if (Test-Path (Join-Path $DocsDir "QUICKSTART.md")) {
    Copy-Item -LiteralPath (Join-Path $DocsDir "QUICKSTART.md") -Destination (Join-Path $StagingDir "QUICKSTART.md")
}
if (Test-Path (Join-Path $DocsDir "REFERENCE_MANUAL.md")) {
    Copy-Item -LiteralPath (Join-Path $DocsDir "REFERENCE_MANUAL.md") -Destination (Join-Path $StagingDir "REFERENCE_MANUAL.md")
}

# Strict check: verify no measurement, test binary, or temp files leaked into staging
$ForbiddenPatterns = @("*.bin", "*.dat", "*.meta", "*.partial", "*.log", "*.tmp")
foreach ($pat in $ForbiddenPatterns) {
    $leaked = @(Get-ChildItem -Path $StagingDir -Recurse -Filter $pat)
    if ($leaked.Count -gt 0) {
        Remove-Item -Recurse -Force $StagingDir
        throw "Security failure: measurement or temporary file ($($leaked[0].FullName)) leaked into release staging!"
    }
}

# 6. Generate MANIFEST.txt
Write-Host "[4/5] Generating release manifest (MANIFEST.txt)..." -ForegroundColor Yellow

$ManifestPath = Join-Path $StagingDir "MANIFEST.txt"
$GccVersion = (& gcc --version | Select-Object -First 1).Trim()
$CmakeVersion = (& cmake --version | Select-Object -First 1).Trim()
$NinjaVersion = "Ninja $(& ninja --version)".Trim()
$TimestampUtc = [DateTime]::UtcNow.ToString("yyyy-MM-dd HH:mm:ss 'UTC'")

$ManifestContent = @"
======================================================================
 Data-Logger Suite Release Manifest
======================================================================
Release Package: $PackageName
Version:         $Version
Target OS:       Windows x64 (Windows 7 SP1 or later, Windows 10 / 11)
Build Date:      $TimestampUtc

Toolchain:
  Compiler:      $GccVersion
  CMake:         $CmakeVersion
  Ninja:         $NinjaVersion
  Linking:       Fully Static (-static, -static-libgcc, -static-libstdc++)
                 Zero external runtime DLL dependencies (OS & UCRT only)

Official Dependency Revisions:
"@

$LockFilePath = Join-Path $WorkingRoot "deps\dependencies.lock.json"
if (Test-Path $LockFilePath) {
    $lockData = Get-Content -Raw -Encoding utf8 $LockFilePath | ConvertFrom-Json
    foreach ($dep in $lockData.dependencies.PSObject.Properties) {
        $d = $dep.Value
        $tag = if ($d.PSObject.Properties['tag']) { $d.tag } else { "N/A" }
        $licName = if ($d.PSObject.Properties['license'] -and $d.license.PSObject.Properties['name']) { $d.license.name } else { "See LICENSES.md" }
        $ManifestContent += "`n  - $($dep.Name):"
        $ManifestContent += "`n      Repository: $($d.repository)"
        $ManifestContent += "`n      Tag:        $tag"
        $ManifestContent += "`n      Commit SHA: $($d.commit)"
        $ManifestContent += "`n      License:    $licName"
    }
}

$ManifestContent += @"


Packaged Files and SHA-256 Hashes:
----------------------------------------------------------------------
"@

$stagedFiles = Get-ChildItem -Path $StagingDir -Recurse -File | Sort-Object FullName
foreach ($file in $stagedFiles) {
    if ($file.FullName -eq $ManifestPath) { continue }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
    $rel = $file.FullName.Substring($StagingDir.Length).TrimStart('\', '/')
    $ManifestContent += "`n$hash  $rel"
}

[System.IO.File]::WriteAllText($ManifestPath, $ManifestContent, [System.Text.Encoding]::UTF8)

# 7. Compress into ZIP Archive
Write-Host "[5/5] Creating release zip archive..." -ForegroundColor Yellow

$ZipPath = Join-Path $OutputDir "$PackageName.zip"
if (Test-Path $ZipPath) {
    Remove-Item -Force $ZipPath
}

Compress-Archive -Path "$StagingDir\*" -DestinationPath $ZipPath -Force

$ZipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ZipPath).Hash
$ZipSize = (Get-Item -LiteralPath $ZipPath).Length

Write-Host "======================================================================" -ForegroundColor Green
Write-Host " RELEASE PACKAGE CREATED SUCCESSFULLY!" -ForegroundColor Green
Write-Host " Output Directory: $StagingDir" -ForegroundColor Green
Write-Host " Zip Archive:      $ZipPath" -ForegroundColor Green
Write-Host " Zip Size:         $([Math]::Round($ZipSize / 1MB, 2)) MB ($ZipSize bytes)" -ForegroundColor Green
Write-Host " Zip SHA-256:      $ZipHash" -ForegroundColor Green
Write-Host "======================================================================" -ForegroundColor Green
