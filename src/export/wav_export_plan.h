#pragma once

#include "wav_export_types.h"
#include "audio/audio_types.h"
#include <string>

namespace dlog {

class WavExportPlan {
public:
    static const uint64_t STANDARD_WAV_LIMIT_BYTES = 4294967296ULL; // 4 GiB (2^32)

    /**
     * @brief 時刻区間 [timeStart, timeEnd) をフレーム区間 [startFrame, endFrame) に安全変換
     */
    static bool CalculateFrameRange(double timeStart, double timeEnd, uint64_t totalFrames,
                                    double effectiveRate, uint64_t& outStartFrame,
                                    uint64_t& outEndFrame, std::string& outError,
                                    LocalizedError* outLocError = nullptr);

    /**
     * @brief WAVヘッダサイズを事前計算
     */
    static uint64_t CalculateHeaderSize(WavContainerFormat container, uint32_t channels, WavSampleFormat format);

    /**
     * @brief エクスポートオプションとDLOGヘッダから事前見積もり情報を計算
     */
    static WavExportEstimate AnalyzePlan(const DLogFileHeader& header, uint64_t totalFrames,
                                         const WavExportOptions& options);

    /**
     * @brief 入出力パスの非破壊妥当性検証 (自己上書き・メタデータ破壊防止)
     */
    static bool ValidatePaths(const std::string& sourceDlogPath, const std::string& destinationWavPath,
                              std::string& outError, LocalizedError* outLocError = nullptr);
};

} // namespace dlog
