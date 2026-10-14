#include "batch_converter_worker.h"
#include <chrono>
#include <iostream>
#include <exception>
#include <set>
#include <map>
#include <iomanip>
#include <sstream>
#include <cctype>
#include <filesystem>
#include <algorithm>

namespace dlog {

constexpr size_t FRAMES_PER_CHUNK = 8192; // 8Kフレーム（約 128KB〜512KB / チャンク）

namespace {

std::string NormalizePathKey(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path normalP = std::filesystem::weakly_canonical(p, ec);
    if (ec) normalP = p.lexically_normal();
    std::string key = normalP.u8string();
#ifdef _WIN32
    for (char& c : key) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '/') c = '\\';
    }
#endif
    return key;
}

} // namespace

ConversionPlan PlanConversion(const BatchItem& item, const ConversionOptions& options) {
    ConversionPlan plan;

    // 1. 出力ディレクトリ決定
    if (options.useSameDirAsInput) {
        size_t slash = item.inputFilePath.find_last_of("/\\");
        plan.baseOutputDir = (slash != std::string::npos) ? item.inputFilePath.substr(0, slash) : ".";
    } else {
        plan.baseOutputDir = options.customOutputDir.empty() ? "." : options.customOutputDir;
    }

    // 2. 拡張子を除いたベース名
    std::string baseName = item.fileNameOnly;
    size_t dot = baseName.find_last_of('.');
    if (dot != std::string::npos) {
        plan.baseFileName = baseName.substr(0, dot);
    } else {
        plan.baseFileName = baseName;
    }

    // 3. 拡張子
    plan.extension = (options.delimiter == '\t') ? ".tsv" : ".csv";

    // 4. 開始・終了フレーム計算
    uint64_t totalFrames = item.totalFrames;
    plan.startFrame = 0;
    plan.endFrame = totalFrames;
    if (options.enableTimeRange && item.isValidHeader) {
        double rate = GetDLogEffectiveSampleRate(item.header);
        if (rate > 0.0) {
            const double requestedStart = std::max(0.0, options.startTimeSec * rate);
            plan.startFrame = (requestedStart < static_cast<double>(totalFrames))
                ? static_cast<uint64_t>(requestedStart) : totalFrames;
            if (options.endTimeSec > options.startTimeSec) {
                const double requestedEnd = options.endTimeSec * rate;
                plan.endFrame = (requestedEnd < static_cast<double>(totalFrames))
                    ? static_cast<uint64_t>(requestedEnd) : totalFrames;
            }
        }
    }
    plan.targetFrames = (plan.endFrame > plan.startFrame) ? (plan.endFrame - plan.startFrame) : 0;

    // 5. 行数および分割計算
    int step = std::max(1, options.decimationStep);
    plan.estimatedOutputRows = (plan.targetFrames + step - 1) / step;

    plan.willSplit = options.enableSplitLines && options.maxLinesPerFile > 0 &&
                     (plan.estimatedOutputRows > options.maxLinesPerFile);
    plan.totalParts = 1;
    if (options.enableSplitLines && options.maxLinesPerFile > 0 && plan.estimatedOutputRows > 0) {
        plan.totalParts = (plan.estimatedOutputRows + options.maxLinesPerFile - 1) / options.maxLinesPerFile;
        if (plan.totalParts == 0) plan.totalParts = 1;
    }
    plan.branchDigits = std::max(2, static_cast<int>(std::to_string(plan.totalParts).length()));

    // 6. 出力ファイル一覧
    std::filesystem::path dirPath = std::filesystem::u8path(plan.baseOutputDir);
    if (!plan.willSplit) {
        std::filesystem::path p = dirPath / (plan.baseFileName + plan.extension);
        plan.plannedOutputFiles.push_back(p.u8string());
    } else {
        for (uint64_t part = 1; part <= plan.totalParts; ++part) {
            char branchBuf[32];
            std::snprintf(branchBuf, sizeof(branchBuf), "-%0*lld", plan.branchDigits, static_cast<long long>(part));
            std::filesystem::path p = dirPath / (plan.baseFileName + branchBuf + plan.extension);
            plan.plannedOutputFiles.push_back(p.u8string());
        }
    }

    return plan;
}

BatchConverterWorker::BatchConverterWorker() = default;

BatchConverterWorker::~BatchConverterWorker() {
    Cancel();
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
}

bool BatchConverterWorker::CheckCollisions(const std::vector<BatchItem>& items,
                                           const ConversionOptions& options,
                                           std::string& errorMsg) {
    // 1. 全入力ファイルのキーを収集
    std::map<std::string, std::string> allInputKeys;
    for (const auto& item : items) {
        if (!item.isValidHeader) continue;
        std::string inKey = NormalizePathKey(std::filesystem::u8path(item.inputFilePath));
        allInputKeys[inKey] = item.inputFilePath;
    }

    // 2. 出力ファイルを計画し、衝突を検出
    // 出力キー -> 最初に計画した入力ファイルのパス
    std::map<std::string, std::string> claimedOutputs;

    for (const auto& item : items) {
        if (!item.isValidHeader) continue;

        ConversionPlan plan = PlanConversion(item, options);
        std::string currentInKey = NormalizePathKey(std::filesystem::u8path(item.inputFilePath));

        for (const auto& plannedPathStr : plan.plannedOutputFiles) {
            std::filesystem::path p = std::filesystem::u8path(plannedPathStr);
            std::string outKey = NormalizePathKey(p);

            // 全入力ファイルとの衝突検査
            auto inIt = allInputKeys.find(outKey);
            if (inIt != allInputKeys.end()) {
                errorMsg = "出力ファイルが入力元ファイルと重複しています: " + plannedPathStr;
                return false;
            }

            // バッチ内での出力同士の衝突検査
            auto claimIt = claimedOutputs.find(outKey);
            if (claimIt != claimedOutputs.end()) {
                std::string claimInKey = NormalizePathKey(std::filesystem::u8path(claimIt->second));
                if (claimInKey != currentInKey) {
                    errorMsg = "バッチ変換内で異なる入力ファイル同士の出力ファイル名重複（衝突）が検出されました: " + plannedPathStr;
                    return false;
                } else {
                    errorMsg = "同一入力ファイルの出力分割間で重複が検出されました: " + plannedPathStr;
                    return false;
                }
            }

            claimedOutputs[outKey] = item.inputFilePath;
        }
    }
    return true;
}


bool BatchConverterWorker::Start(const std::vector<BatchItem>& items, const ConversionOptions& options) {
    if (m_isRunning.load()) {
        return false;
    }

    std::string collisionErr;
    if (!CheckCollisions(items, options, collisionErr)) {
        return false;
    }

    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }

    {
        std::lock_guard<std::mutex> lock(m_itemsMutex);
        m_items = items;
        m_options = options;
        m_totalBytesAllFiles = 0;
        for (auto& it : m_items) {
            it.status = BatchStatus::Pending;
            it.progress = 0.0;
            it.outputFiles.clear();
            it.errorMessage.clear();
            m_totalBytesAllFiles += it.fileSizeBytes;
        }
    }

    m_cancelRequested.store(false);
    m_processedBytesAllFiles.store(0);
    m_currentFileIndex.store(0);
    m_currentFileProgress.store(0.0);
    m_overallProgress.store(0.0);
    m_bytesPerSec.store(0.0);
    m_estimatedRemainingSec.store(0.0);

    m_isRunning.store(true);
    try {
        m_thread = std::make_unique<std::thread>(&BatchConverterWorker::WorkerLoop, this);
    } catch (...) {
        m_isRunning.store(false);
        return false;
    }

    return true;
}

void BatchConverterWorker::Cancel() {
    m_cancelRequested.store(true);
}

BatchStats BatchConverterWorker::GetStats() const {
    BatchStats stats;
    stats.isRunning = m_isRunning.load(std::memory_order_relaxed);
    stats.currentFileIndex = m_currentFileIndex.load(std::memory_order_relaxed);
    stats.currentFileProgress = m_currentFileProgress.load(std::memory_order_relaxed);
    stats.overallProgress = m_overallProgress.load(std::memory_order_relaxed);
    stats.bytesPerSec = m_bytesPerSec.load(std::memory_order_relaxed);
    stats.estimatedRemainingSec = m_estimatedRemainingSec.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(m_itemsMutex);
    stats.totalFiles = m_items.size();
    size_t comp = 0;
    for (const auto& it : m_items) {
        if (it.status == BatchStatus::Completed) comp++;
    }
    stats.completedFiles = comp;

    return stats;
}

std::vector<BatchItem> BatchConverterWorker::GetItems() const {
    std::lock_guard<std::mutex> lock(m_itemsMutex);
    return m_items;
}

void BatchConverterWorker::WorkerLoop() {
    auto startTime = std::chrono::steady_clock::now();
    uint64_t accumulatedPrevFilesBytes = 0;

    size_t count = 0;
    {
        std::lock_guard<std::mutex> lock(m_itemsMutex);
        count = m_items.size();
    }

    for (size_t idx = 0; idx < count; ++idx) {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            for (size_t k = idx; k < count; ++k) {
                if (m_items[k].status == BatchStatus::Pending) {
                    m_items[k].status = BatchStatus::Canceled;
                }
            }
            break;
        }

        m_currentFileIndex.store(idx, std::memory_order_relaxed);
        m_currentFileProgress.store(0.0, std::memory_order_relaxed);

        BatchItem itemCopy;
        {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            m_items[idx].status = BatchStatus::Converting;
            itemCopy = m_items[idx];
        }

        bool success = false;
        try {
            success = ConvertSingleFile(itemCopy);
        } catch (const std::exception& ex) {
            itemCopy.errorMessage = std::string("変換処理中の例外: ") + ex.what();
        } catch (...) {
            itemCopy.errorMessage = "変換処理中に不明な例外が発生しました";
        }

        {
            std::lock_guard<std::mutex> lock(m_itemsMutex);
            m_items[idx] = itemCopy;
            if (m_cancelRequested.load(std::memory_order_relaxed) && !success) {
                m_items[idx].status = BatchStatus::Canceled;
            } else {
                m_items[idx].status = success ? BatchStatus::Completed : BatchStatus::Error;
            }
        }

        accumulatedPrevFilesBytes += itemCopy.fileSizeBytes;
        m_processedBytesAllFiles.store(accumulatedPrevFilesBytes, std::memory_order_relaxed);

        // 速度と残り時間計算
        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(now - startTime).count();
        if (elapsedSec > 0.5 && accumulatedPrevFilesBytes > 0) {
            double speed = static_cast<double>(accumulatedPrevFilesBytes) / elapsedSec;
            m_bytesPerSec.store(speed, std::memory_order_relaxed);

            if (m_totalBytesAllFiles > accumulatedPrevFilesBytes && speed > 0.0) {
                double remainingBytes = static_cast<double>(m_totalBytesAllFiles - accumulatedPrevFilesBytes);
                m_estimatedRemainingSec.store(remainingBytes / speed, std::memory_order_relaxed);
            }
        }

        if (m_totalBytesAllFiles > 0) {
            double overall = static_cast<double>(accumulatedPrevFilesBytes) / static_cast<double>(m_totalBytesAllFiles);
            m_overallProgress.store(std::min(1.0, overall), std::memory_order_relaxed);
        }
    }

    m_isRunning.store(false);
}

bool BatchConverterWorker::ConvertSingleFile(BatchItem& item) {
    DlogReader reader;
    if (!reader.Open(item.inputFilePath)) {
        item.errorMessage = reader.GetLastError().empty()
            ? "ファイルをオープンできません"
            : reader.GetLastError();
        return false;
    }

    const DLogFileHeader& header = reader.GetHeader();
    uint64_t totalFrames = reader.GetTotalFrames();
    if (totalFrames == 0) {
        item.errorMessage = "データフレームが空です";
        return false;
    }

    // 共通の計画を取得 (DATA-002)
    ConversionPlan plan = PlanConversion(item, m_options);

    if (m_options.enableTimeRange) {
        if (plan.startFrame >= plan.endFrame) {
            item.errorMessage = "時間範囲指定が不正です";
            return false;
        }
        if (!reader.SeekToFrame(plan.startFrame)) {
            item.errorMessage = "指定した開始位置へシークできません";
            return false;
        }
    }

    uint64_t targetFrames = plan.targetFrames;
    uint64_t startFrame = plan.startFrame;
    int step = std::max(1, m_options.decimationStep);

    ConversionOptions fileOptions = m_options;
    if (fileOptions.channelMetadata.empty() && !item.channelMetadata.empty()) {
        fileOptions.channelMetadata = item.channelMetadata;
    }

    CsvWriter writer;
    if (!writer.Initialize(plan.baseOutputDir, plan.baseFileName, fileOptions, header, plan.willSplit, plan.totalParts, plan.branchDigits)) {
        item.errorMessage = "CSV出力ファイルを作成できません";
        return false;
    }


    std::vector<float> chunkBuffer;
    uint64_t processedFramesInTarget = 0;
    int channelCount = static_cast<int>(header.channels);

    while (processedFramesInTarget < targetFrames) {
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            writer.Abort();
            item.outputFiles = writer.GetGeneratedFiles();
            item.convertedFrames = processedFramesInTarget;
            item.progress = static_cast<double>(processedFramesInTarget) /
                            static_cast<double>(targetFrames);
            item.errorMessage = "ユーザー操作により中断されました";
            return false;
        }

        size_t framesToReadThisTime = std::min<uint64_t>(FRAMES_PER_CHUNK, targetFrames - processedFramesInTarget);
        size_t framesRead = 0;
        if (!reader.ReadNextChunk(chunkBuffer, framesToReadThisTime, framesRead) || framesRead == 0) {
            item.errorMessage = reader.GetLastError().empty()
                ? "DLOGデータを最後まで読み込めません"
                : reader.GetLastError();
            writer.Abort();
            item.outputFiles = writer.GetGeneratedFiles();
            item.convertedFrames = processedFramesInTarget;
            item.progress = static_cast<double>(processedFramesInTarget) /
                            static_cast<double>(targetFrames);
            return false;
        }

        for (size_t f = 0; f < framesRead; ++f) {
            uint64_t currentAbsoluteFrame = startFrame + processedFramesInTarget + f;
            if ((processedFramesInTarget + f) % static_cast<uint64_t>(step) == 0) {
                const float* samplesPtr = &chunkBuffer[f * channelCount];
                if (!writer.WriteRow(currentAbsoluteFrame, samplesPtr, channelCount)) {
                    item.errorMessage = writer.GetLastError().empty()
                        ? "CSVデータを書き込めません"
                        : writer.GetLastError();
                    writer.Abort();
                    item.outputFiles = writer.GetGeneratedFiles();
                    item.convertedFrames = processedFramesInTarget + f;
                    item.progress = static_cast<double>(processedFramesInTarget + f) /
                                    static_cast<double>(targetFrames);
                    return false;
                }
            }
        }

        processedFramesInTarget += framesRead;

        double fileProg = static_cast<double>(processedFramesInTarget) / static_cast<double>(targetFrames);
        m_currentFileProgress.store(std::min(1.0, fileProg), std::memory_order_relaxed);
    }

    if (processedFramesInTarget != targetFrames) {
        item.errorMessage = "DLOGデータが予定フレーム数より前に終了しました";
        writer.Abort();
        item.outputFiles = writer.GetGeneratedFiles();
        item.convertedFrames = processedFramesInTarget;
        item.progress = static_cast<double>(processedFramesInTarget) /
                        static_cast<double>(targetFrames);
        return false;
    }

    if (!writer.Finish()) {
        item.errorMessage = writer.GetLastError().empty()
            ? "CSVファイルを確定できません"
            : writer.GetLastError();
        item.outputFiles = writer.GetGeneratedFiles();
        item.convertedFrames = processedFramesInTarget;
        item.progress = static_cast<double>(processedFramesInTarget) /
                        static_cast<double>(targetFrames);
        return false;
    }
    item.convertedFrames = processedFramesInTarget;
    item.outputFiles = writer.GetGeneratedFiles();
    item.progress = 1.0;

    return true;
}

} // namespace dlog
