#include "calibration_calculator.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace dlog {

SignalStats CalibrationCalculator::AnalyzeSamples(const float* samples, size_t count) {
    SignalStats stats;
    if (!samples || count == 0) {
        return stats;
    }

    float minVal = std::numeric_limits<float>::infinity();
    float maxVal = -std::numeric_limits<float>::infinity();
    bool foundValid = false;

    for (size_t i = 0; i < count; ++i) {
        float val = samples[i];
        if (!std::isfinite(val)) continue;
        if (val < minVal) minVal = val;
        if (val > maxVal) maxVal = val;
        foundValid = true;
    }

    if (!foundValid || maxVal < minVal) {
        return stats;
    }

    stats.minVal = static_cast<double>(minVal);
    stats.maxVal = static_cast<double>(maxVal);
    stats.vpp = static_cast<double>(maxVal - minVal);
    stats.peakVal = static_cast<double>(std::max(std::abs(minVal), std::abs(maxVal)));

    // 信号品質ステータスの判定
    if (stats.peakVal >= 0.99 || stats.vpp > 1.95) {
        stats.status = CalibrationSignalStatus::Clipping;
    } else if (stats.vpp < 0.01) {
        stats.status = CalibrationSignalStatus::NoSignal;
    } else if (stats.vpp < 0.05) {
        stats.status = CalibrationSignalStatus::LowSignal;
    } else {
        stats.status = CalibrationSignalStatus::Good;
    }

    return stats;
}

bool CalibrationCalculator::CalculateFullScale(double targetVpp, double measuredVpp, double& outVfs) {
    if (!std::isfinite(targetVpp) || !std::isfinite(measuredVpp)) {
        outVfs = 0.0;
        return false;
    }
    if (targetVpp <= 0.0 || measuredVpp < 0.0001) {
        outVfs = 0.0;
        return false;
    }

    outVfs = targetVpp / measuredVpp;
    if (!std::isfinite(outVfs) || outVfs <= 0.0) {
        outVfs = 0.0;
        return false;
    }

    return true;
}

} // namespace dlog
