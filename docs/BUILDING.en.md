[日本語](BUILDING.md)

# Builder Distribution and Clean Build

The Builder ZIP contains the project source code, unit/system tests, locked dependency definitions, and build/packaging scripts.
It does not contain third-party source trees, pre-built binary libraries, compilers, recorded test data, or `.git`.
An active internet connection is required on first run to fetch locked commits and submodules from official GitHub repositories.

## Prerequisites

- Windows 10 / 11 x64, PowerShell 5.1 or later
- Git for Windows, CMake 3.26+, Ninja 1.11+ in PATH
- GCC 13.2.0 x86_64 UCRT, MinGW-w64 11.0.1 (`gcc`, `g++`, `objdump` from the same toolchain)
- Several GB of free disk space (extracting outside cloud synchronization directories is recommended)

No global environment modifications or automatic tool installations are performed. If a different toolchain version is used, update `deps/runtime-notices.lock.json`, verification criteria, and distribution notices accordingly.

## Usage

```powershell
.\build.ps1 -Test
.\scripts\package.ps1
```

Executes dependency fetching, build, hash verification, application build, and CTest.
Standard test runs do not record from physical hardware. To include hardware recording tests, pass `-HardwareTests -Test`.
Only executables that match fully tested history can be packaged.
`-Offline` can only be used once all sources, submodules, and notices are cached locally.
`build.ps1 -Clean` archives the previous application build directory. `scripts/build-deps.ps1 -Clean` creates a new dependency build generation.
Does not recursively delete existing data or old dependency installs; maintenance of archived backups is the user's responsibility.

## Two Distribution Packages

```powershell
.\scripts\package-builder.ps1 -Version 2.0.0
.\scripts\package.ps1 -Version 2.0.0
```

- `DataLogger-v2.0.0-builder.zip`: For development and building. Contains SPEC and developer guidelines.
- `DataLogger-v2.0.0-win64.zip`: Executables, user documentation, license notices, and change history. Excludes SPEC and developer guidelines.

Halts without overwriting if a ZIP with the same name already exists. Specify a different `-OutputDir` or version string to regenerate.

## Distribution Builder ZIP Clean Verification

```powershell
.\scripts\test-clean-build.ps1 -BuilderZip .\dist\DataLogger-v2.0.0-builder.zip
```

Extracts to a clean, isolated directory on each run, performing download, build, test, and binary packaging without an existing dependency cache.
The resulting JSON summary, logs, and artifacts remain in that directory. Since this uses the host's existing OS and compiler, it differs from testing on a fresh Windows VM or different PC.
