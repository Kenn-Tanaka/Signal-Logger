# Non-destructive checks for path safety and allowlist packaging.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/../scripts/common-drive.ps1"
$root = (Resolve-Path "$PSScriptRoot/..").Path
$checks = 0
foreach ($version in @('../escape','1/2','1.0.0/../x','')) {
    $rejected = $false
    try { Assert-ValidPackageVersion $version } catch { $rejected = $true }
    if (!$rejected) { throw 'Invalid package version accepted.' }
    $checks++
}
Invoke-WithSafeMappedDrive $root {
    param($mapped)
    foreach ($name in @('src','src_converter','private','tests','docs','scripts','deps')) {
        $rejected = $false
        try { Assert-SafeDirectoryPath (Join-Path $mapped $name) $mapped | Out-Null } catch { $rejected = $true }
        if (!$rejected) { throw "Protected directory accepted: $name" }
    }
    Invoke-WithSafeMappedDrive $root { param($nested); if (!(Test-Path $nested)) { throw 'Nested mapping failed.' } }
    if (!(Test-Path $mapped)) { throw 'Nested invocation removed mapping.' }
}
$checks += 8
$files = Get-BuilderFiles $root
if (@($files | Where-Object { $_ -match '(^|/)(private|3rdparty|\.deps|build|dist)/|\.(bin|dat|csv|tsv|wav|meta|exe|a)$' }).Count) {
    throw 'Forbidden content in builder allowlist.'
}
$checks++
Write-Host "Build script regression checks passed: $checks"
