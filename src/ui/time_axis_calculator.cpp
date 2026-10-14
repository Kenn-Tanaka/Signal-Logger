#include "ui/time_axis_calculator.h"
#include <cmath>
#include <cstdio>

namespace dlog {

TimeScaleInfo CalculateTimeScale(double timeWindowSec) {
    TimeScaleInfo info;
    if (timeWindowSec <= 0.0) {
        timeWindowSec = 2.0;
    }
    info.secondsPerDiv = timeWindowSec / 10.0;

    double sPerDiv = info.secondsPerDiv;
    char buf[64];

    if (sPerDiv < 0.00099999) {
        // マイクロ秒表示 (us/div)
        double us = sPerDiv * 1e6;
        if (std::fabs(us - std::round(us)) < 1e-3) {
            std::snprintf(buf, sizeof(buf), "%.0f \u03BCs/div", us);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f \u03BCs/div", us);
        }
        info.scaleString = buf;
        info.scaleUnit = "\u03BCs";

        // 11個の div 目盛 (-5 div 〜 +5 div)
        for (int i = -5; i <= 5; ++i) {
            double divUs = i * us;
            if (i == 5) {
                std::snprintf(buf, sizeof(buf), "+%.0f %s", divUs, info.scaleUnit.c_str());
            } else if (i > 0) {
                std::snprintf(buf, sizeof(buf), "+%.0f", divUs);
            } else if (i == 0) {
                std::snprintf(buf, sizeof(buf), "0");
            } else {
                std::snprintf(buf, sizeof(buf), "%.0f", divUs);
            }
            info.divLabels.push_back(buf);
        }
    } else if (sPerDiv < 0.99999) {
        // ミリ秒表示 (ms/div)
        double ms = sPerDiv * 1e3;
        if (std::fabs(ms - std::round(ms)) < 1e-3) {
            std::snprintf(buf, sizeof(buf), "%.0f ms/div", ms);
        } else {
            std::snprintf(buf, sizeof(buf), "%.1f ms/div", ms);
        }
        info.scaleString = buf;
        info.scaleUnit = "ms";

        for (int i = -5; i <= 5; ++i) {
            double divMs = i * ms;
            if (i == 5) {
                std::snprintf(buf, sizeof(buf), "+%.0f %s", divMs, info.scaleUnit.c_str());
            } else if (i > 0) {
                std::snprintf(buf, sizeof(buf), "+%.0f", divMs);
            } else if (i == 0) {
                std::snprintf(buf, sizeof(buf), "0");
            } else {
                std::snprintf(buf, sizeof(buf), "%.0f", divMs);
            }
            info.divLabels.push_back(buf);
        }
    } else {
        // 秒表示 (s/div)
        double s = sPerDiv;
        if (std::fabs(s - std::round(s)) < 1e-3) {
            std::snprintf(buf, sizeof(buf), "%.0f s/div", s);
        } else if (std::fabs(s * 10.0 - std::round(s * 10.0)) < 1e-3) {
            std::snprintf(buf, sizeof(buf), "%.1f s/div", s);
        } else {
            std::snprintf(buf, sizeof(buf), "%.2f s/div", s);
        }
        info.scaleString = buf;
        info.scaleUnit = "s";

        for (int i = -5; i <= 5; ++i) {
            double divS = i * s;
            if (i == 5) {
                if (std::fabs(divS - std::round(divS)) < 1e-3) {
                    std::snprintf(buf, sizeof(buf), "+%.0f %s", divS, info.scaleUnit.c_str());
                } else {
                    std::snprintf(buf, sizeof(buf), "+%.1f %s", divS, info.scaleUnit.c_str());
                }
            } else if (i > 0) {
                if (std::fabs(divS - std::round(divS)) < 1e-3) {
                    std::snprintf(buf, sizeof(buf), "+%.0f", divS);
                } else {
                    std::snprintf(buf, sizeof(buf), "+%.1f", divS);
                }
            } else if (i == 0) {
                std::snprintf(buf, sizeof(buf), "0");
            } else {
                if (std::fabs(divS - std::round(divS)) < 1e-3) {
                    std::snprintf(buf, sizeof(buf), "%.0f", divS);
                } else {
                    std::snprintf(buf, sizeof(buf), "%.1f", divS);
                }
            }
            info.divLabels.push_back(buf);
        }
    }

    return info;
}

TimeRangeInfo CalculateTimeRange(uint64_t lastFrameIndex,
                                 bool hasData,
                                 double timeWindowSec,
                                 double sampleRate) {
    TimeRangeInfo info;
    info.hasData = hasData;

    if (sampleRate <= 0.0) {
        sampleRate = 22050.0;
    }
    if (timeWindowSec <= 0.0) {
        timeWindowSec = 2.0;
    }

    if (!hasData) {
        info.rightTime = 0.0;
        info.leftTime = -timeWindowSec;
        info.centerTime = -timeWindowSec / 2.0;
        info.centerTimeString = "\u2014"; // "—" (EM DASH)
        return info;
    }

    info.rightTime = (static_cast<double>(lastFrameIndex) + 1.0) / sampleRate;
    info.leftTime = info.rightTime - timeWindowSec;
    info.centerTime = (info.leftTime + info.rightTime) / 2.0;

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.6f s", info.centerTime);
    info.centerTimeString = buf;

    return info;
}

} // namespace dlog
