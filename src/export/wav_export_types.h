#pragma once

#include "common/localized_strings.h"
#include <cstdint>
#include <string>
#include <vector>
#include <cstddef>

namespace dlog {

/**
 * @brief WAVエクスポート時の補間方法
 */
enum class InterpolationMethod {
    ZeroOrderHold = 0,    // ゼロ次ホールド: 入力サンプルを次の入力まで保持
    Linear = 1,           // 線形補間 [既定]: 隣接サンプルを直線接続
    BandlimitedFir = 2    // 帯域制限補間 (FIR): ポリフェーズ窓付きsinc補間
};

/**
 * @brief WAVエクスポート時の出力サンプル形式
 */
enum class WavSampleFormat {
    IeeeFloat32 = 0,      // IEEE 32-bit Float [既定]
    Pcm16 = 1             // 16-bit Signed Integer PCM
};

/**
 * @brief WAVコンテナ形式
 */
enum class WavContainerFormat {
    StandardWav = 0,      // 標準 RIFF/WAVE (4 GiB未満)
    Rf64 = 1              // EBU/ITU-R RF64 (大容量・64-bitサイズ対応)
};

/**
 * @brief WAVエクスポート設定オプション
 */
struct WavExportOptions {
    std::string sourceDlogPath;          // 入力DLOGファイルパス
    std::string destinationWavPath;      // 出力WAVファイルパス

    bool exportEntireFile = true;        // ファイル全体を出力するか
    uint64_t startFrame = 0;             // 開始フレーム (inclusive, 0-indexed)
    uint64_t endFrame = 0;               // 終了フレーム (exclusive)

    std::vector<int> selectedChannels;   // 出力対象チャネル一覧 (0-indexed, 入力順序維持)
    bool explicitChannelSelection = false; // true のとき selectedChannels が空ならエラーとする
    WavSampleFormat sampleFormat = WavSampleFormat::IeeeFloat32;
    InterpolationMethod interpolationMethod = InterpolationMethod::Linear;

    bool limitTo4GiB = true;             // 4 GiB未満制限チェック (初期値: true, 永続化しない)
};

/**
 * @brief エクスポート事前計算・見積もり情報
 */
struct WavExportEstimate {
    uint32_t nativeRate = 0;             // デシメーション前周波数 (Hz)
    uint32_t decimationFactor = 1;       // 間引き率 N
    double effectiveInputRate = 0.0;     // DLOG実効記録周波数 nativeRate / decimationFactor
    uint32_t outputRate = 0;             // 出力周波数 (= nativeRate)
    uint32_t interpolationFactor = 1;    // 補間倍率 (= decimationFactor)

    uint64_t inputFrames = 0;            // 選択された入力フレーム数 M
    uint64_t outputFrames = 0;           // 出力フレーム数 M * N
    double outputDurationSec = 0.0;      // 出力時間長 (秒)
    uint32_t channelCount = 0;           // 出力チャネル数
    size_t bytesPerSample = 4;           // サンプルあたりのバイト数 (Float32=4, PCM16=2)

    uint64_t estimatedDataBytes = 0;     // dataチャンクのバイト数
    uint64_t estimatedTotalBytes = 0;    // 全ヘッダ・チャンクを含む総ファイルサイズ
    WavContainerFormat containerFormat = WavContainerFormat::StandardWav;

    bool sizeCalculationOverflow = false;// 計算時の整数オーバーフロー有無
    bool exceeds4GiBLimit = false;       // 4 GiB制限超過フラグ
    bool isValid = false;                // 設定全体の有効性
    std::string validationError;         // 検証エラーメッセージ (英語/ログ用)
    LocalizedError errorInfo;            // 構造化ローカライズエラー情報
};

/**
 * @brief エクスポート実行結果
 */
struct WavExportResult {
    bool success = false;
    bool canceled = false;
    WavContainerFormat containerFormat = WavContainerFormat::StandardWav;
    std::string destinationPath;
    uint64_t outputFramesWritten = 0;
    uint64_t totalBytesWritten = 0;
    uint64_t pcm16ClippedSamples = 0;    // PCM16変換時に飽和・クリップしたサンプル数
};

} // namespace dlog
