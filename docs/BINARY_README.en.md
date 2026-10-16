[日本語](README.md)

# Data-Logger Suite — Windows Binary Release

An up to 8-channel signal logger for Windows 10 / 11 (64-bit).

- `DataLogger.exe`: Non-recording preview, DLOG recording, scaled waveform playback, and WAV export
- `DlogToCsv.exe`: CSV/TSV batch conversion, time range trimming, line splitting, and stopped partial file recovery

For installation and operation, refer to the [Quickstart Guide](docs/QUICKSTART.en.md) and [Reference Manual](docs/REFERENCE_MANUAL.en.md).
For licensing terms, see [LICENSE](LICENSE) and [Third-Party Notices](LICENSES.md); license texts are located in `licenses/`.

No installer required. Simply extract the ZIP archive and keep the executables, documentation, and notices together in the folder structure.
Audio interface device drivers and standard Windows system DLLs/UCRT are required. Uses MME or WASAPI.
Select a writable local drive for recording, and regularly back up your recorded data.
This software provides no hardware overvoltage protection. Strictly observe your audio interface input ratings.

SPEC, developer guidelines, source code, build scripts, and recorded data are not included in this binary distribution.
To rebuild from source, use the separately distributed Builder ZIP. Python offline tools are also provided in the Builder ZIP.
Zero dropouts cannot be unconditionally guaranteed during prolonged recording or storage I/O stalls.
