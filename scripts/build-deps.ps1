<#
.SYNOPSIS
    Builds third-party dependencies from official sources according to deps/dependencies.lock.json.
.DESCRIPTION
    Clones official git repositories at pinned commit SHAs, builds PortAudio and wxWidgets
    statically with MinGW-w64 UCRT, and installs headers, static libraries, and license files
    into a local, non-public .deps directory. Uses isolated staging and deterministic cache keys.
.PARAMETER DepsDir
    Root directory for dependency sources, builds, and installation. Defaults to <ProjectRoot>/.deps.
.PARAMETER Offline
    If specified, avoids network calls; relies on cached repositories in .deps/src.
.PARAMETER Clean
    Cleans build cache directories before rebuilding.
.PARAMETER Force
    Forces rebuilding even if a valid build_completed.json marker is present.
.PARAMETER FromLocalVendor
    Copies sources from existing 3rdparty/ directory into .deps/src only if git commit matches lockfile.
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

# Load common drive and path safety helpers
. "$PSScriptRoot\common-drive.ps1"

$ProjectRoot = (Resolve-Path "$PSScriptRoot\..").Path

Invoke-WithSafeMappedDrive -TargetDirectory $ProjectRoot -Action {
    param([string]$WorkingRoot)

    Write-Host "======================================================================" -ForegroundColor Cyan
    Write-Host " Data-Logger Dependency Builder (Deterministic & Staged)" -ForegroundColor Cyan
    Write-Host " Project root: $ProjectRoot" -ForegroundColor Cyan
    Write-Host " Working root: $WorkingRoot" -ForegroundColor Cyan
    Write-Host "======================================================================" -ForegroundColor Cyan

    if ([string]::IsNullOrWhiteSpace($DepsDir)) {
        $DepsDir = Join-Path $WorkingRoot ".deps"
    } else {
        if ($DepsDir.StartsWith($ProjectRoot)) {
            $rel = $DepsDir.Substring($ProjectRoot.Length).TrimStart('\', '/')
            $DepsDir = Join-Path $WorkingRoot $rel
        }
    }

    # 1. Load Lockfile
    $LockfilePath = Join-Path $WorkingRoot "deps\dependencies.lock.json"
    if (!(Test-Path -LiteralPath $LockfilePath)) {
        throw "Lockfile not found at '$LockfilePath'."
    }
    $LockfileRaw = Get-Content -LiteralPath $LockfilePath -Raw -Encoding UTF8
    $Lockfile = $LockfileRaw | ConvertFrom-Json

    # 2. Check Toolchain
    Write-Host "[1/6] Checking toolchain..." -ForegroundColor Yellow
    $gitCmd = Get-Command git -ErrorAction SilentlyContinue
    $cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
    $ninjaCmd = Get-Command ninja -ErrorAction SilentlyContinue
    $gccCmd = Get-Command c++ -ErrorAction SilentlyContinue

    if (!$gitCmd -or !$cmakeCmd -or !$ninjaCmd -or !$gccCmd) {
        throw "Missing required build tools. Ensure git, cmake, ninja, and c++ (MinGW-w64) are in PATH."
    }

    $gccVerbose = (cmd.exe /c "c++ -v 2>&1" | Out-String).Trim()
    $ToolchainInfo = [PSCustomObject]@{
        git     = (& git --version).Trim()
        cmake   = ((& cmake --version) | Select-Object -First 1).Trim()
        ninja   = (& ninja --version).Trim()
        gcc     = ((& c++ --version) | Select-Object -First 1).Trim()
        gcc_details = $gccVerbose
    }
    Write-Host "  Git:   $($ToolchainInfo.git)" -ForegroundColor Gray
    Write-Host "  CMake: $($ToolchainInfo.cmake)" -ForegroundColor Gray
    Write-Host "  Ninja: $($ToolchainInfo.ninja)" -ForegroundColor Gray
    Write-Host "  C++:   $($ToolchainInfo.gcc)" -ForegroundColor Gray

    # 3. Calculate Deterministic Cache Key
    # Hashes: lockfile content + compiler verbose details + cmake version + ninja version
    $sha = [System.Security.Cryptography.SHA256]::Create()
    $cacheKeyInput = [System.Text.Encoding]::UTF8.GetBytes(
        "LOCKFILE:`n$LockfileRaw`nGCC:`n$gccVerbose`nCMAKE:`n$($ToolchainInfo.cmake)`nNINJA:`n$($ToolchainInfo.ninja)"
    )
    $CacheKey = [BitConverter]::ToString($sha.ComputeHash($cacheKeyInput)).Replace("-", "").ToUpper()
    Write-Host "  Dependency Cache Key: $CacheKey" -ForegroundColor Gray

    # 4. Check Existing Install & Artifact Integrity
    $InstallDir = Join-Path $DepsDir "install"
    $MarkerPath = Join-Path $InstallDir "build_completed.json"

    if (!$Force -and !$Clean -and (Test-Path -LiteralPath $MarkerPath)) {
        try {
            $marker = Get-Content -LiteralPath $MarkerPath -Raw -Encoding UTF8 | ConvertFrom-Json
            if ($marker.PSObject.Properties['cache_key'] -and $marker.cache_key -eq $CacheKey) {
                Write-Host "[INFO] Cache key matched. Verifying artifact file hashes..." -ForegroundColor Green
                $allValid = $true
                if ($marker.artifacts -and $marker.artifacts.hashes) {
                    foreach ($prop in $marker.artifacts.hashes.PSObject.Properties) {
                        $artRel = $prop.Name
                        $expectedHash = $prop.Value
                        $artFull = Join-Path $InstallDir $artRel
                        if (!(Test-Path -LiteralPath $artFull)) {
                            Write-Host "  [MISSING] $artRel" -ForegroundColor Yellow
                            $allValid = $false
                            break
                        }
                        $actualHash = (Get-FileHash -LiteralPath $artFull -Algorithm SHA256).Hash
                        if ($actualHash -ne $expectedHash) {
                            Write-Host "  [HASH MISMATCH] $artRel (expected $expectedHash, got $actualHash)" -ForegroundColor Yellow
                            $allValid = $false
                            break
                        }
                    }
                } else {
                    $allValid = $false
                }

                if ($allValid) {
                    Write-Host "[INFO] All dependency artifacts verified successfully with SHA-256 matching. Skipping build (use -Force to rebuild)." -ForegroundColor Green
                    return
                } else {
                    Write-Host "[INFO] Artifacts missing or modified. Proceeding with clean rebuild." -ForegroundColor Yellow
                }
            } else {
                Write-Host "[INFO] Cache key mismatch (toolchain, lockfile, or options changed). Rebuilding." -ForegroundColor Yellow
            }
        } catch {
            Write-Host "[INFO] Existing marker invalid. Proceeding with build." -ForegroundColor Yellow
        }
    }

    if ($Offline -and !(Test-Path -LiteralPath (Join-Path $DepsDir "src"))) {
        throw "Offline mode specified, but source cache in '$DepsDir\src' does not exist."
    }

    # 5. Prepare Staging Directories
    $StageGuid = [Guid]::NewGuid().ToString("N")
    $StageRoot = Join-Path $DepsDir "staging_$StageGuid"
    $StageInstall = Join-Path $StageRoot "install"
    $SrcDir = Join-Path $DepsDir "src"
    $BuildDir = Join-Path $DepsDir "build"
    $LogsDir = Join-Path $DepsDir "logs"

    # Tell Dropbox to ignore the .deps build tree to prevent sharing violations during ninja builds
    Set-Content -LiteralPath $DepsDir -Stream "com.dropbox.ignored" -Value 1 -ErrorAction SilentlyContinue

    foreach ($d in @($DepsDir, $SrcDir, $LogsDir, $StageRoot, $StageInstall, $BuildDir)) {
        if (!(Test-Path -LiteralPath $d)) {
            New-Item -ItemType Directory -Path $d -Force | Out-Null
        }
    }
    # Place ownership marker in staging
    Set-Content -LiteralPath (Join-Path $StageRoot ".dlog_stage_owner") -Value "DataLogger Staging $StageGuid" -Encoding UTF8

    $IncInstallDir = Join-Path $StageInstall "include"
    $LibInstallDir = Join-Path $StageInstall "lib"
    $LicInstallDir = Join-Path $StageInstall "licenses"
    foreach ($d in @($IncInstallDir, $LibInstallDir, $LicInstallDir)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
    }

    function Invoke-NinjaBuildWithRetry {
        param(
            [string]$Dir,
            [string[]]$Targets
        )
        $attempts = @(
            @("-j", "4", "-k", "0"),
            @("-j", "2", "-k", "0"),
            @("-j", "1")
        )
        for ($i = 0; $i -lt $attempts.Count; $i++) {
            $flags = $attempts[$i]
            $cmd = @("--build", $Dir)
            if ($Targets -and $Targets.Count -gt 0) {
                $cmd += @("--target") + $Targets
            }
            $cmd += @("--") + $flags
            & cmake @cmd
            if ($LASTEXITCODE -eq 0) {
                return
            }
            Write-Host "  [WARN] Build pass $($i + 1) encountered transient lock (exit $LASTEXITCODE). Retrying with $($flags -join ' ')..." -ForegroundColor Yellow
            Start-Sleep -Seconds 1
        }
        throw "Build failed in '$Dir' after $($attempts.Count) passes."
    }

    try {
        # 6. Acquire / Verify Sources
        Write-Host "`n[2/6] Acquiring and verifying pinned dependency sources..." -ForegroundColor Yellow

        function Sync-DependencySource {
            param(
                [string]$Name,
                [PSCustomObject]$Config
            )

            $targetSrc = Join-Path $SrcDir $Name
            $pinnedCommit = $Config.commit

            if ($Offline) {
                if (!(Test-Path -LiteralPath $targetSrc)) {
                    throw "Offline mode specified but source for '$Name' is missing in '$targetSrc'."
                }
            } else {
                if ($FromLocalVendor -and !(Test-Path -LiteralPath $targetSrc)) {
                    $vendorDir = Join-Path $ProjectRoot "3rdparty\$Name"
                    if (Test-Path -LiteralPath $vendorDir) {
                        # Verify vendor git commit if git exists
                        if (Test-Path -LiteralPath (Join-Path $vendorDir ".git")) {
                            $vendorCommit = (& git -C $vendorDir rev-parse HEAD 2>$null).Trim()
                            if ($vendorCommit -eq $pinnedCommit) {
                                Write-Host "  Copying '$Name' from verified local vendor directory '$vendorDir'..." -ForegroundColor Cyan
                                Copy-Item -LiteralPath $vendorDir -Destination $targetSrc -Recurse -Force
                            }
                        }
                    }
                }

                if (!(Test-Path -LiteralPath $targetSrc)) {
                    Write-Host "  Cloning '$Name' from $($Config.repository)..." -ForegroundColor Cyan
                    & git clone $Config.repository $targetSrc
                    if ($LASTEXITCODE -ne 0) { throw "git clone failed for $Name." }
                }
            }

            if (Test-Path -LiteralPath (Join-Path $targetSrc ".git")) {
                Write-Host "  Checking out $Name at pinned commit $pinnedCommit..." -ForegroundColor Cyan
                & git -C $targetSrc checkout --force $pinnedCommit
                if ($LASTEXITCODE -ne 0) { throw "git checkout failed for $Name at $pinnedCommit." }

                # Verify current commit
                $currentCommit = (& git -C $targetSrc rev-parse HEAD).Trim()
                if ($currentCommit -ne $pinnedCommit) {
                    throw "Commit mismatch for ${Name}: Expected $pinnedCommit, got $currentCommit."
                }

                # Handle submodules if configured
                if ($Config.PSObject.Properties['submodules'] -and $Config.submodules) {
                    Write-Host "  Updating submodules for $Name..." -ForegroundColor Cyan
                    & git -C $targetSrc submodule update --init --recursive
                    if ($LASTEXITCODE -ne 0) { throw "git submodule update failed for $Name." }
                }
            } else {
                if ($Name -eq "wxWidgets") {
                    $licPath = Join-Path $targetSrc "docs\licence.txt"
                    if (Test-Path -LiteralPath $licPath) {
                        $hash = (Get-FileHash -LiteralPath $licPath -Algorithm SHA256).Hash.ToUpperInvariant()
                        if ($hash -eq "E8453BE21AEA2B7CC9A39D5B0475190BBFA0A60605B4178377590B94D9C43EAE") {
                            Write-Host "  Source for $Name verified via docs/licence.txt SHA-256." -ForegroundColor Green
                        } else {
                            throw "Source integrity check failed for ${Name}: docs/licence.txt hash mismatch ($hash)."
                        }
                    } else {
                        throw "Source for $Name is missing docs/licence.txt."
                    }
                } else {
                    Write-Host "  [WARN] Source for $Name in '$targetSrc' has no .git metadata. Cannot verify commit." -ForegroundColor Yellow
                }
            }
        }

        Sync-DependencySource -Name "wxWidgets" -Config $Lockfile.dependencies.wxWidgets
        Sync-DependencySource -Name "portaudio" -Config $Lockfile.dependencies.portaudio
        Sync-DependencySource -Name "readerwriterqueue" -Config $Lockfile.dependencies.readerwriterqueue
        Sync-DependencySource -Name "concurrentqueue" -Config $Lockfile.dependencies.concurrentqueue

        # 7. Build PortAudio
        Write-Host "`n[3/6] Building PortAudio (Static, MME + WASAPI)..." -ForegroundColor Yellow
        $paSrc = Join-Path $SrcDir "portaudio"
        $paBuild = Join-Path $BuildDir "portaudio"
        New-Item -ItemType Directory -Path $paBuild -Force | Out-Null

        $paOptions = @(
            "-S", $paSrc,
            "-B", $paBuild,
            "-G", "Ninja",
            "-DCMAKE_INSTALL_PREFIX=$StageInstall"
        )
        $paOptions += $Lockfile.dependencies.portaudio.cmake_options

        Write-Host "  Configuring PortAudio with CMake..." -ForegroundColor Gray
        & cmake @paOptions
        if ($LASTEXITCODE -ne 0) { throw "PortAudio CMake configuration failed." }

        Write-Host "  Building PortAudio..." -ForegroundColor Gray
        Invoke-NinjaBuildWithRetry -Dir $paBuild -Targets @("portaudio")

        # Install PortAudio artifacts
        $paLibBuilt = Join-Path $paBuild "libportaudio_static.a"
        if (!(Test-Path -LiteralPath $paLibBuilt)) {
            $paLibBuilt = Join-Path $paBuild "libportaudio.a"
        }
        if (!(Test-Path -LiteralPath $paLibBuilt)) {
            throw "PortAudio static library not found in '$paBuild'."
        }
        Copy-Item -LiteralPath $paLibBuilt -Destination (Join-Path $LibInstallDir "libportaudio.a") -Force
        Copy-Item -LiteralPath (Join-Path $paSrc "include\portaudio.h") -Destination (Join-Path $IncInstallDir "portaudio.h") -Force
        if (Test-Path -LiteralPath (Join-Path $paSrc "include\pa_win_wasapi.h")) {
            Copy-Item -LiteralPath (Join-Path $paSrc "include\pa_win_wasapi.h") -Destination (Join-Path $IncInstallDir "pa_win_wasapi.h") -Force
        }

        # 8. Build wxWidgets
        Write-Host "`n[4/6] Building wxWidgets (Static, Unicode, Core + Base)..." -ForegroundColor Yellow
        $wxSrc = Join-Path $SrcDir "wxWidgets"
        $wxBuild = Join-Path $BuildDir "wxWidgets"
        New-Item -ItemType Directory -Path $wxBuild -Force | Out-Null

        $wxOptions = @(
            "-S", $wxSrc,
            "-B", $wxBuild,
            "-G", "Ninja",
            "-DCMAKE_INSTALL_PREFIX=$StageInstall"
        )
        $wxOptions += $Lockfile.dependencies.wxWidgets.cmake_options

        Write-Host "  Configuring wxWidgets with CMake..." -ForegroundColor Gray
        & cmake @wxOptions
        if ($LASTEXITCODE -ne 0) { throw "wxWidgets CMake configuration failed." }

        Write-Host "  Building wxWidgets targets..." -ForegroundColor Gray
        Invoke-NinjaBuildWithRetry -Dir $wxBuild -Targets @("wxcore", "wxbase")

        # Copy wxWidgets library files to StageInstall/lib/gcc_x64_lib
        $wxLibStageDir = Join-Path $LibInstallDir "gcc_x64_lib"
        New-Item -ItemType Directory -Path $wxLibStageDir -Force | Out-Null

        $wxLibFiles = Get-ChildItem -Path (Join-Path $wxBuild "lib") -Filter "*.a" -Recurse
        foreach ($lib in $wxLibFiles) {
            Copy-Item -LiteralPath $lib.FullName -Destination (Join-Path $wxLibStageDir $lib.Name) -Force
        }

        # Copy wxWidgets setup.h and headers
        $setupHeader = Get-ChildItem -Path (Join-Path $wxBuild "lib") -Filter "setup.h" -Recurse | Select-Object -First 1
        if ($setupHeader) {
            $destSetupDir = Join-Path $wxLibStageDir "mswu\wx"
            New-Item -ItemType Directory -Path $destSetupDir -Force | Out-Null
            Copy-Item -LiteralPath $setupHeader.FullName -Destination (Join-Path $destSetupDir "setup.h") -Force
        } else {
            throw "wxWidgets setup.h not found in build directory."
        }

        # Copy include/wx tree
        Copy-Item -LiteralPath (Join-Path $wxSrc "include\wx") -Destination (Join-Path $IncInstallDir "wx") -Recurse -Force

        # 9. Install ReaderWriterQueue and ConcurrentQueue headers
        Write-Host "`n[5/6] Installing Queue headers..." -ForegroundColor Yellow
        $rwqSrc = Join-Path $SrcDir "readerwriterqueue"
        Copy-Item -LiteralPath (Join-Path $rwqSrc "readerwriterqueue.h") -Destination (Join-Path $IncInstallDir "readerwriterqueue.h") -Force
        Copy-Item -LiteralPath (Join-Path $rwqSrc "atomicops.h") -Destination (Join-Path $IncInstallDir "atomicops.h") -Force

        $cqSrc = Join-Path $SrcDir "concurrentqueue"
        Copy-Item -LiteralPath (Join-Path $cqSrc "concurrentqueue.h") -Destination (Join-Path $IncInstallDir "concurrentqueue.h") -Force
        Copy-Item -LiteralPath (Join-Path $cqSrc "blockingconcurrentqueue.h") -Destination (Join-Path $IncInstallDir "blockingconcurrentqueue.h") -Force
        Copy-Item -LiteralPath (Join-Path $cqSrc "lightweightsemaphore.h") -Destination (Join-Path $IncInstallDir "lightweightsemaphore.h") -Force

        # 10. Collect Official License Texts
        Write-Host "`n[6/6] Collecting official license notices and computing checksums..." -ForegroundColor Yellow

        function Install-LicenseNotice {
            param(
                [string]$Category,
                [string]$FileName,
                [string]$SourcePath
            )
            $catDir = Join-Path $LicInstallDir $Category
            New-Item -ItemType Directory -Path $catDir -Force | Out-Null
            $dest = Join-Path $catDir $FileName
            Copy-Item -LiteralPath $SourcePath -Destination $dest -Force
        }

        # wxWidgets main & built-in library licenses
        Install-LicenseNotice -Category "wxWidgets" -FileName "licence.txt" -SourcePath (Join-Path $wxSrc $Lockfile.dependencies.wxWidgets.license.relative_path)
        Install-LicenseNotice -Category "wxWidgets" -FileName "libpng_LICENSE" -SourcePath (Join-Path $wxSrc "src\png\LICENSE")
        Install-LicenseNotice -Category "wxWidgets" -FileName "libjpeg_README" -SourcePath (Join-Path $wxSrc "src\jpeg\README")
        Install-LicenseNotice -Category "wxWidgets" -FileName "libtiff_COPYRIGHT" -SourcePath (Join-Path $wxSrc "src\tiff\COPYRIGHT")
        Install-LicenseNotice -Category "wxWidgets" -FileName "zlib_LICENSE" -SourcePath (Join-Path $wxSrc "src\zlib\LICENSE")
        Install-LicenseNotice -Category "wxWidgets" -FileName "regex_COPYRIGHT" -SourcePath (Join-Path $wxSrc "src\regex\COPYRIGHT")

        # PortAudio, ReaderWriterQueue, ConcurrentQueue
        Install-LicenseNotice -Category "portaudio" -FileName "LICENSE.txt" -SourcePath (Join-Path $paSrc $Lockfile.dependencies.portaudio.license.relative_path)
        Install-LicenseNotice -Category "readerwriterqueue" -FileName "LICENSE.md" -SourcePath (Join-Path $rwqSrc $Lockfile.dependencies.readerwriterqueue.license.relative_path)
        Install-LicenseNotice -Category "concurrentqueue" -FileName "LICENSE.md" -SourcePath (Join-Path $cqSrc $Lockfile.dependencies.concurrentqueue.license.relative_path)

        # MinGW-w64 runtime & GCC Runtime Library Exception notices
        $mingwLicDir = Join-Path $LicInstallDir "mingw-w64"
        New-Item -ItemType Directory -Path $mingwLicDir -Force | Out-Null

        $gccExceptionText = @"
GCC RUNTIME LIBRARY EXCEPTION

Version 3.1, 31 March 2009

Copyright (C) 2009 Free Software Foundation, Inc. <http://fsf.org/>

Everyone is permitted to copy and distribute verbatim copies of this
license document, but changing it is not allowed.

This GCC Runtime Library Exception ("Exception") is an additional
permission under section 7 of the GNU General Public License, version
3 ("GPLv3"). It applies to a given file (the "Runtime Library") that
bears a notice placed by the copyright holder of the file stating that
the file is governed by GPLv3 along with this Exception.

When you use GCC to compile a program, GCC may combine portions of
certain GCC header files and runtime libraries with the compiled
program. The purpose of this Exception is to allow compilation of
non-GPL (including proprietary) programs to use, in this way, the
header files and runtime libraries covered by this Exception.

0. Definitions.

A file is an "Independent Module" if it either requires the Runtime
Library for execution after a Compilation Process, or makes use of an
interface provided by the Runtime Library, but is not otherwise based
on the Runtime Library.

"GCC" means a version of the GNU Compiler Collection, with or without
modifications, governed by version 3 (or a specified later version) of
the GNU General Public License (GPL) with the option of using any
subsequent versions published by the FSF.

"GPL-compatible Software" is software whose conditions of propagation
grant permission to combine it with a work licensed under the GPLv3
or any later version.

"Target Code" refers to output from any compiler for a real or virtual
target processor architecture, in executable for that processor
architecture or in a format that can be used to generate executable
for that processor architecture.

The "Compilation Process" transforms code entirely represented in
high-level languages into Target Code, that is, it generates Target
Code from source code with no intermediate transformations.

1. Grant of Additional Permission.

You have permission to propagate a work of Target Code formed by
combining the Runtime Library with Independent Modules, even if such
propagation would otherwise violate the terms of GPLv3, provided that
all Target Code was generated by Eligible Compilation Processes. You
may then convey such a combination under terms of your choice,
consistent with the licensing of the Independent Modules.

2. No Weakening of GCC Copyleft.

The availability of this Exception does not imply any general
presumption that GCC itself is unaffected by the copyleft
requirements of the license of GCC.
"@
        Set-Content -LiteralPath (Join-Path $mingwLicDir "GCC_RUNTIME_EXCEPTION_v3.1.txt") -Value $gccExceptionText -Encoding UTF8

        $mingwRuntimeText = @"
MinGW-w64 Runtime Notices:
The MinGW-w64 runtime library files (including libgcc, libstdc++, libwinpthread, and CRT startup objects)
are governed by their respective licenses:
- MinGW-w64 CRT components are in the Public Domain or licensed under BSD-style/ZPL terms.
- winpthreads is licensed under the MIT License (Copyright (c) 2011 mingw-w64 project).
- libgcc and libstdc++ are covered by GPLv3 with the GCC Runtime Library Exception Version 3.1.
"@
        Set-Content -LiteralPath (Join-Path $mingwLicDir "MINGW_W64_RUNTIME_NOTICES.txt") -Value $mingwRuntimeText -Encoding UTF8

        # 11. Compute Checksums of All Installed Artifacts
        $RequiredArtifacts = @(
            "lib\libportaudio.a",
            "lib\gcc_x64_lib\libwxmsw32u_core.a",
            "lib\gcc_x64_lib\libwxbase32u.a",
            "lib\gcc_x64_lib\libwxpng.a",
            "lib\gcc_x64_lib\libwxjpeg.a",
            "lib\gcc_x64_lib\libwxtiff.a",
            "lib\gcc_x64_lib\libwxzlib.a",
            "lib\gcc_x64_lib\libwxregexu.a",
            "lib\gcc_x64_lib\mswu\wx\setup.h",
            "include\portaudio.h",
            "include\readerwriterqueue.h",
            "include\atomicops.h",
            "include\concurrentqueue.h",
            "licenses\wxWidgets\licence.txt",
            "licenses\wxWidgets\libpng_LICENSE",
            "licenses\wxWidgets\libjpeg_README",
            "licenses\wxWidgets\libtiff_COPYRIGHT",
            "licenses\wxWidgets\zlib_LICENSE",
            "licenses\wxWidgets\regex_COPYRIGHT",
            "licenses\portaudio\LICENSE.txt",
            "licenses\readerwriterqueue\LICENSE.md",
            "licenses\concurrentqueue\LICENSE.md",
            "licenses\mingw-w64\GCC_RUNTIME_EXCEPTION_v3.1.txt",
            "licenses\mingw-w64\MINGW_W64_RUNTIME_NOTICES.txt"
        )

        $ArtifactHashes = [ordered]@{}
        foreach ($rel in $RequiredArtifacts) {
            $full = Join-Path $StageInstall $rel
            if (!(Test-Path -LiteralPath $full)) {
                throw "Required dependency artifact '$rel' is missing in staging installation!"
            }
            $hash = (Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash
            $ArtifactHashes[$rel] = $hash
        }

        # 12. Write build_completed.json inside StageInstall
        $MarkerData = [PSCustomObject]@{
            status        = "SUCCESS"
            cache_key     = $CacheKey
            build_date    = (Get-Date).ToString("yyyy-MM-ddTHH:mm:ssK")
            toolchain     = $ToolchainInfo
            dependencies  = $Lockfile.dependencies
            artifacts     = [PSCustomObject]@{
                count     = $RequiredArtifacts.Count
                libraries = @(
                    "lib\libportaudio.a",
                    "lib\gcc_x64_lib\libwxmsw32u_core.a",
                    "lib\gcc_x64_lib\libwxbase32u.a",
                    "lib\gcc_x64_lib\libwxpng.a",
                    "lib\gcc_x64_lib\libwxjpeg.a",
                    "lib\gcc_x64_lib\libwxtiff.a",
                    "lib\gcc_x64_lib\libwxzlib.a",
                    "lib\gcc_x64_lib\libwxregexu.a"
                )
                hashes    = $ArtifactHashes
            }
        }
        $MarkerJson = $MarkerData | ConvertTo-Json -Depth 10
        Set-Content -LiteralPath (Join-Path $StageInstall "build_completed.json") -Value $MarkerJson -Encoding UTF8

        # 13. Promote Staging to Install Atomically
        Write-Host "`nPromoting verified dependency staging to '$InstallDir'..." -ForegroundColor Cyan

        if (!(Test-Path -LiteralPath $InstallDir)) {
            New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
        }

        & robocopy.exe "$StageInstall" "$InstallDir" /MIR /R:3 /W:1 /NFL /NDL /NJH /NJS
        if ($LASTEXITCODE -ge 8) {
            throw "Failed to promote staged dependency installation to '$InstallDir' (robocopy exit code $LASTEXITCODE)."
        }

        Write-Host "======================================================================" -ForegroundColor Green
        Write-Host " DEPENDENCY BUILD SUCCESSFUL!" -ForegroundColor Green
        Write-Host "  Install root: $InstallDir" -ForegroundColor Green
        Write-Host "  Cache Key:    $CacheKey" -ForegroundColor Green
        Write-Host "  Artifacts:    $($RequiredArtifacts.Count) files verified" -ForegroundColor Green
        Write-Host "======================================================================" -ForegroundColor Green
    }
    finally {
        # Clean up staging root safely
        if (Test-Path -LiteralPath $StageRoot) {
            Remove-SafeDirectory -Path $StageRoot -AllowedParent $DepsDir -RequireOwnerMarker
        }
    }
}
