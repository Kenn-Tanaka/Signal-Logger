# Extract the actual Builder ZIP to a new directory, then build from empty dependency caches.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BuilderZip, [string]$WorkParent=$env:TEMP)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$zip = (Resolve-Path -LiteralPath $BuilderZip).Path
$run = Join-Path ([IO.Path]::GetFullPath($WorkParent)) ('dlog-clean-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $run | Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
    foreach ($entry in $archive.Entries) {
        $target = [IO.Path]::GetFullPath((Join-Path $run $entry.FullName))
        if (!$target.StartsWith($run + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe archive path.' }
    }
} finally { $archive.Dispose() }
[IO.Compression.ZipFile]::ExtractToDirectory($zip,$run)
foreach ($forbidden in @('.deps','build','3rdparty','private')) {
    if (Test-Path (Join-Path $run $forbidden)) { throw "Not a clean builder: $forbidden" }
}
foreach ($line in Get-Content -LiteralPath "$run/MANIFEST.txt" -Encoding UTF8) {
    if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') { throw 'Invalid builder manifest.' }
    $expected = $Matches[1]; $relative = $Matches[2]
    $target = [IO.Path]::GetFullPath((Join-Path $run $relative))
    if (!$target.StartsWith($run + '\', [StringComparison]::OrdinalIgnoreCase) -or (Get-FileHash -LiteralPath $target).Hash -ne $expected) {
        throw 'Builder manifest mismatch.'
    }
}
$watch = [Diagnostics.Stopwatch]::StartNew()
Write-Host "Clean build directory: $run"
Start-Transcript -Path "$run/clean-build.log" | Out-Null
try {
    & "$run/tests/test_build_scripts.ps1"
    & "$run/build.ps1" -Test
    & "$run/scripts/package.ps1" -OutputDir "$run/dist"
    $result = [ordered]@{ status='PASS'; builder_sha256=(Get-FileHash $zip).Hash;
        elapsed_seconds=$watch.Elapsed.TotalSeconds; scope='Fresh source/dependency/build caches; existing host toolchain and OS';
        hardware_capture=$false }
    $result | ConvertTo-Json | Set-Content "$run/clean-build-result.json" -Encoding UTF8
} catch {
    [ordered]@{ status='FAIL'; builder_sha256=(Get-FileHash $zip).Hash; elapsed_seconds=$watch.Elapsed.TotalSeconds;
        error=$_.Exception.Message } | ConvertTo-Json | Set-Content "$run/clean-build-result.json" -Encoding UTF8
    throw
} finally { Stop-Transcript | Out-Null; Write-Host "Logs and outputs retained at $run" }
