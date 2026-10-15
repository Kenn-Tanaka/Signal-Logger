param([Parameter(Mandatory=$true)][string]$Directory, [ValidateSet('Binary','Builder')][string]$Kind)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$base = (Resolve-Path -LiteralPath $Directory).Path
$files = @(Get-ChildItem -LiteralPath $base -Recurse -File)
foreach ($file in $files) {
    $rel = $file.FullName.Substring($base.Length).TrimStart('\','/').Replace('\','/')
    if ($rel -match '(^|/)(private|3rdparty|\.deps|\.git|build|dist)/' -or $rel -match '\.(bin|dat|csv|tsv|wav|meta|partial|a|o|obj)$') { throw "Forbidden package file: $rel" }
    if ($Kind -eq 'Builder' -and $file.Extension -in @('.exe','.dll')) { throw 'Binary in builder package.' }
    if ($Kind -eq 'Binary' -and ($rel -match '(^|/)(SPEC|AGENTS)(\.[a-z]+)?\.md$' -or $file.Extension -in @('.cpp','.ps1'))) { throw 'Developer content in binary package.' }
}
$required = if ($Kind -eq 'Binary') {
    @('DataLogger.exe','DlogToCsv.exe','README.md','README.en.md','LICENSE','LICENSES.md','docs/QUICKSTART.md',
      'docs/QUICKSTART.en.md','docs/REFERENCE_MANUAL.md','docs/REFERENCE_MANUAL.en.md','build_provenance.json',
      'licenses/runtime/gcc-runtime.txt','licenses/runtime/mingw-runtime.txt')
} else { @('build.ps1','CMakeLists.txt','README.en.md','SPEC.en.md','docs/BUILDING.md','docs/BUILDING.en.md','deps/dependencies.lock.json','src/main.cpp','src_converter/main_converter.cpp') }
foreach ($rel in $required) { if (!(Test-Path -LiteralPath (Join-Path $base $rel))) { throw "Missing required file: $rel" } }
Write-Host "$Kind package audit passed ($($files.Count) files)."
