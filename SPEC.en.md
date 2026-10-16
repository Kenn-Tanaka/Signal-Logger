[日本語](SPEC.md)

# Implementation Specification

Date of Reference: 2026-10-14. Target: Current C++ codebase, CMake configuration, and Python auxiliary tools.

## Purpose and Scope

An internal-use Windows application that captures, visualizes, and continuously logs up to 8 channels of analog signals using PC audio interfaces as the hardware front-end. Includes live non-recording preview, offline waveform playback, and batch CSV/TSV export.

Core Requirement: Prioritize zero dropouts, and display cumulative detected recording dropouts until the next recording starts. Unlimited storage stall absorption, certification as a calibrated lab meter, exact 1000.00 Hz rational resampling, >8 channels, and DirectSound/WDM-KS APIs are out of scope.

## Architecture

| Module | Responsibilities |
|---|---|
| `src/app.cpp`, `main.cpp` | `wxApp` startup and smoke test entry point |
| `src/ui/main_frame.*` | Input/record/playback controls, state management, status labels, metadata |
| `src/ui/waveform_panel.*` | Waveform ring buffer, Min/Max envelope rendering, level meters, time axis |
| `src/ui/time_axis_calculator.*` | Time window and division/scale calculations |
| `src/audio/audio_device_mgr.*` | PortAudio enumeration, format querying, stream lifecycle, Raw/UI SPSC queues |
| `src/dsp/decimator_processor.*` | Integer ratio decimation, FIR and moving average anti-aliasing |
| `src/storage/data_logger_worker.*` | Disk write queue, DLOG binary I/O, atomic commit and recovery tracking |
| `src/storage/power_manager.h` | Windows thread execution power management (sleep suppression) |
| `src/playback/*` | Time parsing, window requests, background chunk loading |
| `src_converter/*` | DLOG header validation, partial file recovery, conversion planning, CSV export, batch GUI |
| `tools/read_dlog.py` | Python memory-mapped inspection, metadata display, 16-bit WAV export |
| `tests/*` | Core logger and converter unit/integration test suites |

Built with C++17, wxWidgets 3.2.5, PortAudio v19.8-preview, and ReaderWriterQueue v1.0.6. Dependencies are locked to specific commit SHAs in `deps/dependencies.lock.json`, fetched and built via `scripts/build-deps.ps1` into `.deps/install/`. Completely statically linked. All real-world test data is isolated in `private/`.

## Signal Acquisition and Recording Pipeline

```text
PortAudio callback → Raw SPSC → DSP worker → Disk SPSC → Disk worker → DLOG
                                          └→ UI SPSC → wxTimer → Waveform
```

The real-time audio callback copies incoming samples into pre-allocated `AudioBlock` instances and invokes lock-free `try_enqueue`. Locks, dynamic memory allocation (`malloc`, `new`), file I/O, system calls, and logging are strictly prohibited in the callback thread.

Audio blocks default to 512 frames (max 1024 frames for 8ch). Queue capacities: Raw SPSC = 256 blocks, UI SPSC = 64 blocks, Disk SPSC = 512 blocks.

Preview mode runs the acquisition, DSP, and UI pipeline without engaging the disk worker. The UI timer fires at 30 ms intervals. When the UI queue is full, incoming blocks are discarded to prioritize acquisition and disk recording integrity.

Cumulative recording dropouts track PortAudio input overflows, Raw queue overflow, and Disk queue overflow. UI frame skips are counted separately. Recorded binary streams do not insert dummy samples or missing-segment markers; therefore, elapsed sample time on a recording with dropouts does not correspond strictly to continuous wall-clock time.

## Sampling Rates and DSP

Supported candidate native rates: 32000, 44100, 48000, 88200, 96000, 192000 Hz. Compatibility is queried dynamically via `Pa_IsFormatSupported`.

Recording rate is $F_s = \text{nativeRate} / N$. Supported decimation settings: $N = 1, 2, 4, 8$, and approximate target rates (~1000, 100, 10, 1 Hz) computed as $\max(1, \text{round}(\text{nativeRate} / \text{requestedRate}))$.

- $N = 1$: Direct pass-through without filtering.
- $1 < N \le 8$: When anti-aliasing is enabled, applies a 17-tap Hamming-windowed sinc FIR filter (normalized cutoff $0.45/N$, DC unity gain). When disabled, uses point sampling (zero filter latency and zero step ringing).
- $N > 8$: When anti-aliasing is enabled, applies a boxcar moving average filter maintaining state across buffer boundaries. When disabled, uses $N$-stride point sampling.

Display voltage is normalized PCM $\times$ full-scale voltage. Calibration calculates the shared full-scale voltage from the measured digital Peak-to-Peak on CH1 and the user's reference signal amplitude.

## State Management and UI

Live states: `Stopped`, `Previewing`, `Recording`. Transitioning from preview to recording stops and restarts the audio stream. Upon stopping recording, the application resumes preview only if preview was active before recording started; otherwise it returns to the `Stopped` state.

Display time windows range from 1 ms to 10 s across 10 major grid divisions. Shows center time, time scale, and channel levels. Waveform Hold freezes rendering while recording continues.

The dropout counter displayed on the recording status panel reflects events from the most recent recording and is retained until the next recording starts.

## DLOG v2 Binary Format

Extensions: `.bin` / `.dat`. Little-endian, IEEE 754 Float32 / Double. Fixed 64-byte header:

| Offset | Type | Field | Description |
|---|---|---|---|
| 0x00 | char[4] | magic | "DLOG" |
| 0x04 | uint32 | version | 2 |
| 0x08 | uint32 | header_size | 64 bytes |
| 0x0C | uint32 | channels | 1 to 8 |
| 0x10 | uint32 | sample_rate | Rounded display sample rate |
| 0x14 | uint32 | sample_format | 0 = Float32 |
| 0x18 | uint32 | native_rate | Hardware native sample rate |
| 0x1C | uint32 | decimation_factor | Integer decimation factor $N$ |
| 0x20 | int64 | start_timestamp_utc | Unix Epoch milliseconds |
| 0x28 | double | full_scale_voltage | Voltage for $\pm 1.0$ normalized PCM |
| 0x30 | uint8[16] | reserved | Zero-filled reserved space |
| 0x40~ | float32[] | pcm_data | Interleaved multi-channel PCM |

Frame $i$ offset: $64 + i \times \text{channels} \times 4$. Elapsed relative seconds: $i \times N / \text{native\_rate}$.

### Companion Metadata

Saved alongside as `filename.bin.meta`:

```ini
[DLOG_METADATA]
magic=DLOG
version=2
channels=2
start_timestamp_utc=1791510000000
full_scale_voltage=1
sample_rate=22050
native_rate=44100
decimation_factor=2
anti_aliasing=true

[CH1]
name=Signal1
unit=V

[CH2]
name=Signal2
unit=V
```

`DlogReader` validates magic, version, channel count, native rate, decimation factor, and timestamp against the binary header, safely falling back to defaults (`CH1`, `V`) if inconsistencies are detected.

### Temporary File Management & Recovery

During recording, data is written to `filename.bin.partial` (with branch suffix on collision). Upon clean termination, it is atomically renamed to the destination file. If recording aborts prematurely, the `.partial` file remains intact.

Partial recovery inspects the header, extracts all complete PCM frames into a recovered `.bin` file, and registers it for conversion without modifying the original `.partial` file.

## Playback

Provides random-access 64-bit frame seeking via `DlogReader` and background 65,536-frame chunk prefetching. Speeds: 0.25x to 16x.

## CSV / TSV Conversion

`ConversionPlan` verifies absence of collisions between output paths and inputs before conversion begins. Supports relative seconds, UTC timestamps, sample indices, line splitting, float precision (4/6/8 digits), and custom channel selections.

## WAV Export

Exports the verified DLOG file currently open in the "Recorded Waveform Playback" tab to a standard WAV or 64-bit RF64 file.

### Sampling Rates and Upsampling
- **Output Sample Rate**: Strictly the pre-decimation `native_rate` from the DLOG header.
- **Effective Input Rate**: `native_rate / decimation_factor` (unrounded exact quotient).
- **Interpolation Factor**: $N = \text{decimation\_factor}$.
- **Important Notice**: Interpolation does not restore lost frequency components or raw samples of the original analog signal.
- **Excluded Items**: Playback speed, display vertical gain, waveform hold state, Min/Max display compression, full-scale voltage conversion (normalized PCM values are exported directly), and auto-normalization are never applied to the WAV export.

### Interpolation Methods
1. **Zero-Order Hold (ZOH)**: Holds the current input sample value constant until the next input sample arrives.
2. **Linear Interpolation [Default]**: Connects adjacent input sample points with straight line segments.
3. **Bandlimited Interpolation (FIR)**:
   - **Architecture**: Polyphase windowed-sinc interpolation filter (Blackman window).
   - **Filter Length**: Semi-length $P = 4$ ($2P = 8$ taps per polyphase branch).
   - **Cutoff Frequency**: $f_c = 0.5 / N$.
   - **DC Gain**: Normalized so the sum of tap weights for each subphase branch equals 1.0 (guaranteeing 0 dB DC ripple).
   - **Sinc Zero-Crossing**: At subphase $s = 0$, $h_0[0] = 1.0$ and all other taps are $0.0$, reproducing raw samples bit-identically at input sample grid points.
   - **Boundary Handling**: Clamped endpoint padding (pre-margin $P-1 = 3$ frames, post-margin $P = 4$ frames).
   - **Delay Compensation**: Zero-phase centering ensures no filter group delay is introduced to output timestamps or frame counts.
   - **Bounded Memory**: Small-chunk streaming (max 4096 output frames/chunk) bounds memory footprint to $\le 500$ KB regardless of factor $N$.
   - **State Retention**: Retains per-channel and inter-chunk filter history across streaming blocks to prevent waveform discontinuities at chunk boundaries.
   - **Notice**: May introduce subtle ringing or overshoot near steep transition edges. Automatically disabled if decimation factor $N = 1$ (no interpolation required).

### Output Range and Time Axis
- **Entire File [Default]**: Exports the complete duration.
- **Specified Range**: Half-open interval $[start, end)$. Reuses `ParsePlaybackTimeString` and strictly maintains 64-bit frame arithmetic internally.

### Channel Selection and Layout
- Allows selecting 1 to 8 channels. Preserves channel order ascending by original channel index.
- Multi-channel WAV ($\ge 3$ channels) utilizes `WAVEFORMATEXTENSIBLE` with `dwChannelMask = 0` (unspecified / direct output) to prevent arbitrary surround speaker mapping.

### Formats and Word Length
- **IEEE 32-bit Float [Default]**: Normalized floating-point values in $[-1.0, +1.0]$.
- **16-bit Signed PCM**: Maps $[-1.0, +1.0] \to [-32768, +32767]$. Rejects non-finite values (NaN/Inf) as errors. Clips out-of-range samples and reports clipped sample counts upon completion.

### 4 GiB Size Boundary and RF64
- **4 GiB Standard WAV Limit Checkbox [Default: Checked]**: Not persisted; re-enables on each dialog invocation.
- Checked: Rejects exports exceeding 4 GiB.
- Unchecked: Automatically exports in 64-bit RF64 format (`ds64` chunk) when size reaches or exceeds 4 GiB.

### Safety and Isolation
- Writes streaming chunks to a temporary file (`.~*.tmp`) in the destination directory and performs atomic replacement upon completion.
- Cleans up temporary files safely upon error or user cancellation, preserving existing destination files.
- Operates on a dedicated worker thread with an isolated `DlogReader` instance, leaving GUI playback and seeking positions unaffected.

## Internationalization (Bilingual UI)

Both `DataLogger.exe` and `DlogToCsv.exe` support hot-switching between Japanese and English via the top menu bar (`言語 / Language` -> `● 日本語`, `○ English`).
- Switching language does not restart streams, reset buffers, or disrupt ongoing recording or conversions.
- User preference is saved atomically to `%LOCALAPPDATA%\DataLoggerSuite\settings.ini` (`schema_version=1`, `language=ja|en`).
- CLI option `--language=ja|en` overrides the UI language for the current session without writing to disk.
- Fallback logic checks Windows user default UI language (`ja` -> Japanese, else English).
- CSV output data, numbers, timestamps, and DLOG binary streams are 100% bitwise identical regardless of selected UI language. However, two independent physical measurement sessions on live hardware will have physical clock and scheduling jitter, and bitwise identity across separate recording runs is not guaranteed.
- Playback requests pin the reader, headers, and epoch to an immutable session object (PLAY-001).
- Companion v2 metadata strictly verifies all mandatory keys (`[DLOG_METADATA]` section, `magic`, `version`, `channels`, `native_rate`, `start_timestamp_utc`), rejecting and falling back to default channel naming if missing or inconsistent (DATA-001).
- Calibration is permitted only in Previewing and Stopped states, and is strictly disabled during active Recording to maintain consistent conversion factors (FUNC-001).

## Verification and Change Management

CTest registers `TestDataLogger`, `TestConverter`, `TestWavExport`, and bilingual startup smoke tests for both applications (`TestDataLoggerSmokeJa`, `TestDataLoggerSmokeEn`, `TestDlogToCsvSmokeJa`, `TestDlogToCsvSmokeEn`). Live audio capture tests are disabled by default (`DLOG_ENABLE_HARDWARE_TESTS=ON`, or `build.ps1 -HardwareTests`).

Binary packages contain executables, user guides, license notices, and provenance metadata (SPEC and developer guidelines are excluded from binary ZIP distributions). Source builder packages contain source code, dependency specifications, and developer documentation.

