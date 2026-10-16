[日本語](README.md)

# Data-Logger Suite

An up to 8-channel signal logger for Windows. Using PC audio interfaces as the input front-end, it provides real-time waveform display, DLOG binary recording, waveform playback of recorded data, and CSV/TSV batch conversion.

---

## Documentation

- [Quickstart Guide](docs/QUICKSTART.en.md): Initial input check, recording, playback, and conversion
- [Reference Manual](docs/REFERENCE_MANUAL.en.md): Settings, operation, calibration, and troubleshooting
- [Specification](SPEC.en.md): Architecture, processing method, DLOG format, and constraints
- [Licenses and Third-Party Notices](LICENSES.md): Third-party software licenses and exception clauses
- [Agent Guidelines](AGENTS.md): Implementation constraints and thread design
- [Builder Distribution & Clean Build](docs/BUILDING.en.md): Prerequisites, package differences, verification steps

---

## Architecture and Features

| Component | Purpose |
|---|---|
| `DataLogger.exe` | Non-recording preview, up to 8ch recording, time-axis waveform display, data playback, upsampled WAV export |
| `DlogToCsv.exe` | Multi-file DLOG to CSV/TSV conversion, range trimming, row splitting, partial file recovery |
| `tools/read_dlog.py` | Header verification, 16-bit PCM WAV export (requires Python and NumPy) |

- **Candidate Input Rates**: 32 / 44.1 / 48 / 88.2 / 96 / 192 kHz (dynamically filtered by querying PortAudio for supported device/channel formats)
- **Recording Rates**: Integer submultiples of native rate (decimation + anti-aliasing filter)
- **Playback Feature**: Seek and visualize waveforms directly from DLOG binary files without audio output (speed: 0.25x to 16x)
- **WAV Export**: Export from the Playback tab at the pre-decimation native rate as standard WAV / RF64 (Linear / FIR / ZOH interpolation, Float32 / PCM16, 4 GiB limit / RF64 auto-selection)
- **Data Protection**: Writes to a temporary file (`.partial`) in the same directory during recording, atomically replacing on clean stop

---

## System Requirements

- **OS**: Windows 10 / 11 (64-bit)
- **Audio API**: MME or WASAPI (via PortAudio)
- **Runtime**: Completely statically linked (`-static`, `-static-libgcc`, `-static-libstdc++`). No external DLLs required (uses only standard Windows system DLLs and UCRT)

---

## Build, Test, and Packaging Instructions

### Required Toolchain
- C++17 compatible compiler: GCC 13.2.0 / MinGW-w64 11.0.1 (UCRT x86_64)
- CMake 3.26 or later
- Ninja 1.11 or later
- PowerShell 5.1 or later / Git for Windows

### 1. Automated Dependency Build (from official repositories)
Dependency definitions and commit SHAs are locked in `deps/dependencies.lock.json`.

```powershell
.\scripts\build-deps.ps1
```
* On first run, downloads official revisions and installs static libraries/headers to `.deps/install/`. Subsequent runs use cached binaries (`-Offline` option supported).

### 2. Application Build and Automated Testing
```powershell
.\build.ps1 -Test
```
(or `build.bat -Test`)

- Executables are generated in the `build/` directory (`build/DataLogger.exe`, `build/DlogToCsv.exe`).
- CTest executes test suites (core logger logic, CSV converter, and GUI smoke test).
- Real hardware audio capture tests are skipped by default. Specify `-HardwareTests -Test` when required.

### 3. Release Packaging
```powershell
.\scripts\package.ps1
.\scripts\package-builder.ps1
```

- Automatic verification that all tests pass
- Static link verification (verifies no non-system DLL dependencies exist in PE headers)
- Binary package contains EXEs, user docs, notices, and change history (excludes SPEC and internal developer guidelines)
- Automatically generates `MANIFEST.txt` with locked dependency revisions and SHA-256 hashes
- Generates `dist/DataLogger-v<Version>-win64.zip`
- Source builder package `dist/DataLogger-v<Version>-builder.zip` contains source code, build scripts, and docs (excludes compiled binaries and dependencies)
- Prevents overwriting existing archives of the same version

Builder ZIP verification can be run via `scripts/test-clean-build.ps1 -BuilderZip <PathToZip>`.

---

## Policy on Measured Data and Privacy

- **Measured Data Confidentiality**: Real recorded data (`.bin`, `.dat`, `.meta`, `.partial`, CSV files) is isolated in `private/` and strictly excluded from Git tracking and public distribution archives.
- **Public Test Data**: Only synthetic test signals in `tests/fixtures/` (mathematically generated sine waves) are included.
- **Privacy Protection**: Inclusion of measured values, channel labels, timestamps, or personal information in commit logs or reports is strictly prohibited.

---

## License

Source code and documentation specific to this project are licensed under the **MIT License** (see [LICENSE](LICENSE)).
Copyright Notice: `Copyright (c) 2026 Kenichiro Tanaka`

For third-party library licenses (wxWidgets, PortAudio, readerwriterqueue, concurrentqueue, GCC runtime), see [LICENSES.md](LICENSES.md) and individual license files.
