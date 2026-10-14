#!/usr/bin/env python3
"""
Generate deterministic synthetic DLOG fixtures for public testing.
This script uses pure mathematical equations (sine/cosine waves) to produce
fixtures without any real-world measurement data.
"""

import math
import struct
import os

def generate_synthetic_fixture(output_bin_path: str, output_meta_path: str):
    channels = 2
    sample_rate = 1000
    native_rate = 2000
    decimation_factor = 2
    duration_sec = 2.0
    total_frames = int(sample_rate * duration_sec)
    full_scale_voltage = 5.0
    start_timestamp_utc = 1700000000000  # Fixed synthetic timestamp

    # Fixed 64-byte DLOG v2 header
    # 0x00: char[4] magic = "DLOG"
    # 0x04: uint32 version = 2
    # 0x08: uint32 header_size = 64
    # 0x0C: uint32 channels = 2
    # 0x10: uint32 sample_rate = 1000
    # 0x14: uint32 sample_format = 0 (Float32)
    # 0x18: uint32 native_rate = 2000
    # 0x1C: uint32 decimation_factor = 2
    # 0x20: int64  start_timestamp_utc
    # 0x28: double full_scale_voltage = 5.0
    # 0x30: uint8[16] reserved = 0
    header = struct.pack(
        "<4sIIIIIIIqd16s",
        b"DLOG",
        2,
        64,
        channels,
        sample_rate,
        0,
        native_rate,
        decimation_factor,
        start_timestamp_utc,
        full_scale_voltage,
        b"\x00" * 16,
    )

    with open(output_bin_path, "wb") as f:
        f.write(header)
        for i in range(total_frames):
            t = float(i) / float(sample_rate)
            # Mathematical synthesis: Ch0 = 10Hz sine, Ch1 = 25Hz sine
            ch0 = math.sin(2.0 * math.pi * 10.0 * t) * 0.8
            ch1 = math.cos(2.0 * math.pi * 25.0 * t) * 0.5
            f.write(struct.pack("<ff", ch0, ch1))

    with open(output_meta_path, "w", encoding="utf-8") as f:
        f.write(
            f"[DLOG_METADATA]\n"
            f"magic=DLOG\n"
            f"version=2\n"
            f"channels={channels}\n"
            f"full_scale_voltage={full_scale_voltage:.6f}\n"
            f"sample_rate={sample_rate}\n"
            f"native_rate={native_rate}\n"
            f"decimation_factor={decimation_factor}\n"
            f"start_timestamp_utc={start_timestamp_utc}\n\n"
            f"[CH1]\nname=Synthetic_Sine_10Hz\nunit=V\n\n"
            f"[CH2]\nname=Synthetic_Cos_25Hz\nunit=V\n"
        )

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    bin_path = os.path.join(script_dir, "synthetic_2ch_1khz.bin")
    meta_path = os.path.join(script_dir, "synthetic_2ch_1khz.bin.meta")
    generate_synthetic_fixture(bin_path, meta_path)
    print(f"Generated synthetic fixture:\n  {bin_path}\n  {meta_path}")
