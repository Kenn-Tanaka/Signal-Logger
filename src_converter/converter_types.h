#pragma once

#include "audio/audio_types.h"
#include "common/localized_strings.h"
#include <string>
#include <vector>
#include <cstdint>

namespace dlog {

// タイムスタンプ出力形式
enum class TimeFormat {
    RelativeSec,    // 相対時間 [秒] (例: 0.000000, 0.000045, ...)
    SampleIndex,    // サンプル番号 [整数] (例: 0, 1, 2, ...)
    AbsoluteTime,   // UTC絶対日時 (例: 2026-10-09 10:30:00.123Z)
    None            // 時間列なし (チャネルデータのみ)
};

// 変換オプション構造体
struct ConversionOptions {
    TimeFormat timeFormat = TimeFormat::RelativeSec;
    std::vector<int> selectedChannels;     // 出力対象チャネル一覧 (0-indexed, 空なら全チャネル)
    int decimationStep = 1;                // 間引きステップ (1 = 全サンプル, 2 = 1/2間引き, 10 = 1/10間引き)
    char delimiter = ',';                  // 区切り文字 (',' または '\t')
    int floatPrecision = 6;                // 小数点以下の出力桁数
    
    // 行数自動分割オプション（ユーザー指定行数）
    bool enableSplitLines = true;
    uint64_t maxLinesPerFile = 1000000;    // 1ファイルあたりの最大データ行数 (デフォルト100万行)
    
    // 時間範囲トリミング
    bool enableTimeRange = false;
    double startTimeSec = 0.0;
    double endTimeSec = 0.0;

    // 電圧換算オプション
    bool applyVoltageScale = true;         // ヘッダに記録されたフルスケール電圧を適用して [V] 単位で出力
    double customFullScaleVoltage = 0.0;   // 0.0 の場合はファイルヘッダの fullScaleVoltage を使用。> 0.0 で上書き指定可能

    // チャネル名称・単位設定 (FUNC-003)
    std::vector<ChannelMetadata> channelMetadata;

    // 出力先
    bool useSameDirAsInput = true;
    std::string customOutputDir;
};

// バッチ項目の処理ステータス
enum class BatchStatus {
    Pending,     // 待機中
    Converting,  // 変換中
    Completed,   // 完了
    Canceled,    // 中断
    Error        // エラー
};

// バッチ項目情報
struct BatchItem {
    int id = 0;
    std::string inputFilePath;
    std::string fileNameOnly;
    std::string outputDir;
    
    // DLOG ヘッダ情報
    DLogFileHeader header;
    std::vector<ChannelMetadata> channelMetadata; // コンパニオンメタデータ (.meta)
    uint64_t totalFrames = 0;
    double durationSec = 0.0;
    uint64_t fileSizeBytes = 0;
    bool isValidHeader = false;
    
    // 変換結果
    BatchStatus status = BatchStatus::Pending;
    double progress = 0.0;                 // 0.0 〜 1.0
    uint64_t convertedFrames = 0;
    std::vector<std::string> outputFiles;  // 分割出力されたファイル一覧
    LocalizedError error;
    std::string errorMessage;
};

// バッチ全体の進捗統計
struct BatchStats {
    size_t totalFiles = 0;
    size_t completedFiles = 0;
    size_t currentFileIndex = 0;
    double currentFileProgress = 0.0; // 0.0 〜 1.0
    double overallProgress = 0.0;     // 0.0 〜 1.0
    double bytesPerSec = 0.0;         // 処理速度
    double estimatedRemainingSec = 0.0;
    bool isRunning = false;
};

// 共通の変換出力計画 (DATA-002)
struct ConversionPlan {
    std::string baseOutputDir;
    std::string baseFileName;
    std::string extension; // ".csv" または ".tsv"
    uint64_t startFrame = 0;
    uint64_t endFrame = 0;
    uint64_t targetFrames = 0;
    uint64_t estimatedOutputRows = 0;
    bool willSplit = false;
    uint64_t totalParts = 1;
    int branchDigits = 2;
    std::vector<std::string> plannedOutputFiles;
};

/**
 * @brief バッチ項目とオプションから一元化された出力計画を算出 (DATA-002)
 */
ConversionPlan PlanConversion(const BatchItem& item, const ConversionOptions& options);

} // namespace dlog

