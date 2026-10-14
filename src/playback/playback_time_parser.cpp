#include "playback_time_parser.h"
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

} // anonymous namespace

bool ParsePlaybackTimeString(const std::string& input,
                             double maxPosition,
                             double Fs,
                             double& outAlignedSec,
                             std::string& errorMsg) {
    std::string s = Trim(input);
    if (s.empty()) {
        errorMsg = "時刻が入力されていません。";
        return false;
    }

    if (s.front() == '-') {
        errorMsg = "負の時刻は指定できません。";
        return false;
    }

    if (Fs <= 0.0 || std::isnan(Fs) || std::isinf(Fs)) {
        errorMsg = "サンプリングレートが無効です。";
        return false;
    }

    // 文字検証 (数字, '.', ':', 空白以外はエラー)
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '.' && c != ':' && c != ' ') {
            errorMsg = "時刻の書式が不正です (HH:MM:SS または秒数を指定してください)。";
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
                errorMsg = "秒数の書式が不正です。";
                return false;
            }
            if (std::isnan(val) || std::isinf(val)) {
                errorMsg = "無効な数値です。";
                return false;
            }
            totalSec = val;
        } else if (parts.size() == 2) {
            // MM:SS[.fff]
            size_t idxM = 0;
            int m = std::stoi(parts[0], &idxM);
            if (idxM != parts[0].size() || m < 0) {
                errorMsg = "分の値が不正です。";
                return false;
            }
            size_t idx = 0;
            double sec = std::stod(parts[1], &idx);
            if (idx != parts[1].size()) {
                errorMsg = "秒の書式が不正です。";
                return false;
            }
            if (sec < 0.0 || sec >= 60.0) {
                errorMsg = "秒の値は0以上60未満である必要があります。";
                return false;
            }
            totalSec = m * 60.0 + sec;
        } else if (parts.size() == 3) {
            // HH:MM:SS[.fff]
            size_t idxH = 0;
            int h = std::stoi(parts[0], &idxH);
            if (idxH != parts[0].size() || h < 0) {
                errorMsg = "時の値が不正です。";
                return false;
            }
            size_t idxM = 0;
            int m = std::stoi(parts[1], &idxM);
            if (idxM != parts[1].size() || m < 0 || m >= 60) {
                errorMsg = "分の値は0以上60未満である必要があります。";
                return false;
            }
            size_t idx = 0;
            double sec = std::stod(parts[2], &idx);
            if (idx != parts[2].size()) {
                errorMsg = "秒の書式が不正です。";
                return false;
            }
            if (sec < 0.0 || sec >= 60.0) {
                errorMsg = "秒の値は0以上60未満である必要があります。";
                return false;
            }
            totalSec = h * 3600.0 + m * 60.0 + sec;
        } else {
            errorMsg = "時刻の書式が不正です (コロンが多すぎます)。";
            return false;
        }
    } catch (...) {
        errorMsg = "時刻の数値変換に失敗しました。";
        return false;
    }

    if (totalSec < 0.0) {
        errorMsg = "負の時刻は指定できません。";
        return false;
    }

    // 範囲チェック (指定時刻が末尾を超えている場合はエラーを表示し、黙って丸めない)
    const double epsilon = 1e-9;
    if (totalSec > maxPosition + epsilon) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        oss << "指定時刻 (" << totalSec << " s) がデータの末尾 (" << maxPosition << " s) を超えています。";
        errorMsg = oss.str();
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
