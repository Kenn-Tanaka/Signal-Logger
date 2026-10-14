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

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Data-Logger Suite Build Script" -ForegroundColor Cyan
Write-Host " Project root: $ProjectRoot" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# 1. Virtual drive mapping for path compatibility (MinGW/Ninja ASCII safety)
$WorkingRoot = $ProjectRoot
$VirtDrive = "X:"
$CreatedSubst = $false

if ($ProjectRoot -match '[^\x00-\x7F]| ') {
    $usedDrives = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Name
    $substList = cmd.exe /c "subst"
    if ($usedDrives -contains "X") {
        if ($substList -match "X:\\: => (.*)") {
            $target = $Matches[1].Trim()
            if ($target.TrimEnd('\') -eq $ProjectRoot.TrimEnd('\')) {
                $WorkingRoot = "X:\"
                Write-Host "[INFO] Reusing existing virtual drive X: mapped to '$ProjectRoot'." -ForegroundColor Gray
            }
        }
    }
    if ($WorkingRoot -eq $ProjectRoot) {
        cmd.exe /c "subst $VirtDrive `"$ProjectRoot`" >nul 2>&1"
        if ($LASTEXITCODE -eq 0) {
            $CreatedSubst = $true
            $WorkingRoot = "X:\"
            Write-Host "[INFO] Mapped virtual drive $VirtDrive to '$ProjectRoot' for ASCII compatibility." -ForegroundColor Gray
        }
    }
}

try {
    # 2. Check Dependencies
    $DepsMarker = Join-Path $WorkingRoot ".deps\install\build_completed.json"
    $VendorLib  = Join-Path $WorkingRoot "3rdparty\portaudio\lib\libportaudio.a"
    if (!(Test-Path $DepsMarker) -and !(Test-Path $VendorLib)) {
        Write-Host "[INFO] Dependencies not yet built. Running scripts/build-deps.ps1..." -ForegroundColor Yellow
        & "$PSScriptRoot\scripts\build-deps.ps1"
        if ($LASTEXITCODE -ne 0) {
            throw "Dependency build failed."
        }
    }

    # 3. Setup Build Directory
    if ([string]::IsNullOrWhiteSpace($BuildDir)) {
        $BuildDir = Join-Path $WorkingRoot "build"
    }
    if ($Clean -and (Test-Path $BuildDir)) {
        Write-Host "[INFO] Cleaning build directory '$BuildDir'..." -ForegroundColor Yellow
        Remove-Item -Path $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    }
    if (!(Test-Path $BuildDir)) {
        New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
    }

    # 4. Configure with CMake
    Write-Host "[1/3] Configuring CMake..." -ForegroundColor Yellow
    & cmake -S "$WorkingRoot" -B "$BuildDir" -G "Ninja" -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }

    # 5. Build with Ninja
    Write-Host "[2/3] Building targets (Static Link)..." -ForegroundColor Yellow
    & cmake --build "$BuildDir" --parallel
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed." }

    # 6. Run Tests
    if ($Test) {
        Write-Host "[3/3] Running automated tests with CTest (All test suites)..." -ForegroundColor Yellow
        & ctest --test-dir "$BuildDir" --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "CTest verification failed." }
    } else {
        Write-Host "[3/3] Running GUI smoke test with CTest..." -ForegroundColor Yellow
        & ctest --test-dir "$BuildDir" -R TestSmokeTest --output-on-failure
        if ($LASTEXITCODE -ne 0) { throw "Smoke test failed." }
    }

    Write-Host "`n======================================================================" -ForegroundColor Green
    Write-Host " BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host "  - DataLogger: $BuildDir\DataLogger.exe" -ForegroundColor Green
    Write-Host "  - DlogToCsv:  $BuildDir\DlogToCsv.exe" -ForegroundColor Green
    Write-Host "  To assemble a clean distribution package, run: .\scripts\package.ps1" -ForegroundColor Gray
    Write-Host "======================================================================" -ForegroundColor Green
    exit 0
}
finally {
    if ($CreatedSubst) {
        Write-Host "[INFO] Unmounting virtual drive $VirtDrive..." -ForegroundColor Gray
        cmd.exe /c "subst $VirtDrive /d >nul 2>&1"
    }
}
