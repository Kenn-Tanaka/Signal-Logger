[日本語](QUICKSTART.md)

# Quickstart Guide

Target Audience: First-time users of Data-Logger Suite. For detailed instructions, refer to the [Reference Manual](REFERENCE_MANUAL.en.md). For operating requirements, see [README](../README.en.md).

## 1. Preparation

1. Ensure your audio interface is recognized by Windows and appropriate drivers are installed.
2. Connect signals that remain within the input voltage limits of your hardware. Setting a full-scale voltage does not prevent hardware overvoltage.
3. Verify sufficient free disk space on the target drive. First test with non-critical signals for a short duration.
4. Launch `DataLogger.exe`.

Note: With AC-coupled audio interfaces, DC components are blocked. Choosing a lower recording rate does not enable DC measurement on AC-coupled hardware.

## 2. Previewing Waveforms Without Recording

1. In the "Live Input" tab, select your input device and number of channels.
2. Select the "Native Rate". 32 kHz will only appear if supported by the device.
3. Select the decimation factor and confirm the resulting effective recording rate.
4. Click "Start Preview". Data is not recorded to disk during preview.
5. Adjust display parameters: Time Window, Vertical Gain, and Waveform Hold. Note that Hold only pauses visual rendering; it does not stop streaming.

If controls are disabled, stop the preview first before changing hardware parameters.

## 3. Recording Signals

1. If desired, configure channel names, units, and full-scale voltage. Note: modifying unit names is a display label and does not convert physical values. To calibrate voltage scale, click "Calibrate...". An interactive dialog opens in Previewing or Stopped state (with automatic prompt to start preview if stopped), allowing you to verify live signal metrics (measured Vpp, peak level, clipping detection) in real time before applying (calibration cannot be changed during active recording).
2. Under "Target File", specify a path ending in `.bin` or `.dat`.
3. Click "Start Recording". Pay attention to overwrite warnings if specifying an existing file.
4. Monitor dropout count and file status during recording. UI rendering skips are distinct from audio recording dropouts.
5. Click "Stop Recording" to finish, verifying completion status or errors.
6. Keep the generated DLOG file together with its companion `filename.bin.meta` file.

After recording stops, the app may return to preview mode. Do not assume the app is recording simply because waveforms are moving. For critical measurements, record the dropout count, path, and any errors. You can toggle between Japanese and English via the "言語 / Language" menu at any time, even during recording or conversion (language selection does not alter stored formats or settings).

## 4. Viewing Recorded Data

1. Stop any active recording or preview, and switch to the "Recorded Data Playback" tab.
2. Click "Open File" and select a completed DLOG file. Incomplete `.partial` files cannot be loaded here directly.
3. Enter a time position in seconds (e.g. `12.5`) or formatted timestamp (`00:00:12.500`) to jump directly.
4. Use Play/Pause, Start, End, or the navigation slider.
5. Select a playback speed. Playback speed only modifies the rendering rate; it does not alter saved binary data.

If rendering feels delayed over long files, pause playback and choose a shorter time window.

## 5. Converting to CSV / TSV

1. Launch `DlogToCsv.exe`.
2. Click "Add Files..." or drag and drop DLOG files into the list.
3. Choose the output directory, timestamp format, channels, delimiter, and float precision.
4. If needed, configure time range trimming and row splitting (e.g. 1,000,000 data rows for Excel limits).
5. Click "Start Conversion" and monitor per-file status and progress.
6. Click "Open Output Folder" to inspect the resulting text files.

Re-running conversion overwrites existing output files. Move existing CSVs if you need to preserve them. Converter decimation is simple point sampling, distinct from the LPF anti-aliasing decimation applied during recording.

## 6. Recovering from Unexpected Terminations

1. Ensure the recording process has fully exited, and make a backup copy of the `.partial` file.
2. In `DlogToCsv.exe`, click "Recover Partial File..." and select the incomplete file.
3. Once the recovered file appears in the list, verify its duration, waveform, and channel metadata.
4. Do not delete the original `.partial` file until verification is complete.

Recovery extracts complete valid frames from the incomplete stream. Data unwritten to disk or lost during power cuts cannot be recovered. For details, see [Troubleshooting](REFERENCE_MANUAL.en.md#troubleshooting).
