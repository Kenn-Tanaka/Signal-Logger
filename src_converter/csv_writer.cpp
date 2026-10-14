#include "csv_writer.h"
#include <iomanip>
#include <sstream>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace dlog {

constexpr size_t BUFFER_SIZE = 256 * 1024; // 256 KB バッファ

namespace {

bool ReplacePath(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    std::error_code ec;
    const bool destinationExists = std::filesystem::exists(destination, ec);
    if (ec) return false;
    if (destinationExists) {
        return ReplaceFileW(destination.c_str(), source.c_str(), nullptr,
                            REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != 0;
    }
    return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(source, destination, ec);
    return !ec;
#endif
}

bool MoveReplacing(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    return MoveFileExW(source.c_str(), destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(source, destination, ec);
    return !ec;
#endif
}

std::string EscapeDelimitedField(std::string value, char delimiter) {
    if (!value.empty() && (value[0] == '=' || value[0] == '+' ||
                           value[0] == '-' || value[0] == '@')) {
        value.insert(value.begin(), '\'');
    }
    const bool needsQuotes = value.find(delimiter) != std::string::npos ||
                             value.find('"') != std::string::npos ||
                             value.find('\r') != std::string::npos ||
                             value.find('\n') != std::string::npos;
    if (!needsQuotes) return value;

    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        if (c == '"') escaped.push_back('"');
        escaped.push_back(c);
    }
    escaped.push_back('"');
    return escaped;
}

} // namespace

CsvWriter::CsvWriter() {
    m_streamBuffer.resize(BUFFER_SIZE);
}

CsvWriter::~CsvWriter() {
    // 明示的にFinishされていない出力は、例外・中断による未完成ファイルとして破棄する。
    Abort();
}

bool CsvWriter::Initialize(const std::string& baseOutputDir,
                           const std::string& baseFileName,
                           const ConversionOptions& options,
                           const DLogFileHeader& header,
                           bool willSplit,
                           uint64_t totalParts,
                           int branchDigits) {
    Abort();

    m_baseOutputDir = baseOutputDir;
    m_baseFileName = baseFileName;
    m_options = options;
    m_header = header;

    if (totalParts > 1) {
        m_willSplit = true;
        int len = static_cast<int>(std::to_string(totalParts).length());
        m_branchDigits = std::max({2, len, branchDigits});
    } else if (willSplit) {
        m_willSplit = true;
        m_branchDigits = std::max(2, branchDigits);
    } else {
        m_willSplit = false;
        m_branchDigits = std::max(2, branchDigits);
    }


    m_currentPart = 0;
    m_currentFileLines = 0;
    m_totalRowsWritten = 0;
    m_generatedFiles.clear();
    m_currentFinalPath.clear();
    m_currentTemporaryPath.clear();
    m_lastError.clear();
    m_hasError = false;
    m_cachedSec = static_cast<time_t>(-1);
    m_cachedSecStr[0] = '\0';

    m_delim = m_options.delimiter;

    // 出力対象チャネルの決定
    m_channelIndices.clear();
    if (m_options.selectedChannels.empty()) {
        for (uint32_t ch = 0; ch < m_header.channels; ++ch) {
            m_channelIndices.push_back(static_cast<int>(ch));
        }
    } else {
        for (int ch : m_options.selectedChannels) {
            if (ch >= 0 && ch < static_cast<int>(m_header.channels)) {
                m_channelIndices.push_back(ch);
            }
        }
    }

    if (m_channelIndices.empty()) {
        m_channelIndices.push_back(0);
    }

    // フォーマット文字列
    int prec = std::max(1, std::min(m_options.floatPrecision, 10));
    char fmt[16];
    std::snprintf(fmt, sizeof(fmt), "%%.%df", prec);
    m_floatFormat = fmt;

    return OpenNextFile();
}

bool CsvWriter::OpenNextFile() {
    if (m_fileStream.is_open()) {
        if (!FinalizeCurrentFile()) {
            return false;
        }
    }

    m_currentPart++;
    m_currentFileLines = 0;

    std::string ext = (m_delim == '\t') ? ".tsv" : ".csv";
    std::string filePath;

    std::string dir = m_baseOutputDir;
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') {
        dir += "/";
    }

    // 事前分割なし想定だったが2ファイル目以降に突入した場合の動的保護
    if (!m_willSplit && m_currentPart == 2) {
        m_willSplit = true;
        if (!m_generatedFiles.empty()) {
            std::string oldPath = m_generatedFiles[0];
            char firstBranchBuf[32];
            std::snprintf(firstBranchBuf, sizeof(firstBranchBuf), "-%0*d", m_branchDigits, 1);
            std::string newPath = dir + m_baseFileName + firstBranchBuf + ext;

            if (!MoveReplacing(std::filesystem::u8path(oldPath), std::filesystem::u8path(newPath))) {
                m_lastError = "分割CSVの1ファイル目を枝番付き名称へ変更できません";
                m_hasError = true;
                return false;
            }
            m_generatedFiles[0] = newPath;
        }
    }

    if (!m_willSplit && m_currentPart == 1) {
        filePath = dir + m_baseFileName + ext;
    } else {
        char branchBuf[32];
        std::snprintf(branchBuf, sizeof(branchBuf), "-%0*d", m_branchDigits, m_currentPart);
        filePath = dir + m_baseFileName + branchBuf + ext;
    }

    auto outPath = std::filesystem::u8path(filePath);
    if (outPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(outPath.parent_path(), ec);
        if (ec) {
            m_lastError = "CSV出力ディレクトリを作成できません";
            m_hasError = true;
            return false;
        }
    }

    m_currentFinalPath = outPath;
    m_currentTemporaryPath = outPath;
    m_currentTemporaryPath += L".partial";
    std::error_code tempEc;
    for (unsigned int suffix = 1; std::filesystem::exists(m_currentTemporaryPath, tempEc); ++suffix) {
        tempEc.clear();
        m_currentTemporaryPath = outPath;
        m_currentTemporaryPath += L".partial." + std::to_wstring(suffix);
    }
    if (tempEc) {
        m_lastError = "CSV一時ファイル名を決定できません";
        m_hasError = true;
        return false;
    }

    m_fileStream.open(m_currentTemporaryPath, std::ios::out | std::ios::trunc);
    if (!m_fileStream.is_open()) {
        m_lastError = "CSV一時ファイルを作成できません";
        m_hasError = true;
        return false;
    }

    // 大容量バッファ設定
    m_fileStream.rdbuf()->pubsetbuf(m_streamBuffer.data(), m_streamBuffer.size());

    if (!WriteHeaderLine()) {
        m_lastError = "CSVヘッダを書き込めません";
        m_hasError = true;
        return false;
    }

    return true;
}

bool CsvWriter::WriteHeaderLine() {
    if (!m_fileStream.is_open()) return false;

    bool firstCol = true;
    switch (m_options.timeFormat) {
        case TimeFormat::RelativeSec:
            m_fileStream << "Time[s]";
            firstCol = false;
            break;
        case TimeFormat::SampleIndex:
            m_fileStream << "SampleIndex";
            firstCol = false;
            break;
        case TimeFormat::AbsoluteTime:
            m_fileStream << "DateTimeUTC";
            firstCol = false;
            break;
        case TimeFormat::None:
            break;
    }

    for (int chIdx : m_channelIndices) {
        if (!firstCol) {
            m_fileStream << m_delim;
        }
        std::string colName = "CH" + std::to_string(chIdx + 1);
        std::string colUnit = "V";
        if (chIdx < static_cast<int>(m_options.channelMetadata.size())) {
            if (!m_options.channelMetadata[chIdx].name.empty()) {
                colName = m_options.channelMetadata[chIdx].name;
            }
            if (!m_options.channelMetadata[chIdx].unit.empty()) {
                colUnit = m_options.channelMetadata[chIdx].unit;
            }
        }
        if (m_options.applyVoltageScale) {
            m_fileStream << EscapeDelimitedField(colName + "[" + colUnit + "]", m_delim);
        } else {
            m_fileStream << EscapeDelimitedField(colName, m_delim);
        }
        firstCol = false;
    }
    m_fileStream << "\n";
    return m_fileStream.good();
}

std::string CsvWriter::FormatTimestamp(uint64_t frameIndex) {
    char buf[64];
    double rate = GetDLogEffectiveSampleRate(m_header);
    if (!(rate > 0.0) || !std::isfinite(rate)) rate = 22050.0;

    switch (m_options.timeFormat) {
        case TimeFormat::RelativeSec: {
            double sec = static_cast<double>(frameIndex) / rate;
            std::snprintf(buf, sizeof(buf), "%.6f", sec);
            return buf;
        }
        case TimeFormat::SampleIndex: {
            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(frameIndex));
            return buf;
        }
        case TimeFormat::AbsoluteTime: {
            const long double totalMsValue =
                static_cast<long double>(m_header.startTimestampUtc) +
                (static_cast<long double>(frameIndex) / static_cast<long double>(rate)) * 1000.0L;
            constexpr long double MAX_SUPPORTED_UTC_MS = 4102444800999.0L; // 2100-01-01未満
            if (!std::isfinite(totalMsValue) || totalMsValue < 0.0L ||
                totalMsValue > MAX_SUPPORTED_UTC_MS) {
                return "1970-01-01 00:00:00.000Z";
            }
            int64_t totalMs = static_cast<int64_t>(std::round(totalMsValue));

            time_t secPart = static_cast<time_t>(totalMs / 1000);
            int msPart = static_cast<int>(totalMs % 1000);
            if (msPart < 0) msPart += 1000;

            // 秒が変わった時のみUTC日時を文字列化
            if (secPart != m_cachedSec) {
                m_cachedSec = secPart;
                struct tm timeinfo;
                std::memset(&timeinfo, 0, sizeof(timeinfo));
#ifdef _WIN32
                errno_t err = gmtime_s(&timeinfo, &secPart);
                if (err != 0) {
                    std::snprintf(m_cachedSecStr, sizeof(m_cachedSecStr), "1970-01-01 00:00:00");
                } else
#else
                if (gmtime_r(&secPart, &timeinfo) == nullptr) {
                    std::snprintf(m_cachedSecStr, sizeof(m_cachedSecStr), "1970-01-01 00:00:00");
                } else
#endif
                {
                    std::snprintf(m_cachedSecStr, sizeof(m_cachedSecStr), "%04d-%02d-%02d %02d:%02d:%02d",
                                  timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
                }
            }

            std::snprintf(buf, sizeof(buf), "%s.%03dZ", m_cachedSecStr, msPart);
            return buf;
        }
        case TimeFormat::None:
        default:
            return "";
    }
}

bool CsvWriter::WriteRow(uint64_t frameIndex, const float* samples, int channelCount) {
    if (!m_fileStream.is_open() || m_hasError || !samples || channelCount < 1) return false;

    // 行数上限チェックによる自動ファイル分割
    if (m_options.enableSplitLines && m_options.maxLinesPerFile > 0) {
        if (m_currentFileLines >= m_options.maxLinesPerFile) {
            if (!OpenNextFile()) {
                m_hasError = true;
                return false;
            }
        }
    }

    char numBuf[32];
    bool firstCol = true;

    if (m_options.timeFormat != TimeFormat::None) {
        std::string tsStr = FormatTimestamp(frameIndex);
        m_fileStream << tsStr;
        firstCol = false;
    }

    double vScale = 1.0;
    if (m_options.applyVoltageScale) {
        if (m_options.customFullScaleVoltage > 0.0) {
            vScale = m_options.customFullScaleVoltage;
        } else if (m_header.fullScaleVoltage > 0.0) {
            vScale = m_header.fullScaleVoltage;
        }
    }

    for (int chIdx : m_channelIndices) {
        if (!firstCol) {
            m_fileStream << m_delim;
        }
        float val = 0.0f;
        if (chIdx >= 0 && chIdx < channelCount) {
            val = samples[chIdx];
        }
        double finalVal = (m_options.applyVoltageScale) ? (static_cast<double>(val) * vScale) : static_cast<double>(val);
        std::snprintf(numBuf, sizeof(numBuf), m_floatFormat.c_str(), finalVal);
        m_fileStream << numBuf;
        firstCol = false;
    }
    m_fileStream << "\n";

    if (!m_fileStream.good()) {
        m_lastError = "CSVデータの書き込みに失敗しました";
        m_hasError = true;
        return false;
    }

    m_currentFileLines++;
    m_totalRowsWritten++;
    return true;
}

bool CsvWriter::FinalizeCurrentFile() {
    if (!m_fileStream.is_open()) return !m_hasError;

    m_fileStream.flush();
    const bool writeOk = m_fileStream.good();
    m_fileStream.close();
    if (!writeOk || m_hasError) {
        if (m_lastError.empty()) m_lastError = "CSVファイルのflushに失敗しました";
        m_hasError = true;
        return false;
    }

    if (!ReplacePath(m_currentTemporaryPath, m_currentFinalPath)) {
        m_lastError = "CSV一時ファイルを最終ファイル名へ確定できません";
        m_hasError = true;
        return false;
    }

    m_generatedFiles.push_back(m_currentFinalPath.u8string());
    m_currentTemporaryPath.clear();
    m_currentFinalPath.clear();
    return true;
}

bool CsvWriter::Finish() {
    if (m_fileStream.is_open() && !FinalizeCurrentFile()) {
        return false;
    }
    return !m_hasError;
}

void CsvWriter::Abort() {
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
    if (!m_currentTemporaryPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_currentTemporaryPath, ec);
    }
    m_currentTemporaryPath.clear();
    m_currentFinalPath.clear();
}

} // namespace dlog
