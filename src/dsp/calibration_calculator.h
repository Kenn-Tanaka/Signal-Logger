#ifndef DLOG_CALIBRATION_CALCULATOR_H
#define DLOG_CALIBRATION_CALCULATOR_H

#include <cstddef>
#include <cstdint>

namespace dlog {

enum class CalibrationSignalStatus {
    NoSignal,    // Vpp < 0.01 (信号未検出)
    LowSignal,   // 0.01 <= Vpp < 0.05 (微小信号・精度低下の可能性)
    Good,        // 0.05 <= Vpp <= 1.95 かつ Peak < 0.99 (適正)
    Clipping     // Peak >= 0.99 または Vpp > 1.95 (ADC過大入力・飽和)
};

struct SignalStats {
    double vpp = 0.0;
    double minVal = 0.0;
    double maxVal = 0.0;
    double peakVal = 0.0;
    CalibrationSignalStatus status = CalibrationSignalStatus::NoSignal;
};

class CalibrationCalculator {
public:
    // サンプル配列から信号のPeak-to-Peak、ピーク値、信号ステータスを解析
    static SignalStats AnalyzeSamples(const float* samples, size_t count);

    // 目標 Vpp と 実測デジタル Vpp からフルスケール電圧 (片振幅 V_FS) を算出
    // V_FS = targetVpp / measuredVpp
    static bool CalculateFullScale(double targetVpp, double measuredVpp, double& outVfs);
};

} // namespace dlog

#endif // DLOG_CALIBRATION_CALCULATOR_H
