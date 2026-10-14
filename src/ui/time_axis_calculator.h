#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace dlog {

struct TimeScaleInfo {
    double secondsPerDiv = 0.0;
    std::string scaleString;      // 例: "200 ms/div", "100 us/div", "1 s/div"
    std::string scaleUnit;        // 例: "ms", "us", "s"
    std::vector<std::string> divLabels; // 11個のラベル (-5 div 〜 +5 div)
};

struct TimeRangeInfo {
    bool hasData = false;
    double leftTime = 0.0;
    double rightTime = 0.0;
    double centerTime = 0.0;
    std::string centerTimeString; // 例: "12.345000 s" または "—"
};

/**
 * @brief 表示時間幅から 10 div の時間スケール情報と目盛ラベルを計算する
 * @param timeWindowSec 表示時間幅（秒）
 * @return TimeScaleInfo
 */
TimeScaleInfo CalculateTimeScale(double timeWindowSec);

/**
 * @brief 最終フレームインデックス・時間幅・サンプリング周波数から表示時間範囲を計算する
 * @param lastFrameIndex 受信した最新フレームの累積インデックス
 * @param hasData データが受信済みか
 * @param timeWindowSec 表示時間幅（秒）
 * @param sampleRate デシメーション後サンプリング周波数 (nativeRate / decimationFactor)
 * @return TimeRangeInfo
 */
TimeRangeInfo CalculateTimeRange(uint64_t lastFrameIndex,
                                 bool hasData,
                                 double timeWindowSec,
                                 double sampleRate);

} // namespace dlog
