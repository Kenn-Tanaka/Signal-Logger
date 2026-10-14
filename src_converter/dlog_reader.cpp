#include "dlog_reader.h"
#include <cstring>
#include <iostream>
#include <filesystem>
#include <cmath>
#include <limits>

namespace dlog {

std::vector<ChannelMetadata> DlogReader::LoadCompanionMeta(const std::string& filePath,
                                                          const DLogFileHeader& header,
                                                          std::string* outWarning) {
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

    while (std::getline(in, line)) {
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        line = line.substr(start);

        if (line.rfind("[DLOG_METADATA]", 0) == 0) {
            inMetadataSection = true;
            currentCh = -1;
            continue;
        }

        if (line.rfind("[CH", 0) == 0) {
            inMetadataSection = false;
            size_t closeBracket = line.find(']');
            if (closeBracket != std::string::npos) {
                std::string numStr = line.substr(3, closeBracket - 3);
                try {
                    currentCh = std::stoi(numStr) - 1;
                } catch (...) {
                    currentCh = -1;
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

                if (key == "magic") {
                    if (val != "DLOG") {
                        mismatchDetected = true;
                        mismatchReason = "マジックナンバー不一致 (" + val + " != DLOG)";
                    }
                } else if (key == "version") {
                    try {
                        uint32_t ver = static_cast<uint32_t>(std::stoul(val));
                        if (ver != header.version) {
                            mismatchDetected = true;
                            mismatchReason = "フォーマットバージョン不一致 (" + std::to_string(ver) + " != " + std::to_string(header.version) + ")";
                        }
                    } catch (...) {
                        mismatchDetected = true;
                        mismatchReason = "versionパース失敗";
                    }
                } else if (key == "channels") {
                    try {
                        uint32_t ch = static_cast<uint32_t>(std::stoul(val));
                        if (ch != channelCount) {
                            mismatchDetected = true;
                            mismatchReason = "チャネル数不一致 (" + std::to_string(ch) + " != " + std::to_string(channelCount) + ")";
                        }
                    } catch (...) {
                        mismatchDetected = true;
                        mismatchReason = "channelsパース失敗";
                    }
                } else if (key == "native_rate") {
                    try {
                        uint32_t nr = static_cast<uint32_t>(std::stoul(val));
                        if (header.nativeRate != 0 && nr != header.nativeRate) {
                            mismatchDetected = true;
                            mismatchReason = "ネイティブサンプリング周波数不一致 (" + std::to_string(nr) + " != " + std::to_string(header.nativeRate) + ")";
                        }
                    } catch (...) {
                        mismatchDetected = true;
                        mismatchReason = "native_rateパース失敗";
                    }
                } else if (key == "decimation_factor") {
                    try {
                        uint32_t df = static_cast<uint32_t>(std::stoul(val));
                        if (header.decimationFactor != 0 && df != header.decimationFactor) {
                            mismatchDetected = true;
                            mismatchReason = "デシメーション間引き率不一致 (" + std::to_string(df) + " != " + std::to_string(header.decimationFactor) + ")";
                        }
                    } catch (...) {
                        mismatchDetected = true;
                        mismatchReason = "decimation_factorパース失敗";
                    }
                } else if (key == "start_timestamp_utc") {
                    try {
                        int64_t ts = std::stoll(val);
                        if (header.startTimestampUtc != 0 && ts != header.startTimestampUtc) {
                            mismatchDetected = true;
                            mismatchReason = "記録開始時刻不一致 (" + std::to_string(ts) + " != " + std::to_string(header.startTimestampUtc) + ")";
                        }
                    } catch (...) {
                        mismatchDetected = true;
                        mismatchReason = "start_timestamp_utcパース失敗";
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
                if (key == "name" && !val.empty()) {
                    meta[currentCh].name = val;
                } else if (key == "unit" && !val.empty()) {
                    meta[currentCh].unit = val;
                }
            }
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


DlogReader::DlogReader() = default;

DlogReader::~DlogReader() {
    Close();
}

bool DlogReader::Open(const std::string& filePath) {
    Close();
    m_lastError.clear();

    const auto fail = [this](const std::string& message) {
        m_lastError = message;
        Close();
        return false;
    };

    if (filePath.find(".partial") != std::string::npos) {
        return fail("書き込み中の一時ファイル (.partial) は対象外です");
    }

    try {
        m_fileStream.open(std::filesystem::u8path(filePath), std::ios::in | std::ios::binary);
        if (!m_fileStream.is_open()) {
            return fail("ファイルを開けません");
        }

        // ファイルサイズ取得
        m_fileStream.seekg(0, std::ios::end);
        const std::streampos endPos = m_fileStream.tellg();
        if (endPos < 0) {
            return fail("ファイルサイズを取得できません");
        }
        m_fileSizeBytes = static_cast<uint64_t>(endPos);
        m_fileStream.seekg(0, std::ios::beg);

        if (m_fileSizeBytes < sizeof(DLogFileHeader)) {
            return fail("64バイトのDLOGヘッダがありません");
        }

        // ヘッダ読み込み
        m_fileStream.read(reinterpret_cast<char*>(&m_header), sizeof(m_header));
        if (m_fileStream.gcount() != static_cast<std::streamsize>(sizeof(m_header))) {
            return fail("DLOGヘッダを最後まで読み込めません");
        }

        if (std::memcmp(m_header.magic, "DLOG", 4) != 0) {
            return fail("マジックナンバーがDLOGではありません");
        }
        // v1は既存配布サンプルの読込に限り許容し、新規出力はv2とする。
        if (m_header.version != 1 && m_header.version != DLOG_FORMAT_VERSION) {
            return fail("未対応のDLOGバージョンです");
        }
        if (m_header.headerSize != sizeof(DLogFileHeader)) {
            return fail("DLOGヘッダサイズが64バイトではありません");
        }
        if (m_header.channels == 0 || m_header.channels > MAX_AUDIO_CHANNELS) {
            return fail("チャネル数が1～8の範囲外です");
        }
        if (m_header.sampleFormat != static_cast<uint32_t>(SampleFormatType::Float32)) {
            return fail("Float32以外のサンプル形式には対応していません");
        }
        if (m_header.sampleRate == 0 || m_header.nativeRate == 0 || m_header.decimationFactor == 0) {
            return fail("サンプルレートまたはデシメーション係数が不正です");
        }
        if (std::isnan(m_header.fullScaleVoltage) || std::isinf(m_header.fullScaleVoltage) || m_header.fullScaleVoltage <= 0.0) {
            return fail("フルスケール電圧が不正です");
        }

        const uint64_t dataBytes = m_fileSizeBytes - sizeof(DLogFileHeader);
        const uint64_t bytesPerFrame = static_cast<uint64_t>(m_header.channels) * sizeof(float);
        if (dataBytes % bytesPerFrame != 0) {
            return fail("データ部の末尾がフレーム境界に揃っていません");
        }
        m_totalFrames = dataBytes / bytesPerFrame;
        if (m_totalFrames == 0) {
            return fail("データが空です（0フレーム）");
        }

        const double effectiveRate = GetDLogEffectiveSampleRate(m_header);
        if (!(effectiveRate > 0.0) || !std::isfinite(effectiveRate)) {
            return fail("実効サンプルレートが不正です");
        }
        m_durationSec = static_cast<double>(m_totalFrames) / effectiveRate;
        m_currentFrameIndex = 0;

        m_channelMetadata = LoadCompanionMeta(filePath, m_header, &m_metaWarning);

        return true;
    } catch (const std::exception& ex) {
        return fail(std::string("DLOGファイル処理中の例外: ") + ex.what());
    } catch (...) {
        return fail("DLOGファイル処理中に不明な例外が発生しました");
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
        m_lastError = "読込チャンクサイズが大きすぎます";
        return false;
    }
    size_t samplesToRead = framesToRead * static_cast<size_t>(m_header.channels);
    if (buffer.size() < samplesToRead) {
        buffer.resize(samplesToRead);
    }

    size_t bytesToRead = samplesToRead * sizeof(float);
    if (bytesToRead > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        m_lastError = "読込要求サイズがストリーム上限を超えています";
        return false;
    }
    m_fileStream.read(reinterpret_cast<char*>(buffer.data()), bytesToRead);
    std::streamsize actualBytes = m_fileStream.gcount();
    const size_t bytesPerFrame = static_cast<size_t>(m_header.channels) * sizeof(float);
    framesRead = static_cast<size_t>(actualBytes) / bytesPerFrame;

    if (actualBytes != static_cast<std::streamsize>(bytesToRead)) {
        m_lastError = "PCMデータを最後まで読み込めません";
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
                                   std::string& errorMsg) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::u8path(partialPath);
    if (!std::filesystem::exists(p, ec) || !std::filesystem::is_regular_file(p, ec)) {
        errorMsg = "対象の .partial ファイルが見つかりません: " + partialPath;
        return false;
    }

    uint64_t fileSize = std::filesystem::file_size(p, ec);
    if (ec || fileSize < sizeof(DLogFileHeader)) {
        errorMsg = "ファイルサイズがDLOGヘッダ (64バイト) 未満です";
        return false;
    }

    // 読み込み用ストリームを開く（入力元は完全なリードオンリー）
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) {
        errorMsg = "ファイルを開けません (読み取り権限を確認してください)";
        return false;
    }

    // ヘッダ読み込みと検証
    DLogFileHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    if (in.gcount() != sizeof(header)) {
        errorMsg = "ヘッダを読み込めません";
        return false;
    }

    if (std::memcmp(header.magic, "DLOG", 4) != 0) {
        errorMsg = "マジックナンバーがDLOGではありません";
        return false;
    }
    if (header.version != 1 && header.version != DLOG_FORMAT_VERSION) {
        errorMsg = "フォーマットバージョンが未対応です (version=" + std::to_string(header.version) + ")";
        return false;
    }
    if (header.headerSize != sizeof(DLogFileHeader)) {
        errorMsg = "ヘッダサイズが不正です";
        return false;
    }
    if (header.channels == 0 || header.channels > MAX_AUDIO_CHANNELS) {
        errorMsg = "チャネル数が不正です (1～8)";
        return false;
    }
    if (header.sampleFormat != 0) {
        errorMsg = "サポートされていないサンプル形式です (Float32のみ対応)";
        return false;
    }
    if (header.sampleRate == 0 || header.nativeRate == 0 || header.decimationFactor == 0) {
        errorMsg = "サンプリング周波数またはデシメーション係数が不正です";
        return false;
    }

    const uint64_t bytesPerFrame = static_cast<uint64_t>(header.channels) * sizeof(float);
    uint64_t dataBytes = fileSize - sizeof(DLogFileHeader);
    uint64_t validFrames = dataBytes / bytesPerFrame;
    if (validFrames == 0) {
        errorMsg = "有効なPCMフレームが含まれていません (データ部が空です)";
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
        errorMsg = "復旧用一時ファイル名を決定できません";
        return false;
    }

    std::ofstream out(tempRecPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        errorMsg = "復旧先ファイルを作成できません (書き込み権限またはパスを確認してください): " + tempRecPath.u8string();
        return false;
    }

    // ヘッダ書き込み
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    if (!out.good()) {
        out.close();
        std::filesystem::remove(tempRecPath, ec);
        errorMsg = "復旧先ファイルへのヘッダ書き込みに失敗しました";
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
            std::filesystem::remove(tempRecPath, ec);
            errorMsg = "復旧データの書き込み中にI/Oエラーが発生しました";
            return false;
        }
        remainingBytes -= static_cast<uint64_t>(bytesRead);
    }
    out.flush();
    if (!out.good() || remainingBytes != 0) {
        out.close();
        std::filesystem::remove(tempRecPath, ec);
        errorMsg = "復旧データのフラッシュに失敗しました";
        return false;
    }
    out.close();
    in.close();

    // 一時ファイルから確定ファイルへ移動
    ec.clear();
    std::filesystem::rename(tempRecPath, targetPath, ec);
    if (ec) {
        std::filesystem::remove(tempRecPath, ec);
        errorMsg = "復旧確定ファイルへのリネームに失敗しました: " + ec.message();
        return false;
    }

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


} // namespace dlog
