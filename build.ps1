[CmdletBinding()]
param([switch]$Test, [switch]$Clean, [switch]$Offline, [switch]$HardwareTests,
      [switch]$UseInstalledDependencies)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/scripts/common-drive.ps1"
Invoke-WithSafeMappedDrive $PSScriptRoot {
    param($root)
    if (!$UseInstalledDependencies) {
        & "$PSScriptRoot/scripts/build-deps.ps1" -Offline:$Offline
    }
    $dep = Test-DependencyInstall "$root/.deps/install"
    $lock = Get-Content "$root/deps/dependencies.lock.json" -Raw -Encoding UTF8 | ConvertFrom-Json
    $usedDependencies = [ordered]@{}
    foreach ($entry in $lock.dependencies.PSObject.Properties) {
        $installed = $dep.dependencies.PSObject.Properties[$entry.Name]
        if (!$installed -or $installed.Value.commit -ne $entry.Value.commit -or
            $installed.Value.repository -ne $entry.Value.repository) {
            throw "Installed dependency does not match the lock: $($entry.Name). Run the dependency builder first."
        }
        $usedDependencies[$entry.Name] = $installed.Value
    }
    $build = Join-Path $root 'build'
    # A different SUBST letter invalidates CMake's absolute paths. Preserve the old tree.
    $relocate = $false
    if (Test-Path "$build/CMakeCache.txt") {
        $line = Get-Content "$build/CMakeCache.txt" | Where-Object { $_ -like 'CMAKE_HOME_DIRECTORY:INTERNAL=*' }
        $configured = ($line -replace '^CMAKE_HOME_DIRECTORY:INTERNAL=','').Replace('\','/').TrimEnd('/')
        $relocate = $configured -ne $root.Replace('\','/').TrimEnd('/')
    }
    if (($Clean -or $relocate) -and (Test-Path $build)) {
        $safe = Assert-SafeDirectoryPath $build $root
        Move-Item -LiteralPath $safe -Destination ($safe + '-backup-' + [guid]::NewGuid().ToString('N'))
    }
    $hardware = if ($HardwareTests) { 'ON' } else { 'OFF' }
    & cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release "-DDLOG_ENABLE_HARDWARE_TESTS=$hardware" `
        "-DCMAKE_C_COMPILER=$((Get-Command gcc).Source)" "-DCMAKE_CXX_COMPILER=$((Get-Command g++).Source)"
    if ($LASTEXITCODE -ne 0) { throw 'Application configure failed.' }
    Invoke-NinjaBuildWithRetry $build
    $argsList = @('--test-dir',$build,'--output-on-failure','--no-tests=error','--output-junit',"$build/test-results.xml")
    if (!$Test) { $argsList += @('-R','Smoke') }
    & ctest @argsList
    if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
    [xml]$results = Get-Content "$build/test-results.xml" -Raw
    $total = [int]$results.testsuite.tests
    $failed = [int]$results.testsuite.failures
    $skipped = [int]$results.testsuite.skipped
    if ($total -eq 0 -or $failed -ne 0) { throw 'Invalid test result.' }
    $sourceHashes = Get-BuilderSourceHashes $root
    $provenance = [ordered]@{
        schema=2; build_timestamp=[DateTime]::UtcNow.ToString('o'); toolchain=$dep.toolchain;
        dependencies=$usedDependencies; deps_cache_key=$dep.cache_key;
        dependency_marker_sha256=(Get-FileHash "$root/.deps/install/build_completed.json").Hash;
        source_hashes=$sourceHashes;
        executables=@(foreach($name in @('DataLogger.exe','DlogToCsv.exe')) {
            [ordered]@{name=$name; sha256=(Get-FileHash "$build/$name").Hash; size=(Get-Item "$build/$name").Length}
        });
        tests=[ordered]@{executed=$true; full_suite=[bool]$Test; hardware_enabled=[bool]$HardwareTests;
            total=$total; passed=($total-$failed-$skipped); skipped=$skipped}
    }
    $provenance | ConvertTo-Json -Depth 16 | Set-Content "$build/build_provenance.json" -Encoding UTF8
    Write-Host "Build and tests successful: $build"
}
