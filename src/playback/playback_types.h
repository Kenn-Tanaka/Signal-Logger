#pragma once

#include "audio/audio_types.h"
#include <vector>
#include <string>
#include <cstdint>

namespace dlog {

// 再生要求パラメータ (GUI -> ワーカースレッド)
struct PlaybackRequest {
    uint64_t generation = 0;       // 世代番号 (シークやファイル切替で破棄)
    double centerTimeSec = 0.0;    // 要求中心時刻 (秒)
    double timeWindowSec = 2.0;    // 表示時間幅 (秒)
    int pixelWidth = 1000;         // 画面ピクセル幅 (Min/Max集約用)
};

// 再生用ウィンドウ描画データ (ワーカースレッド -> GUI)
struct PlaybackWindowData {
    uint64_t generation = 0;       // 生成元の世代番号
    double centerTimeSec = 0.0;    // このデータに対応する中心時刻 (秒)
    double timeWindowSec = 2.0;    // 表示時間幅 (秒)
    double sampleRate = 0.0;       // 実効サンプリング周波数 (Hz)
    uint64_t totalFrames = 0;      // ファイルの総フレーム数
    int channels = 0;              // チャネル数
    double fullScaleVoltage = 1.0; // フルスケール電圧 (V)

    // 有効データ区間（秒）
    double validLeftTime = 0.0;
    double validRightTime = 0.0;
    int64_t startFrame = 0;
    size_t frameCount = 0;

    // 集約Min/Maxモード (高密度時)
    bool isAggregated = false;
    int numColumns = 0;
    std::vector<std::vector<float>> minPerChannel; // [ch][col]
    std::vector<std::vector<float>> maxPerChannel; // [ch][col]

    // Rawサンプルモード (低密度・高拡大時)
    std::vector<std::vector<float>> rawSamples;    // [ch][sample]

    // 可視範囲内の各チャネル Peak 絶対値 (0.0〜1.0)
    std::vector<float> peakAbsPerChannel;

    bool isLoading = false;
    bool hasError = false;
    std::string errorMessage;
};

} // namespace dlog
