#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace dlog {

// 入力サンプリング周波数の候補値リスト（一元管理）
inline constexpr double CANDIDATE_SAMPLE_RATES[] = {
    32000.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0
};
inline constexpr size_t NUM_CANDIDATE_SAMPLE_RATES =
    sizeof(CANDIDATE_SAMPLE_RATES) / sizeof(CANDIDATE_SAMPLE_RATES[0]);

/**
 * @brief 利用可能なサンプリング周波数候補から、優先順位に従って選択値を決定する
 * 優先順位:
 * 1. 以前の選択値 (prevRate)
 * 2. デバイスのデフォルト周波数 (defaultRate)
 * 3. 44100 Hz
 * 4. 最初の候補
 */
inline double SelectAppropriateSampleRate(const std::vector<double>& availableRates,
                                         double prevRate,
                                         double defaultRate) {
    if (availableRates.empty()) {
        return 0.0;
    }

    auto matches = [](double a, double b) {
        return std::fabs(a - b) < 0.5;
    };

    // 1. 以前の選択値を可能なら維持
    if (prevRate > 0.0) {
        for (double r : availableRates) {
            if (matches(r, prevRate)) {
                return r;
            }
        }
    }

    // 2. デバイスの defaultSampleRate
    if (defaultRate > 0.0) {
        for (double r : availableRates) {
            if (matches(r, defaultRate)) {
                return r;
            }
        }
    }

    // 3. 44100 Hz
    for (double r : availableRates) {
        if (matches(r, 44100.0)) {
            return r;
        }
    }

    // 4. 最初の対応レート
    return availableRates.front();
}

// 保存サンプルのフォーマット
enum class SampleFormatType : uint32_t {
    Float32 = 0,
    Int16 = 1,
    Int24 = 2,
    Int32 = 3
};

// オーディオ入力デバイス情報
struct DeviceInfo {
    int id = -1;
    std::string name;
    int maxInputChannels = 0;
    double defaultSampleRate = 44100.0;
    std::string hostApiName;
};

// リアルタイムバッファ定数
constexpr size_t MAX_AUDIO_CHANNELS = 8;
constexpr size_t MAX_AUDIO_FRAMES = 1024;
constexpr uint32_t DLOG_FORMAT_VERSION = 2;
constexpr uint32_t DLOG_HEADER_SIZE = 64;

// オーディオブロック（リアルタイム安全な固定サイズバッファ）
struct AudioBlock {
    int channels = 0;
    size_t frameCount = 0;
    double sampleRate = 0.0;
    int64_t timestampUtcMs = 0;
    uint64_t firstOutputFrameIndex = 0; // 記録開始からの出力フレーム累積位置
    // インターリーブ形式の float PCM データ
    float samples[MAX_AUDIO_CHANNELS * MAX_AUDIO_FRAMES];
};

// 表示タイムウィンドウ（秒数）の候補一覧（一元管理）
struct TimeWindowCandidate {
    double seconds;
    const char* label;
};

inline constexpr TimeWindowCandidate TIME_WINDOW_CANDIDATES[] = {
    {0.001, "1 ms (100 us/div)"},
    {0.002, "2 ms (200 us/div)"},
    {0.005, "5 ms (500 us/div)"},
    {0.010, "10 ms (1 ms/div)"},
    {0.020, "20 ms (2 ms/div)"},
    {0.050, "50 ms (5 ms/div)"},
    {0.100, "100 ms (10 ms/div)"},
    {0.200, "200 ms (20 ms/div)"},
    {0.500, "500 ms (50 ms/div)"},
    {1.000, "1 s (100 ms/div)"},
    {2.000, "2 s (200 ms/div)"},
    {5.000, "5 s (500 ms/div)"},
    {10.000, "10 s (1 s/div)"}
};
inline constexpr size_t NUM_TIME_WINDOW_CANDIDATES =
    sizeof(TIME_WINDOW_CANDIDATES) / sizeof(TIME_WINDOW_CANDIDATES[0]);

// ファイル保存用ヘッダ仕様（固定 64 バイト）
#pragma pack(push, 1)
struct DLogFileHeader {
    char magic[4] = {'D', 'L', 'O', 'G'};  // マジックナンバー "DLOG"
    uint32_t version = DLOG_FORMAT_VERSION; // フォーマットバージョン
    uint32_t headerSize = DLOG_HEADER_SIZE; // ヘッダ長（バイト）
    uint32_t channels = 2;                 // チャネル数
    uint32_t sampleRate = 22050;           // 記録サンプリングレート (Hz)
    uint32_t sampleFormat = 0;             // 0: Float32（v2ではFloat32のみ保存）
    uint32_t nativeRate = 44100;           // ハードウェアネイティブレート (Hz)
    uint32_t decimationFactor = 2;         // デシメーション間引き係数 (例: 2 = 1/2間引き)
    int64_t startTimestampUtc = 0;         // 記録開始時刻 (UNIX Epoch ミリ秒)
    double fullScaleVoltage = 1.0;         // フルスケール電圧 [V] (デジタル ±1.0 = ±fullScaleVoltage V)
    uint8_t reserved[16] = {0};            // 将来拡張用予約領域 (残り16バイト)
};
#pragma pack(pop)
static_assert(sizeof(DLogFileHeader) == 64, "DLogFileHeader must be exactly 64 bytes");

inline double GetDLogEffectiveSampleRate(const DLogFileHeader& header) {
    if (header.nativeRate > 0 && header.decimationFactor > 0) {
        return static_cast<double>(header.nativeRate) /
               static_cast<double>(header.decimationFactor);
    }
    return static_cast<double>(header.sampleRate);
}

// チャネル名称・単位メタデータ
struct ChannelMetadata {
    std::string name;        // 例: "CH1", "V_bat", "歪みゲージ"
    std::string unit = "V";  // 例: "V", "mV", "A", "kPa"
};

// データロガー設定
struct LoggerConfig {
    int deviceId = -1;
    int channels = 2;
    double nativeSampleRate = 44100.0;
    int decimationFactor = 2;              // 1: そのまま, 2: 1/2間引き, 4: 1/4間引き等
    double customTargetSampleRate = 0.0;   // UIで選んだ近似目標値（実処理はdecimationFactorの整数1/N）
    double fullScaleVoltage = 1.0;         // フルスケール電圧 [V]
    std::vector<ChannelMetadata> channelMetadata; // 各チャネルの名称・単位設定
    std::string outputFilePath;
    unsigned long framesPerBuffer = 512;
    SampleFormatType sampleFormat = SampleFormatType::Float32;

    double getTargetSampleRate() const {
        if (decimationFactor > 0) {
            return nativeSampleRate / static_cast<double>(decimationFactor);
        }
        if (customTargetSampleRate > 0.0) {
            return customTargetSampleRate;
        }
        return nativeSampleRate;
    }
};

// リアルタイム稼働統計
struct LoggerStats {
    uint64_t totalFramesCaptured = 0;
    uint64_t totalFramesLogged = 0;
    uint64_t totalBytesWritten = 0;
    uint32_t bufferOverruns = 0;          // ファイルキュー溢れ（目標: 0）
    uint32_t uiFramesDropped = 0;         // UI描画間引き・破棄数
    double currentDurationSec = 0.0;
    bool isLogging = false;
    bool isStreaming = false;
    bool hasDiskError = false;
};

} // namespace dlog
