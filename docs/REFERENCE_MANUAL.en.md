[日本語](REFERENCE_MANUAL.md)

# Reference Manual

Target Audience: Users of DataLogger, DlogToCsv, and the auxiliary Python tool. System architecture specifications and developer rules are not included in binary packages; refer to source/builder distributions. The auxiliary Python tool is also provided in the builder distribution.

## Terminology and Measurement Notes

- **Native Rate**: Sampling rate requested from PortAudio (frames per second). Each frame contains 1 sample for each active channel.
- **Recording Rate**: Native rate divided by an integer decimation factor $N$.
- **Preview**: Continuous acquisition and visual display only. No data is saved to DLOG files.
- **Recording**: Continuous data logging written to local storage.
- **Playback**: Visual review of waveforms from completed DLOG files without audio speaker output.
- **Dropout Count**: Cumulative count of detected overflow/underflow events (not the raw number of lost samples or duration).
- **Full-Scale Voltage**: Voltage corresponding to normalized PCM $\pm 1.0$. This is a software scaling factor and does not alter the hardware's physical input range.

Check your hardware interface coupling and maximum input ratings. AC-coupling (DC blocking) cannot be reversed in software. Setting custom unit labels such as A or kPa does not automatically scale sensor sensitivities or calibrate offsets.

## Live Input Configuration

| Item | Operation & Meaning |
|---|---|
| Input Device / Refresh | Lists available audio inputs. Stop preview/recording before refreshing if a device was reconnected. |
| Channels | Number of channels, up to device maximum and capped at 8 channels. |
| Channel Settings | Configure custom names and units for waveforms and companion metadata. |
| Native Rate | Select from 32, 44.1, 48, 88.2, 96, 192 kHz that successfully pass PortAudio format query. |
| Decimation | 1/1, 1/2, 1/4, 1/8, or approximate target rates (~1000, 100, 10, 1 Hz). |
| Anti-Aliasing | When checked, applies FIR / moving average LPF. When unchecked, uses point sampling (disabled on 1/1). |
| Full-Scale Voltage | Positive float value. Used to scale display and CSV conversion. |
| Output File Path | Final DLOG file path. Temporary recording files are created in the same folder. |
| Start/Stop Preview | Starts/stops acquisition without saving. |
| Start/Stop Recording | Starts/stops binary file recording. |

Format querying tests compatibility for the combination of device, channel count, API, and Float32 format. It does not eliminate internal driver rate conversions. Arbitrary rates cannot be entered manually.

Target rates like ~1000 Hz round to an integer decimation factor $N$. At 44.1 kHz, $N=44$ yields ~1002.27 Hz; at 32 kHz, $N=32$ yields 1000 Hz. Fractional/rational resampling to exact 1000.00 Hz is not performed.

The Anti-Aliasing checkbox enables the digital low-pass filter (17-tap FIR for $N \le 8$, boxcar average for $N > 8$) to eliminate Nyquist foldover noise. Uncheck it if you wish to eliminate filter delay and step-response ringing (Gibbs phenomenon) on pulses, or if you prefer pure point sampling. It is automatically disabled (greyed out) at 1/1 full rate. The choice is recorded in the companion `.meta` file as `anti_aliasing=true` or `false`.

### Voltage Calibration

1. Connect a known AC reference signal within hardware ratings to an input channel.
2. Click "Calibrate...". If stopped, a confirmation prompt appears to automatically start preview and begin live signal monitoring (opens immediately if already previewing).
3. In the calibration dialog, select the target channel (CH 1 to CH N) receiving the reference signal.
4. Enter the known Peak-to-Peak voltage [Vpp] (maximum minus minimum) of the reference signal (default: 1.000 Vpp).
5. Inspect the Live Signal Monitor group: verify measured digital Vpp, peak amplitude, and signal health badge (🟢 Good / Stable, 🟡 Low Signal, 🔴 Clipping Warning, ⚪ No Signal). If clipping, reduce the hardware interface input gain knob.
6. Check the calculated full-scale voltage (±V_FS) and click "Apply" to save.
7. If physical input gain knobs on the audio interface are modified later, recalibration is required.

Calibration (the "Calibrate..." button and manual Full Scale Voltage entry) is strictly enabled only in Previewing or Stopped states. During active Recording, calibration is completely disabled to prevent changing conversion scale factors mid-recording. Do not enter RMS or single-ended peak values as Vpp. This does not perform per-channel independent calibration or frequency response equalization.

## Waveform Display & Time Axis

Available time windows are 1, 2, 5, 10, 20, 50, 100, 200, 500 ms, and 1, 2, 5, 10 s. The horizontal axis has 10 major grid divisions; 1 division is 1/10th of the display window (e.g. 2 ms window = 200 $\mu$s/div).

Grid labels are relative to the center of the display (0.000 s). Combine with the displayed "Center Time". Do not confuse time relative to screen center with time from recording start. In preview, time is measured from preview start.

Vertical gain is a visual zoom factor; it does not modify recorded PCM or hardware gain. Peak, voltage, dB, and clipping indicators are for visual aid and not a certified substitute for calibrated lab instrumentation.

Waveform Hold only freezes screen rendering; background recording continues uninterrupted. For playback, use Pause. Large time windows and high channel counts require more display rendering calculations.

## Recording & Data Safety

During recording, data is written to an adjacent `.partial` temporary file (with branch numbers on collision), atomically renaming to the final file on clean stop. Move and archive the DLOG file and its companion `.meta` together.

The UI dropout counter aggregates PortAudio input overflows, raw queue drops, and disk write queue drops. UI rendering skips are tracked on a separate counter. Zero dropouts does not guarantee the absence of physical sensor signal anomalies.

Estimated required disk space: `4 × channels × recording_rate × seconds + 64` bytes. At 192 kHz, 8 channels, 1 hour, without decimation, this is approx. 22.1 GB. Ensure sufficient disk space.

On stopping, the app waits for queues to flush and files to finalize. Do not force-kill the process. Power management sleep-suppression (`SetThreadExecutionState`) prevents automatic Windows sleep, but does not prevent power outages or forced shutdowns.

## Recorded Data Playback

- Open a DLOG file, and navigate using Start, End, the slider, or by typing a time.
- Time input accepts seconds (`12.5`), MM:SS, or HH:MM:SS with fractional seconds. Positions align to sample boundaries.
- The last sample position is `(total_frames - 1) / recording_rate`. This differs by 1 sample from the total duration `total_frames / recording_rate`.
- Playback speeds: 0.25x, 0.5x, 1x, 2x, 4x, 8x, 16x. Does not alter original files.
- Areas where the time window extends outside the recorded range are drawn empty.
- High playback speeds on slow drives may skip frames. Use Pause for exact inspection.
- Audio output, waveform editing, and Undo/Redo are not supported.

### WAV Export Feature

Click the "Export WAV..." button in the Playback tab to export the opened DLOG recording as a standard WAV or 64-bit RF64 audio file.

| Setting | Details & Specifications |
|---|---|
| Output Sampling Rate | Always the pre-decimation `native_rate` from the DLOG header. Interpolation factor $N = \text{decimation\_factor}$. Does not restore lost frequency components or original analog samples |
| Interpolation Method | **Zero-Order Hold**: Holds sample value constant until next point<br>**Linear [Default]**: Connects adjacent points with straight line segments<br>**Bandlimited (FIR)**: 8-tap Blackman-windowed sinc polyphase filter. 1.0 DC gain normalized, sinc zero-crossing bit-exact reproduction at sample points, clamped boundary padding, zero-phase centering. May introduce subtle ringing on steep edges (disabled when decimation factor is 1) |
| Output Range | **Entire File [Default]** or **Specified Range [start, end)**. Strictly managed internally with 64-bit frame arithmetic |
| Channels | Select 1 to 8 channels (maintains ascending order). Multi-channel files ($\ge 3$ ch) set `dwChannelMask = 0` (direct output) to prevent arbitrary surround speaker mapping |
| Output Format | **IEEE 32-bit Float [Default]** or **16-bit Signed PCM** (no auto-normalization; maps $[-1.0, +1.0]$ directly to $[-32768, +32767]$, clips out-of-range samples and reports counts upon completion; rejects non-finite NaN/Inf) |
| 4 GiB Size Boundary | **Enforce 4 GiB Limit [Default: Checked]**. When unchecked, automatically outputs in 64-bit RF64 format (`ds64` chunk) if size exceeds 4 GiB (resets to checked on next dialog opening) |
| Safety & Atomicity | Streams into a temporary file (`.~*.tmp`) in the destination folder, atomically renamed on completion. Incomplete files are safely removed on cancel/error |

* Playback speed, display vertical gain, waveform hold, Min/Max compression, full-scale voltage scaling, and auto-normalization are never applied to exported WAV files.


## CSV / TSV Conversion

| Option | Description |
|---|---|
| Input Registration | Add Files, Add Folder, Drag & Drop |
| Remove / Clear | Removes files from the batch list; does not delete source files |
| Output Directory | Same folder as input, or a specified custom directory |
| Time Format | Relative seconds, UTC absolute timestamp, sample index, or None |
| Decimation | 1/1, 1/2, 1/4, 1/5, 1/10, 1/20, 1/50, 1/100 |
| Delimiter | Comma (`.csv`) or Tab (`.tsv`) |
| Float Precision | 4, 6, or 8 decimal digits. Does not increase underlying hardware precision |
| Voltage Scaling | Multiplies PCM by full-scale voltage. If unchecked, outputs normalized PCM |
| Channels | All channels or selected channels. Channel names and units can be customized |
| Time Range | Start time and end time in seconds. End = 0 means to end of file |
| Line Splitting | Splits output every $N$ data rows, adding column headers to each part |
| Cancel | Requests cancellation. Already completed part files are preserved |

Converter decimation is simple point sampling without additional digital anti-aliasing filtering. Relative time is based on the source DLOG frame position and is not reset to 0 at the start of a trimmed range. High sampling rates with millisecond-formatted absolute time may show duplicate timestamps on consecutive rows.

Split files are named `name-01.csv`, etc., with zero-padded branch digits. Without splitting, output is `name.csv`. Re-converting files with the same name overwrites existing files. If re-converting with different splitting options, old split files may remain; using an empty output folder is recommended.

Output planning includes collision detection, but do not target the same output folder from multiple processes simultaneously. Cancellation does not roll back already completed conversions.

## Troubleshooting

| Symptom | Cause & Action |
|---|---|
| App does not launch | Check EXE location, error message, OS version, and run `--smoke-test` to inspect exit code. |
| No input devices | Check USB connection, drivers, Windows microphone privacy permissions, and click refresh. |
| Input rate list empty | Change channel count or device. Check if another application has exclusive audio lock. |
| DC offset not visible | Audio interface is AC-coupled. Changing sampling rate will not preserve DC. |
| Waveform clipping | Decrease input gain on audio interface hardware. Software vertical gain cannot fix input clipping. |
| Increasing dropouts | High CPU load or slow drive. Reduce sampling rate, change disk, and record dropouts in measurement log. |
| Disk / commit error | Keep both temporary `.partial` and destination path intact. Check free disk space and permissions. |
| Playback laggy | Pause playback, reduce time window width, or copy file to a fast local SSD. |
| Wrong names/units | Ensure DLOG and `.meta` files are kept together in the same directory. |

### Recovering Incomplete Files

The converter's "Recover Partial File (.partial)..." extracts all complete PCM frames into a new `.bin` file and registers it in the batch conversion list. The original `.partial` file is preserved untouched.

Do not run recovery while a recording process is still running. Always create a backup of the original `.partial` first. Ensure sufficient disk space for the recovered file. Incomplete trailing frame bytes are truncated, and data dropped prior to disk write cannot be recovered. Always inspect the recovered file in DlogReader or the playback tab after recovery.

## Auxiliary Python Tool

Requires Python 3 and NumPy. Recommended setup in a virtual environment:

```powershell
python -m venv .venv
.\.venv\Scripts\python -m pip install numpy
.\.venv\Scripts\python tools/read_dlog.py recording.bin --info
.\.venv\Scripts\python tools/read_dlog.py recording.bin --wav output.wav
```

Provides header inspection and 16-bit PCM WAV export only (no plotting UI). WAV export quantizes float samples to 16-bit PCM and rounds non-integer sampling rates to integers. Use the DLOG binary file for precision engineering analysis.

## Settings & Best Practices

UI language preferences (`言語 / Language` menu) are stored per-user in `%LOCALAPPDATA%\DataLoggerSuite\settings.ini` and can also be overridden via the `--language=ja|en` CLI option. Do not rely on persistent hardware settings; verify device, channel count, rate, and path before each recording session. For portable deployment, keep the executables, documentation, and licenses together in the same folder structure.

## Language Selection & Data Format Independence

- Toggling the UI language (Japanese / English) via the "言語 / Language" menu strictly affects visual text only. It does not alter the stored DLOG binary format, companion metadata (`.meta`), or the numeric formatting, delimiter characters, and floating-point representations of CSV/TSV outputs.
- Running CSV conversion on the same DLOG file under Japanese and English UI produces 100% bitwise-identical output files.
- However, two independent live measurement sessions captured separately on physical hardware will have slight hardware clock jitter and OS scheduling timing differences, and are never guaranteed to produce bitwise-identical data regardless of language.

