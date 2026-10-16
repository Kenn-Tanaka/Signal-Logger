[CmdletBinding()]
param([string]$Version='2.0.0', [string]$OutputDir='')
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/common-drive.ps1"
Assert-ValidPackageVersion $Version
$project = (Resolve-Path "$PSScriptRoot/..").Path
if (!$OutputDir) { $OutputDir = Join-Path $project 'dist' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
Invoke-WithSafeMappedDrive $project {
    param($root)
    $build = Join-Path $root 'build'
    $dep = Test-DependencyInstall "$root/.deps/install"
    $prov = Get-Content "$build/build_provenance.json" -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($prov.schema -ne 2 -or !$prov.tests.full_suite -or $prov.tests.total -lt 3) { throw 'Run build.ps1 -Test first.' }
    if ($prov.dependency_marker_sha256 -ne (Get-FileHash "$root/.deps/install/build_completed.json").Hash) { throw 'Dependency set changed since build.' }
    $current = Get-BuilderSourceHashes $root
    foreach ($rel in $current.Keys) {
        if (!$prov.source_hashes.PSObject.Properties[$rel] -or $prov.source_hashes.$rel -ne $current[$rel]) { throw "Source changed since build: $rel" }
    }
    foreach ($name in @('DataLogger.exe','DlogToCsv.exe')) {
        $entry = @($prov.executables | Where-Object { $_.name -eq $name })
        if ($entry.Count -ne 1 -or $entry[0].sha256 -ne (Get-FileHash "$build/$name").Hash) { throw "Executable changed: $name" }
        $imports = @(& objdump -p "$build/$name" | Select-String 'DLL Name:')
        if ($LASTEXITCODE -ne 0 -or !$imports.Count) { throw 'PE inspection failed.' }
        $allowed = '^(KERNEL32|USER32|GDI32|ADVAPI32|SHELL32|SHLWAPI|ole32|OLEAUT32|OLEACC|COMCTL32|comdlg32|UxTheme|VERSION|WINMM|ws2_32|setupapi|dsound|ucrtbase|api-ms-win-crt-[a-z0-9-]+)\.dll$|^WINSPOOL\.DRV$'
        foreach ($line in $imports) {
            $dll = ($line.Line -replace '^.*DLL Name:\s*','').Trim()
            if ($dll -notmatch $allowed) { throw "Unexpected DLL: $dll" }
        }
    }
    # Run executables by their current paths rather than stale SUBST paths in CTestTestfile.
    Push-Location $build
    try {
        & "$build/test_dlog.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Core tests failed.' }
        & "$build/test_converter.exe"
        if ($LASTEXITCODE -ne 0) { throw 'Converter tests failed.' }
        & "$build/test_wav_export.exe"
        if ($LASTEXITCODE -ne 0) { throw 'WAV export tests failed.' }
        $smoke = Start-Process -FilePath "$build/DataLogger.exe" -ArgumentList '--smoke-test' -PassThru -WindowStyle Hidden
        if (!$smoke.WaitForExit(15000)) { $smoke.Kill(); throw 'Smoke test timeout.' }
        if ($smoke.ExitCode -ne 0) { throw 'Smoke test failed.' }
    } finally { Pop-Location }
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    $stage = Join-Path $OutputDir ('.binary-stage-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory "$stage/docs" -Force | Out-Null
    try {
        foreach ($name in @('DataLogger.exe','DlogToCsv.exe','build_provenance.json')) { Copy-Item -LiteralPath "$build/$name" -Destination $stage }
        foreach ($name in @('LICENSE','LICENSES.md')) { Copy-Item -LiteralPath "$root/$name" -Destination $stage }
        Copy-Item -LiteralPath "$root/docs/BINARY_README.md" -Destination "$stage/README.md"
        Copy-Item -LiteralPath "$root/docs/BINARY_README.en.md" -Destination "$stage/README.en.md"
        foreach ($name in @('QUICKSTART.md','QUICKSTART.en.md','REFERENCE_MANUAL.md','REFERENCE_MANUAL.en.md')) { Copy-Item -LiteralPath "$root/docs/$name" -Destination "$stage/docs/" }
        Copy-Item -LiteralPath "$root/.deps/install/licenses" -Destination "$stage/licenses" -Recurse
        Assert-MarkdownLinks $stage
        & "$PSScriptRoot/test-packaging.ps1" -Directory $stage -Kind Binary
        Complete-Package $stage (Join-Path $OutputDir "DataLogger-v$Version-win64.zip")
    } finally {
        if (Test-Path -LiteralPath $stage) {
            $safeStage = Assert-SafeDirectoryPath $stage $OutputDir
            Remove-Item -LiteralPath $safeStage -Recurse -Force
        }
    }
}
