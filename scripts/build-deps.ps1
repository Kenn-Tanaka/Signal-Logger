# Official-source dependency builder. No vendor fallback or destructive checkout.
[CmdletBinding()]
param([switch]$Offline, [switch]$Force, [switch]$Clean)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common-drive.ps1"
$projectRoot = (Resolve-Path "$PSScriptRoot/..").Path
Invoke-WithSafeMappedDrive $projectRoot {
    param($root)
    $deps = Join-Path $root '.deps'
    $lockPath = Join-Path $root 'deps/dependencies.lock.json'
    $lock = Get-Content $lockPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $runtimePath = Join-Path $root 'deps/runtime-notices.lock.json'
    $runtime = Get-Content $runtimePath -Raw -Encoding UTF8 | ConvertFrom-Json
    foreach ($tool in @('git','cmake','ninja','gcc','g++')) { Get-Command $tool -ErrorAction Stop | Out-Null }
    $gcc = (Get-Command gcc).Source; $gxx = (Get-Command g++).Source
    $version = (& $gxx -dumpfullversion).Trim(); $target = (& $gxx -dumpmachine).Trim()
    if ($version -ne $runtime.gcc_version -or $target -ne 'x86_64-w64-mingw32') {
        throw 'Use GCC 13.2.0 x86_64 MinGW-w64 UCRT; update runtime notices before changing toolchains.'
    }
    $macros = ('' | & $gcc -dM -E -include _mingw.h -) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $macros -notmatch '#define _UCRT\b' -or
        $macros -notmatch '#define __MINGW64_VERSION_MAJOR 11\b' -or
        $macros -notmatch '#define __MINGW64_VERSION_MINOR 0\b' -or
        $macros -notmatch '#define __MINGW64_VERSION_BUGFIX 1\b') { throw 'MinGW-w64 11.0.1 UCRT headers required.' }
    $toolchain = [ordered]@{ compiler=((& $gxx --version) | Select-Object -First 1); target=$target;
        cmake=((& cmake --version) | Select-Object -First 1); ninja=(& ninja --version).Trim() }
    $sha = [Security.Cryptography.SHA256]::Create()
    $keyText = (Get-FileHash $lockPath).Hash + (Get-FileHash $runtimePath).Hash +
        (Get-FileHash "$PSScriptRoot/build-deps.ps1").Hash + (Get-FileHash $gcc).Hash +
        (Get-FileHash $gxx).Hash + ($toolchain | ConvertTo-Json -Compress)
    $key = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($keyText))).Replace('-','')
    $sha.Dispose()
    $install = Join-Path $deps 'install'
    if (!$Force -and !$Clean -and (Test-Path "$install/build_completed.json")) {
        try {
            $existing = Test-DependencyInstall $install
            if ($existing.cache_key -eq $key) { Write-Host 'Verified dependency cache hit.'; return }
        } catch { Write-Warning 'Dependency cache invalid; rebuilding in a new generation.' }
    }
    New-Item -ItemType Directory -Path $deps -Force | Out-Null
    $generation = Join-Path $deps ('work-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $generation | Out-Null
    $stage = Join-Path $generation 'install'
    foreach ($sub in @('include','lib/gcc_x64_lib/mswu/wx','licenses')) {
        New-Item -ItemType Directory -Path (Join-Path $stage $sub) -Force | Out-Null
    }
    $sources = @{}; $observed = [ordered]@{}
    foreach ($prop in $lock.dependencies.PSObject.Properties) {
        $name = $prop.Name; $cfg = $prop.Value
        $src = Join-Path $deps "sources/$name-$($cfg.commit)"
        if (!(Test-Path $src)) {
            if ($Offline) { throw "Offline source missing: $name" }
            New-Item -ItemType Directory -Path $src -Force | Out-Null
            & git -C $src init --quiet
            if ($LASTEXITCODE -ne 0) { throw 'git init failed' }
            & git -C $src remote add origin $cfg.repository
            if ($LASTEXITCODE -ne 0) { throw 'git remote failed' }
            & git -C $src fetch --depth 1 origin $cfg.commit
            if ($LASTEXITCODE -ne 0) { throw "Fetch failed: $name" }
            & git -C $src -c advice.detachedHead=false checkout --detach FETCH_HEAD
            if ($LASTEXITCODE -ne 0) { throw "Checkout failed: $name" }
        }
        if (!(Test-Path "$src/.git")) { throw "Unverifiable source: $name" }
        $head = (& git -C $src rev-parse HEAD).Trim()
        if ($LASTEXITCODE -ne 0 -or $head -ne $cfg.commit) { throw "Revision mismatch: $name" }
        $dirty = @(& git -C $src status --porcelain --untracked-files=all)
        if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw "Modified source cache: $name (preserved)" }
        if ($cfg.submodules -and !$Offline) {
            & git -C $src submodule update --init --recursive --depth 1
            if ($LASTEXITCODE -ne 0) { throw "Submodule fetch failed: $name" }
        }
        $subs = @(& git -C $src submodule status --recursive)
        if ($LASTEXITCODE -ne 0 -or @($subs | Where-Object { $_ -match '^[-+U]' }).Count) { throw "Submodule mismatch: $name" }
        $dirty = @(& git -C $src status --porcelain --untracked-files=all)
        if ($LASTEXITCODE -ne 0 -or $dirty.Count) { throw "Dirty submodule: $name" }
        $sources[$name] = $src
        $observed[$name] = [ordered]@{ repository=$cfg.repository; commit=$head; submodules=$subs }
    }
    foreach ($name in @('portaudio','wxWidgets')) {
        $build = Join-Path $generation $name
        $options = @('-S',$sources[$name],'-B',$build,'-G','Ninja',"-DCMAKE_C_COMPILER=$gcc","-DCMAKE_CXX_COMPILER=$gxx")
        $options += $lock.dependencies.$name.cmake_options
        & cmake @options
        if ($LASTEXITCODE -ne 0) { throw "Configure failed: $name" }
        $targets = if ($name -eq 'portaudio') { @('portaudio') } else { @('wxcore','wxbase') }
        Invoke-NinjaBuildWithRetry -Dir $build -Targets $targets
    }
    $paLib = Join-Path $generation 'portaudio/libportaudio.a'
    if (!(Test-Path $paLib)) { $paLib = Join-Path $generation 'portaudio/libportaudio_static.a' }
    Copy-Item -LiteralPath $paLib -Destination "$stage/lib/libportaudio.a"
    Copy-Item -LiteralPath "$($sources.portaudio)/include/portaudio.h" -Destination "$stage/include/portaudio.h"
    Get-ChildItem "$generation/wxWidgets/lib" -Recurse -Filter '*.a' | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination "$stage/lib/gcc_x64_lib/$($_.Name)"
    }
    # wx configures both release/debug headers even for a Release-only build.
    $setup = "$generation/wxWidgets/lib/gcc_x64_lib/mswu/wx/setup.h"
    if (!(Test-Path -LiteralPath $setup)) { throw 'Release wx setup.h missing.' }
    Copy-Item -LiteralPath $setup -Destination "$stage/lib/gcc_x64_lib/mswu/wx/setup.h"
    Copy-Item -LiteralPath "$($sources.wxWidgets)/include/wx" -Destination "$stage/include/wx" -Recurse
    foreach ($name in @('readerwriterqueue','concurrentqueue')) {
        foreach ($header in $lock.dependencies.$name.output_artifacts.headers) {
            Copy-Item -LiteralPath "$($sources[$name])/$header" -Destination "$stage/include/$header"
        }
    }
    foreach ($name in $sources.Keys) {
        New-Item -ItemType Directory "$stage/licenses/$name" -Force | Out-Null
        Copy-Item -LiteralPath (Join-Path $sources[$name] $lock.dependencies.$name.license.relative_path) -Destination "$stage/licenses/$name/"
    }
    foreach ($rel in @('docs/lgpl.txt','src/png/LICENSE','src/jpeg/README',
        'src/tiff/COPYRIGHT','src/zlib/README','3rdparty/pcre/LICENCE','3rdparty/nanosvg/LICENSE.txt')) {
        Copy-Item -LiteralPath (Join-Path $sources.wxWidgets $rel) -Destination "$stage/licenses/wxWidgets/$($rel.Replace('/','_'))"
    }
    foreach ($name in @('readerwriterqueue','concurrentqueue')) {
        Get-ChildItem -LiteralPath $sources[$name] -Filter '*.h' | ForEach-Object {
            Copy-Item -LiteralPath $_.FullName -Destination "$stage/licenses/$name/"
        }
    }
    New-Item -ItemType Directory "$deps/notices" -Force | Out-Null
    New-Item -ItemType Directory "$stage/licenses/runtime" -Force | Out-Null
    foreach ($notice in $runtime.notices) {
        $cached = Join-Path $deps "notices/$($notice.file)"
        if (!(Test-Path $cached)) {
            if ($Offline) { throw "Offline runtime notice missing: $($notice.file)" }
            [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
            Invoke-WebRequest -UseBasicParsing -Uri $notice.url -OutFile $cached
        }
        if ((Get-FileHash $cached).Hash -ne $notice.sha256) { throw 'Runtime notice hash mismatch' }
        Copy-Item -LiteralPath $cached -Destination "$stage/licenses/runtime/"
    }
    $hashes = [ordered]@{}
    foreach ($file in Get-ChildItem -LiteralPath $stage -File -Recurse) {
        $rel = $file.FullName.Substring($stage.Length).TrimStart('\','/').Replace('\','/')
        $hashes[$rel] = (Get-FileHash -LiteralPath $file.FullName).Hash
    }
    $marker = [ordered]@{ schema=2; status='SUCCESS'; cache_key=$key; toolchain=$toolchain;
        dependencies=$observed; lock_sha256=(Get-FileHash $lockPath).Hash;
        runtime_lock_sha256=(Get-FileHash $runtimePath).Hash; hashes=$hashes }
    $marker | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath "$stage/build_completed.json" -Encoding UTF8
    Test-DependencyInstall $stage | Out-Null
    $backup = Join-Path $deps ('install-backup-' + [guid]::NewGuid().ToString('N'))
    $hadOld = Test-Path $install
    if ($hadOld) { Move-Item -LiteralPath $install -Destination $backup }
    try { Move-Item -LiteralPath $stage -Destination $install }
    catch {
        if ($hadOld -and !(Test-Path $install)) { Move-Item -LiteralPath $backup -Destination $install }
        throw
    }
    Write-Host 'Dependency build completed; sources, build logs and previous install retained.'
}
