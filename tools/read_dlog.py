#!/usr/bin/env python3
"""
Data-Logger Binary File Reader & Exporter (.bin / .dat)
"""

import sys
import os
import struct
import argparse
import math
import numpy as np

MAX_CHANNELS = 8
HEADER_SIZE = 64
SUPPORTED_VERSIONS = {1, 2}

def read_dlog_file(filepath):
    with open(filepath, "rb") as f:
        # ヘッダ読み込み (64 bytes)
        header_bytes = f.read(HEADER_SIZE)
        if len(header_bytes) < HEADER_SIZE:
            raise ValueError("ファイルサイズが小さすぎます（ヘッダが不正）")

        magic, version, header_size, channels, sample_rate, sample_format, native_rate, decimation, timestamp = \
            struct.unpack("<4sIIIIIIIq", header_bytes[:40])
        full_scale_voltage = struct.unpack("<d", header_bytes[40:48])[0]
        if full_scale_voltage <= 0.0 or not math.isfinite(full_scale_voltage):
            full_scale_voltage = 1.0

        magic_str = magic.decode("ascii", errors="replace")
        if magic_str != "DLOG":
            raise ValueError(f"無効なマジックナンバー: {magic_str}")
        if version not in SUPPORTED_VERSIONS:
            raise ValueError(f"未対応のDLOGバージョン: {version}")
        if header_size != HEADER_SIZE:
            raise ValueError(f"無効なヘッダサイズ: {header_size}")
        if not 1 <= channels <= MAX_CHANNELS:
            raise ValueError(f"チャネル数が1～{MAX_CHANNELS}の範囲外です: {channels}")
        if sample_format != 0:
            raise ValueError(f"未対応のサンプル形式です: {sample_format} (Float32のみ対応)")
        if sample_rate == 0 or native_rate == 0 or decimation == 0:
            raise ValueError("サンプルレートまたはデシメーション係数が不正です")

        effective_rate = native_rate / decimation
        if not math.isfinite(effective_rate) or effective_rate <= 0:
            raise ValueError("実効サンプルレートが不正です")

        print("=== Data-Logger ファイル情報 ===")
        print(f"ファイル: {filepath}")
        print(f"マジック: {magic_str} (バージョン: {version})")
        print(f"チャネル数: {channels}")
        print(f"サンプリングレート: {effective_rate:.9g} Hz (ヘッダ表示値: {sample_rate} Hz, 元レート: {native_rate} Hz, デシメーション: 1/{decimation})")
        print(f"フルスケール電圧: ±{full_scale_voltage:.4f} V")
        print(f"フォーマット: {'Float32' if sample_format == 0 else 'Unknown'}")
        print(f"開始タイムスタンプ: {timestamp}")

        file_size = os.fstat(f.fileno()).st_size
        data_bytes = file_size - HEADER_SIZE
        bytes_per_frame = channels * np.dtype("<f4").itemsize
        if data_bytes % bytes_per_frame != 0:
            raise ValueError("データ部の末尾がフレーム境界に揃っていません")
        frames = data_bytes // bytes_per_frame

        print(f"総フレーム数: {frames} ({frames / effective_rate:.2f} 秒)")

        # 大容量ログを全件RAMへ読み込まず、必要な範囲だけOSがページングする。
        data = (np.memmap(filepath, dtype="<f4", mode="r", offset=HEADER_SIZE,
                          shape=(frames, channels))
                if frames > 0 else np.empty((0, channels), dtype=np.float32))

        return {
            "channels": channels,
            "sample_rate": effective_rate,
            "native_rate": native_rate,
            "decimation": decimation,
            "timestamp": timestamp,
            "data": data
        }

def export_wav(dlog_data, out_wav):
    import wave
    data = dlog_data["data"]
    channels = dlog_data["channels"]
    sample_rate = dlog_data["sample_rate"]

    with wave.open(out_wav, "wb") as wf:
        wf.setnchannels(channels)
        wf.setsampwidth(2)
        wf.setframerate(round(sample_rate))
        chunk_frames = 65536
        for start in range(0, data.shape[0], chunk_frames):
            chunk = np.asarray(data[start:start + chunk_frames])
            int16_data = np.clip(chunk * 32767.0, -32768, 32767).astype("<i2")
            wf.writeframesraw(int16_data.tobytes())

    print(f"WAVファイルへ出力完了: {out_wav}")

def main():
    parser = argparse.ArgumentParser(description="Data-Logger バイナリファイル解析・エクスポートツール")
    parser.add_argument("file", help="入力 .bin または .dat ファイルパス")
    parser.add_argument("--info", action="store_true", help="ヘッダと記録情報を表示（既定動作）")
    parser.add_argument("--wav", metavar="OUTPUT.wav", help="16-bit PCM WAVとして書き出すパス")
    args = parser.parse_args()

    if not os.path.exists(args.file):
        print(f"エラー: ファイルが存在しません: {args.file}")
        sys.exit(1)

    info = read_dlog_file(args.file)
    if args.wav:
        export_wav(info, args.wav)

if __name__ == "__main__":
    main()
