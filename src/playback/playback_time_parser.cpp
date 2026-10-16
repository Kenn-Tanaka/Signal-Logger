#include "playback_time_parser.h"
#include "common/language_preferences.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <vector>

namespace dlog {

namespace {

std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, last - first + 1);
}

inline std::string PickMsg(const char* ja, const char* en) {
    return (LanguagePreferences::GetCurrentLanguage() == Language::English) ? en : ja;
}

} // anonymous namespace

bool ParsePlaybackTimeRaw(const std::string& input,
                          double maxDuration,
                          double& outRawSec,
                          std::string& errorMsg,
                          double boundaryTolerance) {
    std::string s = Trim(input);
    if (s.empty()) {
        errorMsg = PickMsg("時刻が入力されていません。", "Time is not entered.");
        return false;
    }

    if (s.front() == '-') {
        errorMsg = PickMsg("負の時刻は指定できません。", "Negative time cannot be specified.");
        return false;
    }

    // 文字検証 (数字, '.', ':', 空白以外はエラー)
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != ':' && c != ' ') {
            errorMsg = PickMsg("時刻の書式が不正です (HH:MM:SS または秒数を指定してください)。",
                               "Invalid time format (use HH:MM:SS or seconds).");
            return false;
        }
    }

    // コロン ':' で分割
    std::vector<std::string> parts;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ':')) {
        parts.push_back(Trim(part));
    }

    double totalSec = 0.0;

    try {
        if (parts.size() == 1) {
            // 単一の秒数指定 (例: "123.456", "10")
            size_t idx = 0;
            double val = std::stod(parts[0], &idx);
            if (idx != parts[0].size()) {
                errorMsg = PickMsg("秒数の書式が不正です。", "Invalid seconds format.");
                return false;
            }
            if (std::isnan(val) || std::isinf(val)) {
                errorMsg = PickMsg("無効な数値です。", "Invalid numeric value.");
                return false;
            }
            totalSec = val;
        } else if (parts.size() == 2) {
            // MM:SS[.fff]
            size_t idxM = 0;
            int m = std::stoi(parts[0], &idxM);
            if (idxM != parts[0].size() || m < 0) {
                errorMsg = PickMsg("分の値が不正です。", "Invalid minutes value.");
                return false;
            }
            size_t idx = 0;
            double sec = std::stod(parts[1], &idx);
            if (idx != parts[1].size()) {
                errorMsg = PickMsg("秒の書式が不正です。", "Invalid seconds format.");
                return false;
            }
            if (sec < 0.0 || sec >= 60.0) {
                errorMsg = PickMsg("秒の値は0以上60未満である必要があります。", "Seconds must be between 0 and < 60.");
                return false;
            }
            totalSec = m * 60.0 + sec;
        } else if (parts.size() == 3) {
            // HH:MM:SS[.fff]
            size_t idxH = 0;
            int h = std::stoi(parts[0], &idxH);
            if (idxH != parts[0].size() || h < 0) {
                errorMsg = PickMsg("時の値が不正です。", "Invalid hours value.");
                return false;
            }
            size_t idxM = 0;
            int m = std::stoi(parts[1], &idxM);
            if (idxM != parts[1].size() || m < 0 || m >= 60) {
                errorMsg = PickMsg("分の値は0以上60未満である必要があります。", "Minutes must be between 0 and < 60.");
                return false;
            }
            size_t idx = 0;
            double sec = std::stod(parts[2], &idx);
            if (idx != parts[2].size()) {
                errorMsg = PickMsg("秒の書式が不正です。", "Invalid seconds format.");
                return false;
            }
            if (sec < 0.0 || sec >= 60.0) {
                errorMsg = PickMsg("秒の値は0以上60未満である必要があります。", "Seconds must be between 0 and < 60.");
                return false;
            }
            totalSec = h * 3600.0 + m * 60.0 + sec;
        } else {
            errorMsg = PickMsg("時刻の書式が不正です (コロンが多すぎます)。", "Invalid time format (too many colons).");
            return false;
        }
    } catch (...) {
        errorMsg = PickMsg("時刻の数値変換に失敗しました。", "Failed to parse time value.");
        return false;
    }

    if (totalSec < 0.0) {
        errorMsg = PickMsg("負の時刻は指定できません。", "Negative time cannot be specified.");
        return false;
    }

    // 範囲チェック (指定時刻が末尾を超えている場合はエラーを表示し、黙って丸めない)
    if (totalSec > maxDuration + boundaryTolerance) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        if (LanguagePreferences::GetCurrentLanguage() == Language::English) {
            oss << "Specified time (" << totalSec << " s) exceeds the end of data (" << maxDuration << " s).";
        } else {
            oss << "指定時刻 (" << totalSec << " s) がデータの末尾 (" << maxDuration << " s) を超えています。";
        }
        errorMsg = oss.str();
        return false;
    }

    if (totalSec > maxDuration) {
        totalSec = maxDuration;
    }

    outRawSec = totalSec;
    return true;
}

bool ParsePlaybackTimeString(const std::string& input,
                             double maxPosition,
                             double Fs,
                             double& outAlignedSec,
                             std::string& errorMsg) {
    if (Fs <= 0.0 || std::isnan(Fs) || std::isinf(Fs)) {
        errorMsg = PickMsg("サンプリングレートが無効です。", "Invalid sampling rate.");
        return false;
    }

    double totalSec = 0.0;
    if (!ParsePlaybackTimeRaw(input, maxPosition, totalSec, errorMsg, 1e-9)) {
        return false;
    }

    // 最も近いサンプルフレームへスナップ
    int64_t frameIdx = static_cast<int64_t>(std::round(totalSec * Fs));
    if (frameIdx < 0) frameIdx = 0;
    outAlignedSec = static_cast<double>(frameIdx) / Fs;
    if (outAlignedSec > maxPosition) {
        outAlignedSec = maxPosition;
    }

    return true;
}

std::string FormatPlaybackTime(double seconds, bool includeMicros) {
    if (seconds < 0.0) seconds = 0.0;
    if (std::isnan(seconds) || std::isinf(seconds)) return "--:--:--.---";

    int64_t totalWholeSec = static_cast<int64_t>(seconds);
    int hours = static_cast<int>(totalWholeSec / 3600);
    int mins = static_cast<int>((totalWholeSec % 3600) / 60);
    int secs = static_cast<int>(totalWholeSec % 60);
    double frac = seconds - static_cast<double>(totalWholeSec);

    char buf[64];
    if (includeMicros) {
        int micros = static_cast<int>(std::round(frac * 1000000.0));
        if (micros >= 1000000) {
            micros = 0;
            secs++;
            if (secs >= 60) {
                secs = 0;
                mins++;
                if (mins >= 60) {
                    mins = 0;
                    hours++;
                }
            }
        }
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%06d", hours, mins, secs, micros);
    } else {
        int millis = static_cast<int>(std::round(frac * 1000.0));
        if (millis >= 1000) {
            millis = 0;
            secs++;
            if (secs >= 60) {
                secs = 0;
                mins++;
                if (mins >= 60) {
                    mins = 0;
                    hours++;
                }
            }
        }
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", hours, mins, secs, millis);
    }
    return std::string(buf);
}

double CalculateMaxPosition(uint64_t totalFrames, double Fs) {
    if (totalFrames == 0 || Fs <= 0.0) return 0.0;
    return static_cast<double>(totalFrames - 1) / Fs;
}

double CalculateTotalDuration(uint64_t totalFrames, double Fs) {
    if (totalFrames == 0 || Fs <= 0.0) return 0.0;
    return static_cast<double>(totalFrames) / Fs;
}

uint64_t TimeToFrameIndex(double timeSec, double Fs, uint64_t totalFrames) {
    if (totalFrames == 0 || Fs <= 0.0 || timeSec <= 0.0) return 0;
    int64_t idx = static_cast<int64_t>(std::round(timeSec * Fs));
    if (idx < 0) return 0;
    if (static_cast<uint64_t>(idx) >= totalFrames) return totalFrames - 1;
    return static_cast<uint64_t>(idx);
}

double FrameIndexToTime(uint64_t frameIndex, double Fs) {
    if (Fs <= 0.0) return 0.0;
    return static_cast<double>(frameIndex) / Fs;
}

uint64_t FrameIndexToFileOffset(uint64_t frameIndex, uint32_t headerSize, int channels, size_t bytesPerSample) {
    return static_cast<uint64_t>(headerSize) +
           frameIndex * static_cast<uint64_t>(channels) * static_cast<uint64_t>(bytesPerSample);
}

} // namespace dlog
