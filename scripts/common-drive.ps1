Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not ('DlogBuild.NativePath' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
namespace DlogBuild {
    public static class NativePath {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        static extern uint QueryDosDevice(string name, StringBuilder target, int length);
        public static string SubstTarget(string drive) {
            var buffer = new StringBuilder(32768);
            if (QueryDosDevice(drive, buffer, buffer.Capacity) == 0) return null;
            var path = buffer.ToString();
            return path.StartsWith(@"\??\") ? path.Substring(4) : null;
        }
    }
}
'@
}

function Assert-ValidPackageVersion {
    param([string]$Version)
    if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must be major.minor.patch.' }
}

function Get-PhysicalPath {
    param([string]$Path)
    $full = [IO.Path]::GetFullPath($Path)
    for ($depth=0; $depth -lt 16; $depth++) {
        if ($full -notmatch '^[A-Za-z]:\\') { break }
        $mappedTarget = [DlogBuild.NativePath]::SubstTarget($full.Substring(0,2))
        if (!$mappedTarget) { break }
        $full = Join-Path $mappedTarget $full.Substring(3)
        if ($depth -eq 15) { throw 'Excessive nested drive mappings.' }
    }
    # Do not traverse junctions/symlinks for managed file operations.
    $part = $full
    while ($part) {
        if (Test-Path -LiteralPath $part) {
            if ((Get-Item -LiteralPath $part -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Reparse-point path is not supported: $part"
            }
        }
        $parent = Split-Path $part -Parent
        if ($parent -eq $part) { break }
        $part = $parent
    }
    return [IO.Path]::GetFullPath($full)
}

function Assert-SafeDirectoryPath {
    param([string]$Path, [string]$AllowedParent, [switch]$RequireOwnerMarker)
    if (!$Path -or !$AllowedParent) { throw 'Explicit target and allowed parent required.' }
    $full = (Get-PhysicalPath $Path).TrimEnd('\','/')
    $parent = (Get-PhysicalPath $AllowedParent).TrimEnd('\','/')
    if (!$full.StartsWith($parent + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Target outside allowed parent.' }
    $root = (Get-PhysicalPath "$PSScriptRoot/..").TrimEnd('\','/')
    if ($root -eq $full -or $root.StartsWith($full + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Protected project root.' }
    foreach ($name in @('private','src','src_converter','tests','docs','deps','scripts','tools','3rdparty','.git')) {
        $protected = Join-Path $root $name
        if ($full -eq $protected -or $full.StartsWith($protected + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Protected project directory.' }
    }
    if ($RequireOwnerMarker -and (Test-Path $full) -and !(Test-Path "$full/.dlog_stage_owner")) { throw 'Missing ownership marker.' }
    return $full
}

function Invoke-WithSafeMappedDrive {
    param([string]$TargetDirectory, [scriptblock]$Action)
    $target = Get-PhysicalPath $TargetDirectory
    $root = $target; $created = $null
    if ($target -match '[^\x00-\x7F]| ') {
        foreach ($letter in @('X','Y','Z','W','V','U','T')) {
            $mappedTarget = [DlogBuild.NativePath]::SubstTarget("$($letter):")
            if ($mappedTarget -and $mappedTarget.TrimEnd('\') -eq $target.TrimEnd('\')) { $root = "$($letter):\"; break }
        }
        if ($root -eq $target) {
            foreach ($letter in @('X','Y','Z','W','V','U','T')) {
                if (!(Test-Path "$($letter):\")) {
                    & subst.exe "$($letter):" $target
                    if ($LASTEXITCODE -eq 0) { $created = "$($letter):"; $root = "$created\"; break }
                }
            }
            if (!$created) { throw 'No free drive for ASCII build path.' }
        }
    }
    try { & $Action $root }
    finally { if ($created) { & subst.exe $created /d } }
}

function Invoke-NinjaBuildWithRetry {
    param([string]$Dir, [string[]]$Targets = @())
    $argsList = @('--build',$Dir,'--parallel','4')
    if ($Targets.Count) { $argsList += @('--target') + $Targets }
    & cmake @argsList
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $Dir (inspect output; no blind retries)." }
}

function Test-DependencyInstall {
    param([string]$Directory)
    $meta = Get-Content -LiteralPath "$Directory/build_completed.json" -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($meta.schema -ne 2 -or $meta.status -ne 'SUCCESS') { throw 'Unsupported dependency marker.' }
    $required = @('lib/libportaudio.a','lib/gcc_x64_lib/libwxmsw32u_core.a','lib/gcc_x64_lib/libwxbase32u.a',
        'lib/gcc_x64_lib/libwxpng.a','lib/gcc_x64_lib/libwxjpeg.a','lib/gcc_x64_lib/libwxtiff.a',
        'lib/gcc_x64_lib/libwxzlib.a','lib/gcc_x64_lib/libwxregexu.a','lib/gcc_x64_lib/mswu/wx/setup.h',
        'include/wx/wx.h','include/readerwriterqueue.h','include/atomicops.h','include/portaudio.h',
        'licenses/runtime/gcc-runtime.txt','licenses/runtime/mingw-runtime.txt')
    foreach ($rel in $required) { if (!$meta.hashes.PSObject.Properties[$rel]) { throw "Missing artifact definition: $rel" } }
    $base = [IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
    foreach ($prop in $meta.hashes.PSObject.Properties) {
        $path = [IO.Path]::GetFullPath((Join-Path $base $prop.Name))
        if (!$path.StartsWith($base + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid artifact path.' }
        if (!(Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path).Hash -ne $prop.Value) {
            throw "Dependency artifact mismatch: $($prop.Name)"
        }
    }
    return $meta
}

function Get-BuilderFiles {
    param([string]$Root)
    # Explicit roots and extensions: never recurse through the entire workspace.
    $relative = @('CMakeLists.txt','CMakePresets.json','build.ps1','build.bat','README.md','SPEC.md','AGENTS.md',
        'LICENSE','LICENSES.md','.gitignore','tests/test_dlog.cpp','tests/test_converter.cpp','tests/test_build_scripts.ps1',
        'tests/fixtures/README.md','tests/fixtures/generate_fixtures.py','tools/read_dlog.py',
        'deps/dependencies.lock.json','deps/runtime-notices.lock.json',
        'docs/QUICKSTART.md','docs/REFERENCE_MANUAL.md','docs/BINARY_README.md','docs/BUILDING.md',
        'scripts/common-drive.ps1','scripts/build-deps.ps1','scripts/package.ps1','scripts/package-builder.ps1',
        'scripts/test-clean-build.ps1','scripts/test-packaging.ps1')
    foreach ($dir in @('src','src_converter')) {
        Get-ChildItem -LiteralPath (Join-Path $Root $dir) -File -Recurse | Where-Object { $_.Extension -in @('.cpp','.h') } | ForEach-Object {
            $relative += $_.FullName.Substring($Root.TrimEnd('\','/').Length).TrimStart('\','/').Replace('\','/')
        }
    }
    return @($relative | Sort-Object -Unique)
}

function Get-BuilderSourceHashes {
    param([string]$Root)
    $hashes = [ordered]@{}
    foreach ($rel in Get-BuilderFiles $Root) { $hashes[$rel] = (Get-FileHash -LiteralPath (Join-Path $Root $rel)).Hash }
    return $hashes
}

function Assert-MarkdownLinks {
    param([string]$Root)
    foreach ($file in Get-ChildItem -LiteralPath $Root -Recurse -File -Filter '*.md') {
        # Upstream notices remain byte-for-byte originals; only our user docs are checked here.
        if ($file.FullName.Substring($Root.Length).Replace('\','/') -match '^/licenses/') { continue }
        foreach ($match in [regex]::Matches((Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8), '\]\(([^)]+)\)')) {
            $link = $match.Groups[1].Value
            if ($link -match '^(https?://|#|mailto:)') { continue }
            $path = ($link -split '#')[0]
            if ($path -and !(Test-Path -LiteralPath (Join-Path $file.DirectoryName $path))) {
                throw "Broken packaged link in $($file.Name): $link"
            }
        }
    }
}

function Complete-Package {
    param([string]$Stage, [string]$Destination)
    # Never delete/overwrite a previous release. Use another version/output directory.
    if (Test-Path -LiteralPath $Destination) { throw "Package exists; preserved: $Destination" }
    $manifest = [Collections.Generic.List[string]]::new()
    foreach ($file in Get-ChildItem -LiteralPath $Stage -Recurse -File | Sort-Object FullName) {
        $rel = $file.FullName.Substring($Stage.Length).TrimStart('\','/').Replace('\','/')
        $manifest.Add("$((Get-FileHash -LiteralPath $file.FullName).Hash)  $rel")
    }
    $manifest | Set-Content -LiteralPath "$Stage/MANIFEST.txt" -Encoding UTF8
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $temporary = $Destination + '.' + [guid]::NewGuid().ToString('N') + '.tmp'
    [IO.Compression.ZipFile]::CreateFromDirectory($Stage,$temporary)
    Move-Item -LiteralPath $temporary -Destination $Destination
    Write-Host "Created $Destination"
    Write-Host "SHA256: $((Get-FileHash -LiteralPath $Destination).Hash)"
}
