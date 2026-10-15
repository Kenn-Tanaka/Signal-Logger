#include "dlog_reader.h"
#include <cstring>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <limits>
#include <set>
#include <map>

namespace dlog {

std::vector<ChannelMetadata> DlogReader::LoadCompanionMeta(const std::string& filePath,
                                                          const DLogFileHeader& header,
                                                          std::string* outWarning) {
    if (outWarning) outWarning->clear();
    const uint32_t channelCount = header.channels;
    std::vector<ChannelMetadata> meta(channelCount);
    for (uint32_t i = 0; i < channelCount; ++i) {
        meta[i].name = "CH" + std::to_string(i + 1);
        meta[i].unit = "V";
    }

    std::string metaPath = filePath + ".meta";
    std::ifstream in(std::filesystem::u8path(metaPath));
    if (!in.is_open()) {
        size_t dot = filePath.find_last_of('.');
        if (dot != std::string::npos) {
            metaPath = filePath.substr(0, dot) + ".meta";
            in.open(std::filesystem::u8path(metaPath));
        }
    }
    if (!in.is_open()) {
        return meta;
    }

    std::string line;
    int currentCh = -1;
    bool inMetadataSection = false;
    bool mismatchDetected = false;
    std::string mismatchReason;

    std::set<std::string> seenMetaKeys;
    std::set<int> seenChannelSections;
    std::map<int, std::set<std::string>> seenChKeys;

    auto parseStrictU32 = [](const std::string& s, uint32_t& out) -> bool {
        if (s.empty()) return false;
        try {
            size_t idx = 0;
            unsigned long v = std::stoul(s, &idx);
            if (idx != s.size() || v > std::numeric_limits<uint32_t>::max()) return false;
            out = static_cast<uint32_t>(v);
            return true;
        } catch (...) {
            return false;
        }
    };

    auto parseStrictI64 = [](const std::string& s, int64_t& out) -> bool {
        if (s.empty()) return false;
        try {
            size_t idx = 0;
            long long v = std::stoll(s, &idx);
            if (idx != s.size()) return false;
            out = static_cast<int64_t>(v);
            return true;
        } catch (...) {
            return false;
        }
    };

    auto parseStrictDouble = [](const std::string& s, double& out) -> bool {
        if (s.empty()) return false;
        try {
            size_t idx = 0;
            double v = std::stod(s, &idx);
            if (idx != s.size() || !std::isfinite(v)) return false;
            out = v;
            return true;
        } catch (...) {
            return false;
        }
    };

    bool hasDlogMetadataSection = false;

    while (std::getline(in, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line.rfind("[DLOG_METADATA]", 0) == 0) {
            if (hasDlogMetadataSection) {
                mismatchDetected = true;
                mismatchReason = "重複セクション検出 ([DLOG_METADATA])";
                break;
            }
            hasDlogMetadataSection = true;
            inMetadataSection = true;
            currentCh = -1;
            continue;
        }

        if (line.rfind("[CH", 0) == 0) {
            inMetadataSection = false;
            size_t closeBracket = line.find(']');
            if (closeBracket != std::string::npos) {
                std::string numStr = line.substr(3, closeBracket - 3);
                uint32_t chNum = 0;
                if (!parseStrictU32(numStr, chNum) || chNum == 0) {
                    currentCh = -1;
                } else {
                    currentCh = static_cast<int>(chNum) - 1;
                    if (seenChannelSections.count(currentCh) > 0) {
                        mismatchDetected = true;
                        mismatchReason = "重複チャネルセクション検出 ([CH" + std::to_string(chNum) + "])";
                        break;
                    }
                    seenChannelSections.insert(currentCh);
                }
            }
        } else if (inMetadataSection) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                val.erase(0, val.find_first_not_of(" \t\r\n"));
                val.erase(val.find_last_not_of(" \t\r\n") + 1);

                if (seenMetaKeys.count(key) > 0) {
                    mismatchDetected = true;
                    mismatchReason = "重複キー検出 (" + key + ")";
                    break;
                }
                seenMetaKeys.insert(key);

                if (key == "magic") {
                    if (val != "DLOG") {
                        mismatchDetected = true;
                        mismatchReason = "マジックナンバー不一致 (" + val + " != DLOG)";
                    }
                } else if (key == "version") {
                    uint32_t ver = 0;
                    if (!parseStrictU32(val, ver) || (ver != 1 && ver != DLOG_FORMAT_VERSION)) {
                        mismatchDetected = true;
                        mismatchReason = "version不正 (" + val + ")";
                    } else if (ver != header.version) {
                        mismatchDetected = true;
                        mismatchReason = "フォーマットバージョン不一致 (" + std::to_string(ver) + " != " + std::to_string(header.version) + ")";
                    }
                } else if (key == "channels") {
                    uint32_t ch = 0;
                    if (!parseStrictU32(val, ch) || ch == 0 || ch > MAX_AUDIO_CHANNELS) {
                        mismatchDetected = true;
                        mismatchReason = "channels不正 (" + val + ")";
                    } else if (ch != channelCount) {
                        mismatchDetected = true;
                        mismatchReason = "チャネル数不一致 (" + std::to_string(ch) + " != " + std::to_string(channelCount) + ")";
                    }
                } else if (key == "native_rate") {
                    uint32_t nr = 0;
                    if (!parseStrictU32(val, nr) || nr == 0) {
                        mismatchDetected = true;
                        mismatchReason = "native_rate不正 (" + val + ")";
                    } else if (header.nativeRate != 0 && nr != header.nativeRate) {
                        mismatchDetected = true;
                        mismatchReason = "ネイティブサンプリング周波数不一致 (" + std::to_string(nr) + " != " + std::to_string(header.nativeRate) + ")";
                    }
                } else if (key == "decimation_factor") {
                    uint32_t df = 0;
                    if (!parseStrictU32(val, df) || df == 0) {
                        mismatchDetected = true;
                        mismatchReason = "decimation_factor不正 (" + val + ")";
                    } else if (header.decimationFactor != 0 && df != header.decimationFactor) {
                        mismatchDetected = true;
                        mismatchReason = "デシメーション間引き率不一致 (" + std::to_string(df) + " != " + std::to_string(header.decimationFactor) + ")";
                    }
                } else if (key == "start_timestamp_utc") {
                    int64_t ts = 0;
                    if (!parseStrictI64(val, ts)) {
                        mismatchDetected = true;
                        mismatchReason = "start_timestamp_utc不正 (" + val + ")";
                    } else if (header.startTimestampUtc != 0 && ts != header.startTimestampUtc) {
                        mismatchDetected = true;
                        mismatchReason = "記録開始時刻不一致 (" + std::to_string(ts) + " != " + std::to_string(header.startTimestampUtc) + ")";
                    }
                } else if (key == "full_scale_voltage") {
                    double vfs = 0.0;
                    if (!parseStrictDouble(val, vfs) || vfs <= 0.0) {
                        mismatchDetected = true;
                        mismatchReason = "full_scale_voltage不正 (" + val + ")";
                    } else if (header.fullScaleVoltage > 0.0 && std::fabs(vfs - header.fullScaleVoltage) > 1e-4) {
                        mismatchDetected = true;
                        mismatchReason = "フルスケール電圧不一致 (" + std::to_string(vfs) + " != " + std::to_string(header.fullScaleVoltage) + ")";
                    }
                } else if (key == "anti_aliasing") {
                    if (val != "true" && val != "false" && val != "1" && val != "0") {
                        mismatchDetected = true;
                        mismatchReason = "anti_aliasing不正 (" + val + ")";
                    }
                }

                if (mismatchDetected) {
                    break;
                }
            }
        } else if (currentCh >= 0 && currentCh < static_cast<int>(channelCount)) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                key.erase(key.find_last_not_of(" \t\r\n") + 1);
                val.erase(0, val.find_first_not_of(" \t\r\n"));
                val.erase(val.find_last_not_of(" \t\r\n") + 1);

                if (seenChKeys[currentCh].count(key) > 0) {
                    mismatchDetected = true;
                    mismatchReason = "CH" + std::to_string(currentCh + 1) + " 重複キー検出 (" + key + ")";
                    break;
                }
                seenChKeys[currentCh].insert(key);

                if (key == "name" && !val.empty()) {
                    meta[currentCh].name = val;
                } else if (key == "unit" && !val.empty()) {
                    meta[currentCh].unit = val;
                }
            }
        }
    }

    if (!mismatchDetected) {
        if (header.version == DLOG_FORMAT_VERSION) {
            if (!hasDlogMetadataSection) {
                mismatchDetected = true;
                mismatchReason = "v2メタデータセクション欠落 ([DLOG_METADATA])";
            } else {
                const char* requiredKeys[] = {"magic", "version", "channels", "native_rate", "start_timestamp_utc"};
                for (const char* reqKey : requiredKeys) {
                    if (seenMetaKeys.count(reqKey) == 0) {
                        mismatchDetected = true;
                        mismatchReason = std::string("必須メタデータキー欠落: ") + reqKey;
                        break;
                    }
                }
            }
        } else if (hasDlogMetadataSection && seenMetaKeys.count("channels") == 0) {
            mismatchDetected = true;
            mismatchReason = "必須メタデータキー欠落: channels";
        }
    }

    if (mismatchDetected) {
        if (outWarning) {
            *outWarning = "メタデータ不整合 (別記録の.metaを検出: " + mismatchReason + ") - 既定値フォールバック";
        }
        std::vector<ChannelMetadata> fallback(channelCount);
        for (uint32_t i = 0; i < channelCount; ++i) {
            fallback[i].name = "CH" + std::to_string(i + 1);
            fallback[i].unit = "V";
        }
        return fallback;
    }

    return meta;
}


bool DlogReader::ValidateHeader(const DLogFileHeader& header, LocalizedError& outError) {
    outError.Clear();
    if (std::memcmp(header.magic, "DLOG", 4) != 0) {
        outError = MakeError(StringId::ErrInvalidMagic);
        return false;
    }
    // v1は既存配布サンプルの読込に限り許容し、新規出力はv2とする。
    if (header.version != 1 && header.version != DLOG_FORMAT_VERSION) {
        outError = MakeError(StringId::ErrUnsupportedVersion, std::to_string(header.version));
        return false;
    }
    if (header.headerSize != sizeof(DLogFileHeader)) {
        outError = MakeError(StringId::ErrInvalidHeaderSize);
        return false;
    }
    if (header.channels == 0 || header.channels > MAX_AUDIO_CHANNELS) {
        outError = MakeError(StringId::ErrInvalidChannels, std::to_string(header.channels));
        return false;
    }
    if (header.sampleFormat != static_cast<uint32_t>(SampleFormatType::Float32)) {
        outError = MakeError(StringId::ErrUnsupportedSampleFormat);
        return false;
    }
    if (header.sampleRate == 0 || header.nativeRate == 0 || header.decimationFactor == 0) {
        outError = MakeError(StringId::ErrInvalidSampleRateOrDecimation);
        return false;
    }
    if (std::isnan(header.fullScaleVoltage) || std::isinf(header.fullScaleVoltage) || header.fullScaleVoltage <= 0.0) {
        outError = MakeError(StringId::ErrInvalidFullScaleVoltage);
        return false;
    }
    return true;
}

bool DlogReader::ValidateHeader(const DLogFileHeader& header, std::string& errorMsg) {
    LocalizedError err;
    bool ok = ValidateHeader(header, err);
    errorMsg = err.FormatCurrent();
    return ok;
}

DlogReader::DlogReader() = default;

DlogReader::~DlogReader() {
    Close();
}

bool DlogReader::Open(const std::string& filePath) {
    Close();
    m_lastErrorInfo.Clear();

    const auto fail = [this](const LocalizedError& err) {
        m_lastErrorInfo = err;
        Close();
        return false;
    };

    if (filePath.find(".partial") != std::string::npos) {
        return fail(MakeError(StringId::ErrFileOpenFailed, filePath));
    }

    try {
        m_fileStream.open(std::filesystem::u8path(filePath), std::ios::in | std::ios::binary);
        if (!m_fileStream.is_open()) {
            return fail(MakeError(StringId::ErrFileOpenFailed, filePath));
        }

        // ファイルサイズ取得
        m_fileStream.seekg(0, std::ios::end);
        const std::streampos endPos = m_fileStream.tellg();
        if (endPos < 0) {
            return fail(MakeError(StringId::ErrFileOpenFailed, filePath));
        }
        m_fileSizeBytes = static_cast<uint64_t>(endPos);
        m_fileStream.seekg(0, std::ios::beg);

        if (m_fileSizeBytes < sizeof(DLogFileHeader)) {
            return fail(MakeError(StringId::ErrFileSmallerThanHeader));
        }

        // ヘッダ読み込み
        m_fileStream.read(reinterpret_cast<char*>(&m_header), sizeof(m_header));
        if (m_fileStream.gcount() != static_cast<std::streamsize>(sizeof(m_header))) {
            return fail(MakeError(StringId::ErrHeaderReadFailed));
        }

        LocalizedError validErr;
        if (!ValidateHeader(m_header, validErr)) {
            return fail(validErr);
        }

        const uint64_t dataBytes = m_fileSizeBytes - sizeof(DLogFileHeader);
        const uint64_t bytesPerFrame = static_cast<uint64_t>(m_header.channels) * sizeof(float);
        if (dataBytes % bytesPerFrame != 0) {
            return fail(MakeError(StringId::ErrPartialFrameDiscarded, std::to_string(dataBytes % bytesPerFrame)));
        }
        m_totalFrames = dataBytes / bytesPerFrame;
        if (m_totalFrames == 0) {
            return fail(MakeError(StringId::ErrZeroFramesInFile));
        }

        const double effectiveRate = GetDLogEffectiveSampleRate(m_header);
        if (!(effectiveRate > 0.0) || !std::isfinite(effectiveRate)) {
            return fail(MakeError(StringId::ErrInvalidSampleRateOrDecimation));
        }
        m_durationSec = static_cast<double>(m_totalFrames) / effectiveRate;
        m_currentFrameIndex = 0;

        m_channelMetadata = LoadCompanionMeta(filePath, m_header, &m_metaWarning);

        return true;
    } catch (const std::exception& ex) {
        LocalizedError err = MakeError(StringId::ErrConversionException, ex.what());
        return fail(err);
    } catch (...) {
        return fail(MakeError(StringId::ErrUnknown));
    }
}

void DlogReader::Close() {
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
    m_fileSizeBytes = 0;
    m_totalFrames = 0;
    m_durationSec = 0.0;
    m_currentFrameIndex = 0;
    m_metaWarning.clear();
}


bool DlogReader::SeekToFrame(uint64_t frameIndex) {
    if (!IsOpen() || frameIndex > m_totalFrames) return false;

    const uint64_t bytesPerFrame = static_cast<uint64_t>(m_header.channels) * sizeof(float);
    const uint64_t byteOffset = sizeof(DLogFileHeader) + frameIndex * bytesPerFrame;
    if (byteOffset > m_fileSizeBytes ||
        byteOffset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    m_fileStream.clear();
    std::streampos pos = static_cast<std::streamoff>(byteOffset);
    m_fileStream.seekg(pos, std::ios::beg);
    if (!m_fileStream.good()) return false;

    m_currentFrameIndex = frameIndex;
    return true;
}

bool DlogReader::ReadNextChunk(std::vector<float>& buffer, size_t maxFrames, size_t& framesRead) {
    framesRead = 0;
    if (!IsOpen() || m_currentFrameIndex >= m_totalFrames) {
        return false;
    }

    size_t framesToRead = std::min<uint64_t>(maxFrames, m_totalFrames - m_currentFrameIndex);
    if (framesToRead == 0) {
        return false;
    }

    if (framesToRead > std::numeric_limits<size_t>::max() / static_cast<size_t>(m_header.channels)) {
        m_lastErrorInfo = MakeError(StringId::ErrChunkSizeTooLarge);
        return false;
    }
    size_t samplesToRead = framesToRead * static_cast<size_t>(m_header.channels);
    if (buffer.size() < samplesToRead) {
        buffer.resize(samplesToRead);
    }

    size_t bytesToRead = samplesToRead * sizeof(float);
    if (bytesToRead > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        m_lastErrorInfo = MakeError(StringId::ErrReadPastEnd);
        return false;
    }
    m_fileStream.read(reinterpret_cast<char*>(buffer.data()), bytesToRead);
    std::streamsize actualBytes = m_fileStream.gcount();
    const size_t bytesPerFrame = static_cast<size_t>(m_header.channels) * sizeof(float);
    framesRead = static_cast<size_t>(actualBytes) / bytesPerFrame;

    if (actualBytes != static_cast<std::streamsize>(bytesToRead)) {
        m_lastErrorInfo = MakeError(StringId::ErrPcmReadIncomplete);
        return false;
    }

    m_currentFrameIndex += framesRead;
    return framesRead > 0;
}

bool DlogReader::ReadFrames(uint64_t startFrame, size_t frameCount, std::vector<float>& buffer, size_t& framesRead) {
    if (!SeekToFrame(startFrame)) {
        framesRead = 0;
        return false;
    }
    return ReadNextChunk(buffer, frameCount, framesRead);
}

bool DlogReader::InspectFile(const std::string& filePath, BatchItem& item) {
    item.inputFilePath = filePath;
    item.isValidHeader = false;

    // ファイル名抽出
    size_t slash = filePath.find_last_of("/\\");
    item.fileNameOnly = (slash == std::string::npos) ? filePath : filePath.substr(slash + 1);

    DlogReader reader;
    if (!reader.Open(filePath)) {
        item.status = BatchStatus::Error;
        item.error = reader.GetLastErrorInfo();
        item.errorMessage = reader.GetLastError().empty()
            ? "無効なDLOGファイルまたは開けません"
            : reader.GetLastError();
        return false;
    }

    item.header = reader.GetHeader();
    item.channelMetadata = reader.GetChannelMetadata();
    item.fileSizeBytes = reader.GetFileSize();
    item.totalFrames = reader.GetTotalFrames();
    item.durationSec = reader.GetDurationSec();
    item.isValidHeader = true;
    item.status = BatchStatus::Pending;

    return true;
}

bool DlogReader::RecoverPartialFile(const std::string& partialPath,
                                   std::string& outRecoveredPath,
                                   LocalizedError& outError,
                                   const RecoveryProgressCallback& progressCallback) {
    outError.Clear();
    std::error_code ec;
    std::filesystem::path p = std::filesystem::u8path(partialPath);
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) {
        outError = MakeError(StringId::ErrFileNotFound, partialPath);
        return false;
    }

    uint64_t fileSize = std::filesystem::file_size(p, ec);
    if (ec || fileSize < sizeof(DLogFileHeader)) {
        outError = MakeError(StringId::ErrFileSmallerThanHeader);
        return false;
    }

    // 読み込み用ストリームを開く（入力元は完全なリードオンリー）
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) {
        outError = MakeError(StringId::ErrFileOpenFailed, partialPath);
        return false;
    }

    // ヘッダ読み込みと検証
    DLogFileHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (in.gcount() != sizeof(header)) {
        outError = MakeError(StringId::ErrHeaderReadFailed);
        return false;
    }

    if (!ValidateHeader(header, outError)) {
        return false;
    }

    const uint64_t bytesPerFrame = static_cast<uint64_t>(header.channels) * sizeof(float);
    uint64_t dataBytes = fileSize - sizeof(DLogFileHeader);
    uint64_t validFrames = dataBytes / bytesPerFrame;
    if (validFrames == 0) {
        outError = MakeError(StringId::ErrZeroFramesInFile);
        return false;
    }

    uint64_t alignedDataBytes = validFrames * bytesPerFrame;

    // 確定後のファイル名を決定 (非破壊: 元ファイルを変更せず、復旧先既存時の安全性を確保)
    std::string filename = p.filename().u8string();
    std::string cleanName = filename;
    size_t partialPos = cleanName.rfind(".partial");
    if (partialPos != std::string::npos) {
        cleanName = cleanName.substr(0, partialPos);
    }
    if (cleanName.empty() || cleanName == filename) {
        cleanName = filename + ".recovered.bin";
    }

    std::filesystem::path targetDir = p.parent_path();
    std::filesystem::path targetPath = targetDir / cleanName;

    // 復旧先が既に存在する場合は、上書きせず衝突回避枝番を付与
    if (std::filesystem::exists(targetPath, ec)) {
        std::string stem = targetPath.stem().u8string();
        std::string ext = targetPath.extension().u8string();
        for (int suffix = 1; suffix < 10000; ++suffix) {
            std::filesystem::path candidate = targetDir / (stem + "_recovered_" + std::to_string(suffix) + ext);
            ec.clear();
            if (!std::filesystem::exists(candidate, ec)) {
                targetPath = candidate;
                break;
            }
        }
    }

    // 書込先の一時ファイルを作成 (コピー方式)
    std::filesystem::path tempRecPath = targetPath;
    tempRecPath += L".rec_temp";
    std::error_code tempEc;
    for (int suffix = 1; std::filesystem::exists(tempRecPath, tempEc); ++suffix) {
        tempEc.clear();
        tempRecPath = targetPath;
        tempRecPath += L".rec_temp." + std::to_wstring(suffix);
    }
    if (tempEc) {
        outError = MakeError(StringId::ErrTempFileCreationFailed, tempRecPath.u8string());
        return false;
    }

    std::ofstream out(tempRecPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        outError = MakeError(StringId::ErrTempFileCreationFailed, tempRecPath.u8string());
        return false;
    }

    // ヘッダ書き込み
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!out.good()) {
        out.close();
        std::filesystem::remove(tempRecPath, ec);
        outError = MakeError(StringId::ErrHeaderWriteFailed);
        return false;
    }

    // PCMデータをアライメント済みの境界までチャンクコピー (元の.partialは一切変更しない)
    constexpr size_t COPY_CHUNK = 64 * 1024;
    std::vector<char> copyBuf(COPY_CHUNK);
    uint64_t remainingBytes = alignedDataBytes;
    while (remainingBytes > 0) {
        size_t toRead = static_cast<size_t>(std::min<uint64_t>(remainingBytes, COPY_CHUNK));
        in.read(copyBuf.data(), toRead);
        std::streamsize bytesRead = in.gcount();
        if (bytesRead <= 0) break;
        out.write(copyBuf.data(), bytesRead);
        if (!out.good()) {
            out.close();
            in.close();
            std::filesystem::remove(tempRecPath, ec);
            outError = MakeError(StringId::ErrDataWriteFailed);
            return false;
        }
        remainingBytes -= static_cast<uint64_t>(bytesRead);

        if (progressCallback && alignedDataBytes > 0) {
            double ratio = static_cast<double>(alignedDataBytes - remainingBytes) / static_cast<double>(alignedDataBytes);
            if (!progressCallback(ratio)) {
                out.close();
                in.close();
                std::filesystem::remove(tempRecPath, ec);
                outError = MakeError(StringId::ErrUserCanceled);
                return false;
            }
        }
    }
    out.flush();
    if (!out.good() || remainingBytes != 0) {
        out.close();
        in.close();
        std::filesystem::remove(tempRecPath, ec);
        outError = MakeError(StringId::ErrFlushFailed);
        return false;
    }
    out.close();
    in.close();

    // 一時ファイルから確定ファイルへ移動
    ec.clear();
    std::filesystem::rename(tempRecPath, targetPath, ec);
    if (ec) {
        std::filesystem::remove(tempRecPath, ec);
        outError = MakeError(StringId::ErrRenameFailed, ec.message());
        return false;
    }

    // 作成した復旧ファイルの自己検証
    DlogReader verifier;
    if (!verifier.Open(targetPath.u8string())) {
        std::filesystem::remove(targetPath, ec);
        outError = MakeError(StringId::ErrVerificationFailed, verifier.GetLastError());
        return false;
    }
    verifier.Close();

    // コンパニオンメタデータ (.meta) の安全なコピー (検証に合格した場合のみコピー)
    std::filesystem::path sourceMeta = p;
    sourceMeta += ".meta";
    if (!std::filesystem::exists(sourceMeta, ec)) {
        sourceMeta = std::filesystem::u8path(partialPath + ".meta");
    }
    if (std::filesystem::exists(sourceMeta, ec)) {
        std::string metaWarn;
        auto verifiedMeta = LoadCompanionMeta(p.u8string(), header, &metaWarn);
        if (metaWarn.empty()) {
            std::filesystem::path targetMeta = targetPath;
            targetMeta += ".meta";
            std::filesystem::copy_file(sourceMeta, targetMeta, std::filesystem::copy_options::overwrite_existing, ec);
        }
    }

    outRecoveredPath = targetPath.u8string();
    return true;
}

bool DlogReader::RecoverPartialFile(const std::string& partialPath,
                                   std::string& outRecoveredPath,
                                   std::string& errorMsg,
                                   const RecoveryProgressCallback& progressCallback) {
    LocalizedError err;
    bool ok = RecoverPartialFile(partialPath, outRecoveredPath, err, progressCallback);
    errorMsg = err.FormatCurrent();
    return ok;
}


} // namespace dlog
