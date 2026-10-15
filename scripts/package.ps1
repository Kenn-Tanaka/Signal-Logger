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

# Import safe drive, path & staging helpers
. "$PSScriptRoot\common-drive.ps1"

# 1. Version validation (BUILD-004: strictly digits and dots, no path traversal)
Assert-ValidPackageVersion -Version $Version

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Data-Logger Suite Release Packaging Script" -ForegroundColor Cyan
Write-Host " Version:      $Version" -ForegroundColor Cyan
Write-Host " Project root: $ProjectRoot" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

Invoke-WithSafeMappedDrive -TargetDirectory $ProjectRoot -Action {
    param($WorkingRoot)

    $BuildDir = Join-Path $WorkingRoot "build"
    $LoggerExe = Join-Path $BuildDir "DataLogger.exe"
    $ConverterExe = Join-Path $BuildDir "DlogToCsv.exe"
    $ProvenancePath = Join-Path $BuildDir "build_provenance.json"

    # 2. Verify Executables and Provenance
    if (!(Test-Path -LiteralPath $LoggerExe) -or !(Test-Path -LiteralPath $ConverterExe)) {
        throw "Executables not found in '$BuildDir'. Please build them first using '.\build.ps1 -Test'."
    }

    if (!(Test-Path -LiteralPath $ProvenancePath)) {
        throw "Build provenance file 'build_provenance.json' not found in '$BuildDir'. Please rebuild using '.\build.ps1 -Test' to generate provenance metadata."
    }

    $provenance = Get-Content -LiteralPath $ProvenancePath -Raw -Encoding utf8 | ConvertFrom-Json
    $dlogCurrentHash = (Get-FileHash -LiteralPath $LoggerExe -Algorithm SHA256).Hash
    $csvCurrentHash  = (Get-FileHash -LiteralPath $ConverterExe -Algorithm SHA256).Hash

    $dlogProvEntry = $provenance.executables | Where-Object { $_.name -eq "DataLogger.exe" }
    $csvProvEntry  = $provenance.executables | Where-Object { $_.name -eq "DlogToCsv.exe" }

    if (!$dlogProvEntry -or $dlogProvEntry.sha256 -ne $dlogCurrentHash) {
        throw "Provenance check failed for DataLogger.exe. Binary hash does not match build_provenance.json."
    }
    if (!$csvProvEntry -or $csvProvEntry.sha256 -ne $csvCurrentHash) {
        throw "Provenance check failed for DlogToCsv.exe. Binary hash does not match build_provenance.json."
    }
    Write-Host "[OK] Binary provenance verified against build_provenance.json." -ForegroundColor Green

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

    # 5. Staging Release Package in Isolated Directory (BUILD-002, BUILD-004)
    Write-Host "[3/5] Assembling package contents in isolated staging..." -ForegroundColor Yellow

    if ([string]::IsNullOrWhiteSpace($OutputDir)) {
        $OutputDir = Join-Path $WorkingRoot "dist"
    }
    $OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
    if (!(Test-Path -LiteralPath $OutputDir)) {
        New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    }
    Set-Content -LiteralPath $OutputDir -Stream "com.dropbox.ignored" -Value 1 -ErrorAction SilentlyContinue

    $PackageName = "DataLogger-v$Version-win64"
    $FinalPackageDir = Join-Path $OutputDir $PackageName
    $StageGuid = [Guid]::NewGuid().ToString("N")
    $StagingDir = Join-Path $OutputDir ".staging_${PackageName}_${StageGuid}"

    # Create staging directory and place owner marker
    New-Item -ItemType Directory -Path $StagingDir -Force | Out-Null
    $OwnerMarker = Join-Path $StagingDir ".dlog_stage_owner"
    Set-Content -LiteralPath $OwnerMarker -Value "Data-Logger Stage Owner: $StageGuid" -Encoding utf8

    try {
        # Copy binaries
        Copy-Item -LiteralPath $LoggerExe -Destination (Join-Path $StagingDir "DataLogger.exe")
        Copy-Item -LiteralPath $ConverterExe -Destination (Join-Path $StagingDir "DlogToCsv.exe")

        # Copy primary licenses
        $LicenseFile = Join-Path $WorkingRoot "LICENSE"
        if (Test-Path -LiteralPath $LicenseFile) {
            Copy-Item -LiteralPath $LicenseFile -Destination (Join-Path $StagingDir "LICENSE")
        }

        $LicensesMd = Join-Path $WorkingRoot "LICENSES.md"
        if (Test-Path -LiteralPath $LicensesMd) {
            Copy-Item -LiteralPath $LicensesMd -Destination (Join-Path $StagingDir "LICENSES.md")
        }

        # Copy 3rd-party license texts
        $InstallLicenses = Join-Path $WorkingRoot ".deps\install\licenses"
        if (Test-Path -LiteralPath $InstallLicenses) {
            $DestLicenses = Join-Path $StagingDir "licenses"
            Copy-Item -Recurse -LiteralPath $InstallLicenses -Destination $DestLicenses
        }

        # Copy README and documentation preserving docs/ structure (DOC-002)
        $ReadmeFile = Join-Path $WorkingRoot "README.md"
        if (Test-Path -LiteralPath $ReadmeFile) {
            Copy-Item -LiteralPath $ReadmeFile -Destination (Join-Path $StagingDir "README.md")
        }

        $DocsSourceDir = Join-Path $WorkingRoot "docs"
        if (Test-Path -LiteralPath $DocsSourceDir) {
            $DocsDestDir = Join-Path $StagingDir "docs"
            New-Item -ItemType Directory -Path $DocsDestDir -Force | Out-Null
            if (Test-Path -LiteralPath (Join-Path $DocsSourceDir "QUICKSTART.md")) {
                Copy-Item -LiteralPath (Join-Path $DocsSourceDir "QUICKSTART.md") -Destination (Join-Path $DocsDestDir "QUICKSTART.md")
            }
            if (Test-Path -LiteralPath (Join-Path $DocsSourceDir "REFERENCE_MANUAL.md")) {
                Copy-Item -LiteralPath (Join-Path $DocsSourceDir "REFERENCE_MANUAL.md") -Destination (Join-Path $DocsDestDir "REFERENCE_MANUAL.md")
            }
        }

        # Strict check: verify no measurement, test binary, or temp files leaked into staging
        $ForbiddenPatterns = @("*.bin", "*.dat", "*.meta", "*.partial", "*.log", "*.tmp")
        foreach ($pat in $ForbiddenPatterns) {
            $leaked = @(Get-ChildItem -Path $StagingDir -Recurse -Filter $pat)
            if ($leaked.Count -gt 0) {
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
Target OS:       Windows 10 / 11 (64-bit)
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
        if (Test-Path -LiteralPath $LockFilePath) {
            $lockData = Get-Content -Raw -Encoding utf8 -LiteralPath $LockFilePath | ConvertFrom-Json
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

        # Remove owner marker before hashing
        Remove-Item -LiteralPath $OwnerMarker -Force -ErrorAction SilentlyContinue

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
        $TempZipPath = Join-Path $OutputDir ".temp_${PackageName}_${StageGuid}.zip"

        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zipSuccess = $false
        for ($retry = 0; $retry -lt 5; $retry++) {
            try {
                if (Test-Path -LiteralPath $TempZipPath) {
                    Remove-Item -LiteralPath $TempZipPath -Force
                }
                [System.IO.Compression.ZipFile]::CreateFromDirectory($StagingDir, $TempZipPath, [System.IO.Compression.CompressionLevel]::Optimal, $false)
                $zipSuccess = $true
                break
            } catch {
                Write-Host "  [WARN] Zip archive retry $($retry + 1)/5 (transient file lock): $($_.Exception.Message)" -ForegroundColor Yellow
                Start-Sleep -Seconds 1
            }
        }
        if (!$zipSuccess) {
            throw "Failed to create release zip archive at '$TempZipPath' due to transient file locks."
        }

        # 8. Atomic Promotion to Final Package Directory
        if (Test-Path -LiteralPath $FinalPackageDir) {
            Remove-SafeDirectory -Path $FinalPackageDir -AllowedParent $OutputDir
        }
        Move-Item -LiteralPath $StagingDir -Destination $FinalPackageDir -Force

        if (Test-Path -LiteralPath $ZipPath) {
            Remove-Item -LiteralPath $ZipPath -Force
        }
        Move-Item -LiteralPath $TempZipPath -Destination $ZipPath -Force

        $ZipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $ZipPath).Hash
        $ZipSize = (Get-Item -LiteralPath $ZipPath).Length

        Write-Host "======================================================================" -ForegroundColor Green
        Write-Host " RELEASE PACKAGE CREATED SUCCESSFULLY!" -ForegroundColor Green
        Write-Host " Output Directory: $FinalPackageDir" -ForegroundColor Green
        Write-Host " Zip Archive:      $ZipPath" -ForegroundColor Green
        Write-Host " Zip Size:         $([Math]::Round($ZipSize / 1MB, 2)) MB ($ZipSize bytes)" -ForegroundColor Green
        Write-Host " Zip SHA-256:      $ZipHash" -ForegroundColor Green
        Write-Host "======================================================================" -ForegroundColor Green
    }
    finally {
        # Clean up any remaining temporary files if promotion was interrupted
        if (Test-Path -LiteralPath $StagingDir) {
            Remove-Item -LiteralPath $StagingDir -Recurse -Force -ErrorAction SilentlyContinue
        }
        $residualTempZip = Join-Path $OutputDir ".temp_${PackageName}_${StageGuid}.zip"
        if (Test-Path -LiteralPath $residualTempZip) {
            Remove-Item -LiteralPath $residualTempZip -Force -ErrorAction SilentlyContinue
        }
    }
}
