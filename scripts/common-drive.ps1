# Common Drive & Staging Safety Helpers for Data-Logger
# Enforces safe path validation, isolated staging, and guaranteed drive cleanup.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Assert-ValidPackageVersion {
    param([string]$Version)
    if ([string]::IsNullOrWhiteSpace($Version)) {
        throw "Version cannot be empty."
    }
    if ($Version -notmatch '^[0-9]+(\.[0-9]+)*$') {
        throw "Invalid version format '$Version'. Version must only contain digits and dots (e.g. 2.0.0), with no path separators or relative components."
    }
}

function Assert-SafeDirectoryPath {
    param(
        [string]$Path,
        [string]$AllowedParent = "",
        [switch]$RequireOwnerMarker
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Path cannot be null or empty."
    }

    $resolved = [System.IO.Path]::GetFullPath($Path)

    # 1. Reject drive roots and system roots
    $driveRoot = [System.IO.Path]::GetPathRoot($resolved)
    if ($resolved.TrimEnd('\', '/') -eq $driveRoot.TrimEnd('\', '/')) {
        throw "Security violation: Attempted operation on drive root '$resolved' is rejected."
    }

    # 2. Reject workspace root or any parent of workspace root
    $scriptRoot = (Resolve-Path "$PSScriptRoot\..").Path
    if ($resolved.TrimEnd('\', '/') -eq $scriptRoot.TrimEnd('\', '/')) {
        throw "Security violation: Attempted operation on workspace root '$resolved' is rejected."
    }

    # 3. Reject private directory or critical source directories
    $privateDir = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot "private"))
    $srcDir = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot "src"))
    $docsDir = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot "docs"))
    $testsDir = [System.IO.Path]::GetFullPath((Join-Path $scriptRoot "tests"))

    foreach ($forbidden in @($privateDir, $srcDir, $docsDir, $testsDir)) {
        if ($resolved.StartsWith($forbidden, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Security violation: Attempted operation inside protected directory '$forbidden' on target '$resolved' is rejected."
        }
    }

    # 4. If AllowedParent is specified, ensure target is strictly inside AllowedParent
    if (![string]::IsNullOrWhiteSpace($AllowedParent)) {
        $resolvedParent = [System.IO.Path]::GetFullPath($AllowedParent).TrimEnd('\', '/')
        if (!$resolved.StartsWith($resolvedParent + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Security violation: Target '$resolved' is not strictly inside allowed parent '$resolvedParent'."
        }
    }

    # 5. Check owner marker if required
    if ($RequireOwnerMarker -and (Test-Path -LiteralPath $resolved)) {
        $markerPath = Join-Path $resolved ".dlog_stage_owner"
        if (!(Test-Path -LiteralPath $markerPath)) {
            throw "Security violation: Target directory '$resolved' exists but does not contain '.dlog_stage_owner' marker. Refusing to modify/clean."
        }
    }

    return $resolved
}

function Remove-SafeDirectory {
    param(
        [string]$Path,
        [string]$AllowedParent = "",
        [switch]$RequireOwnerMarker
    )

    $validated = Assert-SafeDirectoryPath -Path $Path -AllowedParent $AllowedParent -RequireOwnerMarker:$RequireOwnerMarker
    if (Test-Path -LiteralPath $validated) {
        # Retry logic for Windows transient locks (cloud sync, indexer, explorer)
        for ($retry = 0; $retry -lt 10; ++$retry) {
            try {
                Remove-Item -LiteralPath $validated -Recurse -Force -ErrorAction Stop
                break
            } catch {
                Start-Sleep -Milliseconds 50
            }
        }
        if (Test-Path -LiteralPath $validated) {
            throw "Failed to remove directory '$validated' after retries."
        }
    }
}

function Invoke-WithSafeMappedDrive {
    param(
        [string]$TargetDirectory,
        [scriptblock]$Action
    )

    $resolvedTarget = [System.IO.Path]::GetFullPath($TargetDirectory)
    $workingRoot = $resolvedTarget
    $mappedDrive = $null
    $createdSubst = $false

    # Check if target contains non-ASCII characters or spaces that trouble MinGW/Ninja
    $needsMapping = ($resolvedTarget -match '[^\x00-\x7F]| ')

    if ($needsMapping) {
        $usedDrives = Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Name
        $substOutput = cmd.exe /c "subst"

        # Check if an existing subst drive already points to exactly this target
        foreach ($cand in @("X", "Y", "Z", "W", "V", "U", "T")) {
            if ($usedDrives -contains $cand) {
                if ($substOutput -match "(?i)$($cand):\\: => (.*)") {
                    $existingTarget = $Matches[1].Trim()
                    if ($existingTarget.TrimEnd('\') -eq $resolvedTarget.TrimEnd('\')) {
                        $mappedDrive = "$($cand):"
                        $workingRoot = "$($cand):\"
                        $createdSubst = $false
                        break
                    }
                }
            }
        }

        # If not already mapped, create a new subst mapping
        if (!$mappedDrive) {
            foreach ($cand in @("X", "Y", "Z", "W", "V", "U", "T")) {
                if ($usedDrives -notcontains $cand) {
                    $mappedDrive = "$($cand):"
                    cmd.exe /c "subst $mappedDrive `"$resolvedTarget`" >nul 2>&1"
                    if ($LASTEXITCODE -eq 0) {
                        $createdSubst = $true
                        $workingRoot = "$($cand):\"
                        break
                    }
                }
            }
            if (!$mappedDrive) {
                Write-Warning "Could not find a free drive letter for virtual drive mapping. Proceeding with original path."
                $workingRoot = $resolvedTarget
            }
        }
    }

    try {
        & $Action $workingRoot
    } finally {
        # ONLY unmap if THIS invocation explicitly created the subst
        if ($createdSubst -and $mappedDrive) {
            cmd.exe /c "subst $mappedDrive /d >nul 2>&1"
        }
    }
}

function Invoke-NinjaBuildWithRetry {
    param(
        [string]$Dir,
        [string[]]$Targets = @()
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

