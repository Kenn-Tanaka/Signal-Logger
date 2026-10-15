# Data-Logger and DlogToCsv PowerShell Build Script
[CmdletBinding()]
param(
    [switch]$Test,
    [switch]$Clean,
    [string]$BuildDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path "$PSScriptRoot").Path

# Import safe drive & staging utilities
. "$PSScriptRoot\scripts\common-drive.ps1"

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Data-Logger Suite Build Script" -ForegroundColor Cyan
Write-Host " Project root: $ProjectRoot" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

Invoke-WithSafeMappedDrive -TargetDirectory $ProjectRoot -Action {
    param($WorkingRoot)

    # 1. Check Dependencies
    $DepsMarker = Join-Path $WorkingRoot ".deps\install\build_completed.json"
    $VendorLib  = Join-Path $WorkingRoot "3rdparty\portaudio\lib\libportaudio.a"
    if (!(Test-Path -LiteralPath $DepsMarker) -and !(Test-Path -LiteralPath $VendorLib)) {
        Write-Host "[INFO] Dependencies not yet built. Running scripts/build-deps.ps1..." -ForegroundColor Yellow
        & "$PSScriptRoot\scripts\build-deps.ps1"
        if ($LASTEXITCODE -ne 0) {
            throw "Dependency build failed."
        }
    }

    $depsCacheKey = $null
    if (Test-Path -LiteralPath $DepsMarker) {
        try {
            $depsMeta = Get-Content -LiteralPath $DepsMarker -Raw | ConvertFrom-Json
            $depsCacheKey = $depsMeta.cache_key
            Write-Host "[INFO] Using verified dependency installation (Cache Key: $depsCacheKey)" -ForegroundColor Gray
        } catch {
            Write-Warning "Could not parse dependency build marker '$DepsMarker'."
        }
    }

    # 2. Setup Build Directory
    if ([string]::IsNullOrWhiteSpace($BuildDir)) {
        $BuildDir = Join-Path $WorkingRoot "build"
    }
    if ($Clean -and (Test-Path -LiteralPath $BuildDir)) {
        Write-Host "[INFO] Cleaning build directory '$BuildDir'..." -ForegroundColor Yellow
        Remove-SafeDirectory -Path $BuildDir -AllowedParent $WorkingRoot
    }
    if (!(Test-Path -LiteralPath $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }
    Set-Content -LiteralPath $BuildDir -Stream "com.dropbox.ignored" -Value 1 -ErrorAction SilentlyContinue

    # 3. Configure with CMake
    Write-Host "[1/3] Configuring CMake..." -ForegroundColor Yellow
    & cmake -S "$WorkingRoot" -B "$BuildDir" -G "Ninja" -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

    # 4. Build with Ninja
    Write-Host "[2/3] Building targets (Static Link)..." -ForegroundColor Yellow
    Invoke-NinjaBuildWithRetry -Dir $BuildDir

    # 5. Run Tests
    $testsPassedCount = 0
    $testsTotalCount = 0
    if ($Test) {
        Write-Host "[3/3] Running automated tests with CTest (All test suites)..." -ForegroundColor Yellow
        $origEA = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        $ctestOutput = & ctest --test-dir "$BuildDir" --output-on-failure 2>&1
        $testExit = $LASTEXITCODE
        $ErrorActionPreference = $origEA
        $ctestOutput | ForEach-Object { Write-Host $_ }
        if ($testExit -ne 0) { throw "CTest verification failed." }

        foreach ($line in $ctestOutput) {
            if ($line -match "(\d+)\s+tests passed") {
                $testsPassedCount = [int]$Matches[1]
            }
            if ($line -match "out of\s+(\d+)") {
                $testsTotalCount = [int]$Matches[1]
            }
        }
    } else {
        Write-Host "[3/3] Running GUI smoke test with CTest..." -ForegroundColor Yellow
        & ctest --test-dir "$BuildDir" -R TestSmokeTest --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Smoke test failed." }
        $testsPassedCount = 1
        $testsTotalCount = 1
    }

    # 6. Generate Build Provenance Metadata (BUILD-003, BUILD-005)
    $dataLoggerExe = Join-Path $BuildDir "DataLogger.exe"
    $dlogToCsvExe  = Join-Path $BuildDir "DlogToCsv.exe"
    if (!(Test-Path -LiteralPath $dataLoggerExe) -or !(Test-Path -LiteralPath $dlogToCsvExe)) {
        throw "Build finished but expected executables not found in '$BuildDir'."
    }

    $dlogHash = (Get-FileHash -LiteralPath $dataLoggerExe -Algorithm SHA256).Hash
    $dlogSize = (Get-Item -LiteralPath $dataLoggerExe).Length
    $csvHash  = (Get-FileHash -LiteralPath $dlogToCsvExe -Algorithm SHA256).Hash
    $csvSize  = (Get-Item -LiteralPath $dlogToCsvExe).Length

    $gccVer = (& c++ --version 2>&1)[0]
    $cmakeVer = (& cmake --version 2>&1)[0]
    $ninjaVer = (& ninja --version 2>&1)[0]

    $provenance = [ordered]@{
        build_timestamp = (Get-Date).ToString("o")
        compiler = $gccVer
        cmake = $cmakeVer
        ninja = $ninjaVer
        deps_cache_key = $depsCacheKey
        executables = @(
            [ordered]@{
                name = "DataLogger.exe"
                sha256 = $dlogHash
                size = $dlogSize
            },
            [ordered]@{
                name = "DlogToCsv.exe"
                sha256 = $csvHash
                size = $csvSize
            }
        )
        tests = [ordered]@{
            executed = $true
            full_suite = [bool]$Test
            passed = $testsPassedCount
            total = $testsTotalCount
        }
    }

    $provenancePath = Join-Path $BuildDir "build_provenance.json"
    $provenance | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $provenancePath -Encoding utf8
    Write-Host "[INFO] Build provenance saved to '$provenancePath'." -ForegroundColor Gray

    Write-Host "`n======================================================================" -ForegroundColor Green
    Write-Host " BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host "  - DataLogger: $dataLoggerExe (SHA256: $dlogHash)" -ForegroundColor Green
    Write-Host "  - DlogToCsv:  $dlogToCsvExe (SHA256: $csvHash)" -ForegroundColor Green
    Write-Host "  To assemble a clean distribution package, run: .\scripts\package.ps1" -ForegroundColor Gray
    Write-Host "======================================================================" -ForegroundColor Green
}
