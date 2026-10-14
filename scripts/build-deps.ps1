<#
.SYNOPSIS
    Builds third-party dependencies from official sources according to deps/dependencies.lock.json.
.DESCRIPTION
    Clones official git repositories at pinned commit SHAs, builds PortAudio and wxWidgets
    statically with MinGW-w64 UCRT, and installs headers, static libraries, and license files
    into a local, non-public .deps directory.
.PARAMETER DepsDir
    Root directory for dependency sources, builds, and installation. Defaults to <ProjectRoot>/.deps.
.PARAMETER Offline
    If specified, avoids network calls; relies on cached repositories in .deps/src or 3rdparty/.
.PARAMETER Clean
    Cleans build cache directories before rebuilding.
.PARAMETER Force
    Forces rebuilding even if a valid build_completed.json marker is present.
.PARAMETER FromLocalVendor
    Copies sources from existing 3rdparty/ directory into .deps/src instead of cloning over network.
#>
param(
    [string]$DepsDir = "",
    [switch]$Offline,
    [switch]$Clean,
    [switch]$Force,
    [switch]$FromLocalVendor
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..").Path

# Virtual drive mapping for non-ASCII / space path compatibility with Ninja & MinGW
$WorkingRoot = $ProjectRoot
$MappedDrive = $null
$CreatedSubst = $false

if ($ProjectRoot -match '[^\x00-\x7F]| ') {
    $usedDrives = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Name
    $substOutput = cmd.exe /c "subst"
    foreach ($cand in @("X", "Y", "Z", "W", "V")) {
        if ($usedDrives -contains $cand) {
            if ($substOutput -match "$($cand):\\: => (.*)") {
                $target = $Matches[1].Trim()
                if ($target.TrimEnd('\') -eq $ProjectRoot.TrimEnd('\')) {
                    $MappedDrive = "$($cand):"
                    $WorkingRoot = "$($cand):"
                    Write-Host "[INFO] Reusing existing virtual drive $MappedDrive mapped to '$ProjectRoot'." -ForegroundColor Gray
                    break
                }
            }
        }
    }
    if (!$MappedDrive) {
        foreach ($cand in @("X", "Y", "Z", "W", "V")) {
            if ($usedDrives -notcontains $cand) {
                $MappedDrive = "$($cand):"
                cmd.exe /c "subst $MappedDrive `"$ProjectRoot`" >nul 2>&1"
                if ($LASTEXITCODE -eq 0) {
                    $CreatedSubst = $true
                    $WorkingRoot = "$($cand):"
                    Write-Host "[INFO] Mapped virtual drive $MappedDrive to '$ProjectRoot' for ASCII compatibility." -ForegroundColor Gray
                    break
                }
            }
        }
    }
}

if ([string]::IsNullOrWhiteSpace($DepsDir)) {
    $DepsDir = Join-Path $WorkingRoot ".deps"
} else {
    if ($DepsDir.StartsWith($ProjectRoot)) {
        $rel = $DepsDir.Substring($ProjectRoot.Length).TrimStart('\', '/')
        $DepsDir = Join-Path $WorkingRoot $rel
    }
}

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host " Data-Logger Dependency Builder" -ForegroundColor Cyan
Write-Host " Project root: $ProjectRoot" -ForegroundColor Cyan
Write-Host " Working root: $WorkingRoot" -ForegroundColor Cyan
Write-Host " Target dir:   $DepsDir" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# 1. Load Lockfile
$LockfilePath = Join-Path $WorkingRoot "deps\dependencies.lock.json"
if (!(Test-Path $LockfilePath)) {
    throw "Lockfile not found at '$LockfilePath'."
}
$Lockfile = Get-Content $LockfilePath -Raw | ConvertFrom-Json

# 2. Check Toolchain
Write-Host "[1/6] Checking toolchain..." -ForegroundColor Yellow
$gitCmd = Get-Command git -ErrorAction SilentlyContinue
$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
$ninjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
$gccCmd = Get-Command c++ -ErrorAction SilentlyContinue

if (!$gitCmd -or !$cmakeCmd -or !$ninjaCmd -or !$gccCmd) {
    throw "Missing required build tools. Ensure git, cmake, ninja, and c++ (MinGW-w64) are in PATH."
}

$ToolchainInfo = [PSCustomObject]@{
    git     = (& git --version).Trim()
    cmake   = ((& cmake --version) | Select-Object -First 1).Trim()
    ninja   = (& ninja --version).Trim()
    gcc     = ((& c++ --version) | Select-Object -First 1).Trim()
    date    = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ssK")
}
Write-Host "  Git:   $($ToolchainInfo.git)" -ForegroundColor Gray
Write-Host "  CMake: $($ToolchainInfo.cmake)" -ForegroundColor Gray
Write-Host "  Ninja: $($ToolchainInfo.ninja)" -ForegroundColor Gray
Write-Host "  C++:   $($ToolchainInfo.gcc)" -ForegroundColor Gray

# 3. Setup directories
$SrcDir     = Join-Path $DepsDir "src"
$BuildDir   = Join-Path $DepsDir "build"
$InstallDir = Join-Path $DepsDir "install"
$LogsDir    = Join-Path $DepsDir "logs"

foreach ($dir in @($DepsDir, $SrcDir, $BuildDir, $InstallDir, $LogsDir)) {
    if (!(Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
}

$IncInstallDir = Join-Path $InstallDir "include"
$LibInstallDir = Join-Path $InstallDir "lib"
$LicInstallDir = Join-Path $InstallDir "licenses"
foreach ($dir in @($IncInstallDir, $LibInstallDir, $LicInstallDir)) {
    if (!(Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
}

# 4. Check completion marker
$MarkerPath = Join-Path $InstallDir "build_completed.json"
if (!$Force -and !$Clean -and (Test-Path $MarkerPath)) {
    Write-Host "[INFO] Valid completion marker found at '$MarkerPath'." -ForegroundColor Green
    $marker = Get-Content $MarkerPath -Raw | ConvertFrom-Json
    $allPresent = $true
    if ($marker.artifacts) {
        foreach ($lib in $marker.artifacts.libraries) {
            $libPath = Join-Path $InstallDir $lib
            if (!(Test-Path $libPath)) {
                $allPresent = $false
                break
            }
        }
    }
    if ($allPresent) {
        Write-Host "  All required dependency artifacts verified. Skipping rebuild (use -Force to rebuild)." -ForegroundColor Green
        exit 0
    }
    Write-Host "  Some artifacts missing, proceeding with build." -ForegroundColor Yellow
}

if ($Clean) {
    Write-Host "[INFO] Cleaning build directory '$BuildDir'..." -ForegroundColor Yellow
    Remove-Item -Path $BuildDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
}

try {
    # 5. Fetch / Sync Sources
    Write-Host "`n[2/6] Acquiring and verifying pinned dependency sources..." -ForegroundColor Yellow

    function Get-OrSyncDependency {
        param(
            [string]$Name,
            [PSCustomObject]$Config
        )

        $targetSrc = Join-Path $SrcDir $Name
        $pinnedCommit = $Config.commit

        if ($Offline) {
            if (!(Test-Path (Join-Path $targetSrc ".git")) -and !(Test-Path $targetSrc)) {
                $vendorDir = Join-Path $ProjectRoot "3rdparty\$Name"
                if (Test-Path $vendorDir) {
                    Write-Host "  [Offline] Copying '$Name' from local vendor directory '$vendorDir'..." -ForegroundColor Cyan
                    Copy-Item -Path $vendorDir -Destination $targetSrc -Recurse -Force
                } else {
                    throw "Offline mode specified but source for '$Name' is missing in '$targetSrc' and '3rdparty'."
                }
            }
        } else {
            if ($FromLocalVendor -and !(Test-Path $targetSrc)) {
                $vendorDir = Join-Path $ProjectRoot "3rdparty\$Name"
                if (Test-Path $vendorDir) {
                    Write-Host "  Copying '$Name' from local vendor cache '$vendorDir'..." -ForegroundColor Cyan
                    Copy-Item -Path $vendorDir -Destination $targetSrc -Recurse -Force
                }
            }

            if (!(Test-Path $targetSrc)) {
                Write-Host "  Cloning '$Name' from $($Config.repository)..." -ForegroundColor Cyan
                & git clone $Config.repository $targetSrc
                if ($LASTEXITCODE -ne 0) { throw "git clone failed for $Name." }
            } elseif (Test-Path (Join-Path $targetSrc ".git")) {
                Write-Host "  Using existing git repository at '$targetSrc'..." -ForegroundColor Gray
            } else {
                Write-Host "  Using existing source tree at '$targetSrc'..." -ForegroundColor Gray
            }

            if (Test-Path (Join-Path $targetSrc ".git")) {
                Write-Host "  Checking out pinned commit $pinnedCommit for '$Name'..." -ForegroundColor Cyan
                & git -C $targetSrc checkout -f $pinnedCommit
                if ($LASTEXITCODE -ne 0) {
                    & git -C $targetSrc fetch --all --tags
                    & git -C $targetSrc checkout -f $pinnedCommit
                    if ($LASTEXITCODE -ne 0) { throw "git checkout failed for $Name at $pinnedCommit." }
                }
            }
        }

        # Verify commit SHA if git repo exists
        if (Test-Path (Join-Path $targetSrc ".git")) {
            $actualCommit = (& git -C $targetSrc rev-parse HEAD).Trim()
            if ($actualCommit -ne $pinnedCommit) {
                throw "Commit verification failed for '$Name'. Expected: $pinnedCommit, Actual: $actualCommit"
            }
            Write-Host "  Verified '$Name' commit SHA: $actualCommit" -ForegroundColor Green
        } else {
            Write-Host "  Verified '$Name' files present in '$targetSrc'." -ForegroundColor Green
        }
    }

    # Fetch wxWidgets, portaudio, readerwriterqueue, concurrentqueue
    Get-OrSyncDependency -Name "wxWidgets" -Config $Lockfile.dependencies.wxWidgets
    Get-OrSyncDependency -Name "portaudio" -Config $Lockfile.dependencies.portaudio
    Get-OrSyncDependency -Name "readerwriterqueue" -Config $Lockfile.dependencies.readerwriterqueue
    Get-OrSyncDependency -Name "concurrentqueue" -Config $Lockfile.dependencies.concurrentqueue

    # 6. Build PortAudio
    Write-Host "`n[3/6] Building PortAudio (Static, WASAPI/MME)..." -ForegroundColor Yellow
    $paSrc = Join-Path $SrcDir "portaudio"
    $paBuild = Join-Path $BuildDir "portaudio"
    if (!(Test-Path $paBuild)) { New-Item -ItemType Directory -Path $paBuild -Force | Out-Null }

    $paOpts = @(
        "-S", $paSrc,
        "-B", $paBuild,
        "-G", "Ninja"
    ) + $Lockfile.dependencies.portaudio.cmake_options

    Write-Host "  Configuring PortAudio CMake..." -ForegroundColor Gray
    & cmake @paOpts
    if ($LASTEXITCODE -ne 0) { throw "PortAudio CMake configuration failed." }

    Write-Host "  Compiling PortAudio with Ninja..." -ForegroundColor Gray
    & cmake --build $paBuild --parallel
    if ($LASTEXITCODE -ne 0) { throw "PortAudio build failed." }

    # Find and install PortAudio library
    $paLibCandidate = Join-Path $paBuild "libportaudio.a"
    if (!(Test-Path $paLibCandidate)) {
        $found = Get-ChildItem -Path $paBuild -Filter "libportaudio.a" -Recurse | Select-Object -First 1
        if ($found) { $paLibCandidate = $found.FullName }
    }
    if (!(Test-Path $paLibCandidate)) {
        throw "Could not find built libportaudio.a in '$paBuild'."
    }

    Copy-Item $paLibCandidate -Destination (Join-Path $LibInstallDir "libportaudio.a") -Force
    Copy-Item (Join-Path $paSrc "include\*.h") -Destination $IncInstallDir -Force
    Write-Host "  Installed libportaudio.a and PortAudio headers to '$InstallDir'." -ForegroundColor Green

    # 7. Build wxWidgets
    Write-Host "`n[4/6] Building wxWidgets (Static, Unicode, Builtin image/regex libs)..." -ForegroundColor Yellow
    $wxSrc = Join-Path $SrcDir "wxWidgets"
    $wxBuild = Join-Path $BuildDir "wxWidgets"
    if (!(Test-Path $wxBuild)) { New-Item -ItemType Directory -Path $wxBuild -Force | Out-Null }

    # Check if prebuilt libraries exist in local cache when -Offline / -FromLocalVendor
    $wxLibDestDir = Join-Path $LibInstallDir "gcc_x64_lib"
    if (!(Test-Path $wxLibDestDir)) { New-Item -ItemType Directory -Path $wxLibDestDir -Force | Out-Null }

    $wxBuiltInTree = $true
    foreach ($libName in $Lockfile.dependencies.wxWidgets.output_artifacts.libraries) {
        $expected = Join-Path $wxLibDestDir $libName
        if (!(Test-Path $expected)) {
            $wxBuiltInTree = $false
            break
        }
    }

    if (!$wxBuiltInTree -or $Force) {
        # Check if 3rdparty prebuilt is available for offline / local-vendor fast bootstrap
        $vendorWxLib = Join-Path $ProjectRoot "3rdparty\wxWidgets\lib\gcc_x64_lib"
        if ($FromLocalVendor -and (Test-Path $vendorWxLib)) {
            Write-Host "  Copying pre-verified wxWidgets static libraries from local vendor cache..." -ForegroundColor Cyan
            Copy-Item -Path "$vendorWxLib\*.a" -Destination $wxLibDestDir -Force
            # Copy setup.h directory
            Copy-Item -Path "$vendorWxLib\mswu" -Destination $wxLibDestDir -Recurse -Force
        } else {
            $wxOpts = @(
                "-S", $wxSrc,
                "-B", $wxBuild,
                "-G", "Ninja"
            ) + $Lockfile.dependencies.wxWidgets.cmake_options

            Write-Host "  Configuring wxWidgets CMake..." -ForegroundColor Gray
            & cmake @wxOpts
            if ($LASTEXITCODE -ne 0) { throw "wxWidgets CMake configuration failed." }

            Write-Host "  Compiling wxWidgets targets (wxbase, wxcore, image/regex libs)..." -ForegroundColor Gray
            & cmake --build $wxBuild --parallel --target wxbase wxcore wxpng wxjpeg wxtiff wxzlib wxregex
            if ($LASTEXITCODE -ne 0) { throw "wxWidgets build failed." }

            # Gather built static libraries from wxBuild
            $wxFoundLibs = Get-ChildItem -Path $wxBuild -Filter "*.a" -Recurse
            foreach ($flib in $wxFoundLibs) {
                Copy-Item $flib.FullName -Destination $wxLibDestDir -Force
            }

            # Find generated setup.h
            $setupFound = Get-ChildItem -Path $wxBuild -Filter "setup.h" -Recurse | Select-Object -First 1
            if ($setupFound) {
                $mswuDir = Join-Path $wxLibDestDir "mswu\wx"
                if (!(Test-Path $mswuDir)) { New-Item -ItemType Directory -Path $mswuDir -Force | Out-Null }
                Copy-Item $setupFound.FullName -Destination $mswuDir -Force
            }
        }
    }

    # Copy wxWidgets public headers
    Write-Host "  Installing wxWidgets headers to '$IncInstallDir\wx'..." -ForegroundColor Gray
    $wxIncSrc = Join-Path $wxSrc "include\wx"
    if (Test-Path $wxIncSrc) {
        $wxIncDest = Join-Path $IncInstallDir "wx"
        if (!(Test-Path $wxIncDest)) { New-Item -ItemType Directory -Path $wxIncDest -Force | Out-Null }
        Copy-Item -Path "$wxIncSrc\*" -Destination $wxIncDest -Recurse -Force
    }

    # Verify wxWidgets libraries
    foreach ($libName in $Lockfile.dependencies.wxWidgets.output_artifacts.libraries) {
        $targetLib = Join-Path $wxLibDestDir $libName
        if (!(Test-Path $targetLib)) {
            throw "Missing required wxWidgets library '$libName' in '$wxLibDestDir'."
        }
    }
    Write-Host "  Verified and installed all wxWidgets static libraries." -ForegroundColor Green

    # 8. Install Header-Only Queue Libraries
    Write-Host "`n[5/6] Installing ReaderWriterQueue and ConcurrentQueue headers..." -ForegroundColor Yellow
    $rwqSrc = Join-Path $SrcDir "readerwriterqueue"
    Copy-Item (Join-Path $rwqSrc "readerwriterqueue.h") -Destination $IncInstallDir -Force
    Copy-Item (Join-Path $rwqSrc "atomicops.h") -Destination $IncInstallDir -Force
    $rwqSubDir = Join-Path $IncInstallDir "readerwriterqueue"
    if (!(Test-Path $rwqSubDir)) { New-Item -ItemType Directory -Path $rwqSubDir -Force | Out-Null }
    Copy-Item (Join-Path $rwqSrc "readerwriterqueue.h") -Destination $rwqSubDir -Force
    Copy-Item (Join-Path $rwqSrc "atomicops.h") -Destination $rwqSubDir -Force

    $cqSrc = Join-Path $SrcDir "concurrentqueue"
    Copy-Item (Join-Path $cqSrc "concurrentqueue.h") -Destination $IncInstallDir -Force
    Copy-Item (Join-Path $cqSrc "blockingconcurrentqueue.h") -Destination $IncInstallDir -Force
    Copy-Item (Join-Path $cqSrc "lightweightsemaphore.h") -Destination $IncInstallDir -Force
    $cqSubDir = Join-Path $IncInstallDir "concurrentqueue"
    if (!(Test-Path $cqSubDir)) { New-Item -ItemType Directory -Path $cqSubDir -Force | Out-Null }
    Copy-Item (Join-Path $cqSrc "concurrentqueue.h") -Destination $cqSubDir -Force
    Copy-Item (Join-Path $cqSrc "blockingconcurrentqueue.h") -Destination $cqSubDir -Force
    Copy-Item (Join-Path $cqSrc "lightweightsemaphore.h") -Destination $cqSubDir -Force

    Write-Host "  Installed header-only queues into '$IncInstallDir'." -ForegroundColor Green

    # 9. Collect License Files
    Write-Host "`n[6/6] Collecting third-party license texts..." -ForegroundColor Yellow
    function Collect-License {
        param([string]$DepName, [string]$SrcPath, [string]$RelPath)
        $fullPath = Join-Path $SrcPath $RelPath
        if (Test-Path $fullPath) {
            $destSub = Join-Path $LicInstallDir $DepName
            if (!(Test-Path $destSub)) { New-Item -ItemType Directory -Path $destSub -Force | Out-Null }
            Copy-Item $fullPath -Destination $destSub -Force
            Write-Host "  Collected license for $DepName from '$fullPath'." -ForegroundColor Gray
        } else {
            Write-Warning "License text not found at '$fullPath'."
        }
    }

    Collect-License -DepName "wxWidgets" -SrcPath $wxSrc -RelPath $Lockfile.dependencies.wxWidgets.license.relative_path
    Collect-License -DepName "portaudio" -SrcPath $paSrc -RelPath $Lockfile.dependencies.portaudio.license.relative_path
    Collect-License -DepName "readerwriterqueue" -SrcPath $rwqSrc -RelPath $Lockfile.dependencies.readerwriterqueue.license.relative_path
    Collect-License -DepName "concurrentqueue" -SrcPath $cqSrc -RelPath $Lockfile.dependencies.concurrentqueue.license.relative_path

    # 10. Generate build_completed.json marker
    $ArtifactList = @(
        "lib\libportaudio.a",
        "lib\gcc_x64_lib\libwxmsw32u_core.a",
        "lib\gcc_x64_lib\libwxbase32u.a",
        "lib\gcc_x64_lib\libwxpng.a",
        "lib\gcc_x64_lib\libwxjpeg.a",
        "lib\gcc_x64_lib\libwxtiff.a",
        "lib\gcc_x64_lib\libwxzlib.a",
        "lib\gcc_x64_lib\libwxregexu.a"
    )

    $LibraryHashes = @{}
    foreach ($art in $ArtifactList) {
        $artPath = Join-Path $InstallDir $art
        if (Test-Path $artPath) {
            $hash = (Get-FileHash -Path $artPath -Algorithm SHA256).Hash
            $LibraryHashes[$art] = $hash
        }
    }

    $MarkerContent = [PSCustomObject]@{
        status        = "SUCCESS"
        build_date    = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ssK")
        toolchain     = $ToolchainInfo
        dependencies  = $Lockfile.dependencies
        artifacts     = [PSCustomObject]@{
            libraries = $ArtifactList
            hashes    = $LibraryHashes
        }
    }

    $MarkerContent | ConvertTo-Json -Depth 10 | Set-Content $MarkerPath -Encoding UTF8

    Write-Host "`n======================================================================" -ForegroundColor Green
    Write-Host " DEPENDENCY BUILD SUCCESSFUL!" -ForegroundColor Green
    Write-Host "  Install root: $InstallDir" -ForegroundColor Green
    Write-Host "  Marker:       $MarkerPath" -ForegroundColor Green
    Write-Host "======================================================================" -ForegroundColor Green
    exit 0
}
finally {
    if ($CreatedSubst -and $MappedDrive) {
        cmd.exe /c "subst $MappedDrive /d >nul 2>&1"
    }
}
