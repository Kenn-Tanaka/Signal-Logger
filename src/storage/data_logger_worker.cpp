#include "storage/data_logger_worker.h"
#include <chrono>
#include <iostream>
#include <cstring>
#include <filesystem>
#include <cmath>
#include <cstddef>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#endif

namespace dlog {

DataLoggerWorker::DataLoggerWorker() {
    m_queue = std::make_unique<moodycamel::ReaderWriterQueue<AudioBlock>>(QUEUE_CAPACITY);
}

DataLoggerWorker::~DataLoggerWorker() {
    Stop();
}

bool DataLoggerWorker::Start(const LoggerConfig& config) {
    if (m_isRunning.load()) {
        return false;
    }

    const double targetRate = config.getTargetSampleRate();
    if (config.channels < 1 || config.channels > static_cast<int>(MAX_AUDIO_CHANNELS) ||
        config.nativeSampleRate <= 0.0 || !std::isfinite(config.nativeSampleRate) ||
        config.nativeSampleRate > static_cast<double>(std::numeric_limits<uint32_t>::max()) ||
        config.decimationFactor < 1 || config.framesPerBuffer == 0 ||
        config.framesPerBuffer > MAX_AUDIO_FRAMES ||
        targetRate < 0.5 || !std::isfinite(targetRate) ||
        targetRate > static_cast<double>(std::numeric_limits<uint32_t>::max()) ||
        config.sampleFormat != SampleFormatType::Float32 || config.outputFilePath.empty()) {
        m_hasDiskError.store(true, std::memory_order_relaxed);
        return false;
    }

    m_config = config;
    m_stopRequested.store(false);
    m_hasDiskError.store(false);
    m_totalFramesLogged.store(0);
    m_totalBytesWritten.store(0);
    m_bufferOverruns.store(0);
    int64_t initialStartMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    m_startTimeMs.store(initialStartMs, std::memory_order_relaxed);
    m_stopTimeMs = 0;
    m_lastWallClockDurationSec = 0.0;
    m_lastDataDurationSec = 0.0;

    // キューの初期化（残存データのクリア）
    m_queue = std::make_unique<moodycamel::ReaderWriterQueue<AudioBlock>>(QUEUE_CAPACITY);

    // バイナリファイルのオープン（非ASCII・日本語パス対応 & 親ディレクトリ自動生成）
    m_finalOutputPath = std::filesystem::u8path(m_config.outputFilePath);
    if (m_finalOutputPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(m_finalOutputPath.parent_path(), ec);
        if (ec) {
            m_hasDiskError.store(true, std::memory_order_relaxed);
            return false;
        }
    }

    // 最終ファイルは正常停止まで保持し、録音中は隣接する一時ファイルへ書き込む。
    // 以前の異常終了で残った partial は上書きしない。
    m_temporaryOutputPath = m_finalOutputPath;
    m_temporaryOutputPath += L".partial";
    std::error_code pathEc;
    for (unsigned int suffix = 1; std::filesystem::exists(m_temporaryOutputPath, pathEc); ++suffix) {
        pathEc.clear();
        m_temporaryOutputPath = m_finalOutputPath;
        m_temporaryOutputPath += L".partial." + std::to_wstring(suffix);
    }
    if (pathEc) {
        m_hasDiskError.store(true, std::memory_order_relaxed);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        m_fileStream.open(m_temporaryOutputPath, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!m_fileStream.is_open()) {
            m_hasDiskError.store(true, std::memory_order_relaxed);
            std::error_code ec;
            std::filesystem::remove(m_temporaryOutputPath, ec);
            return false;
        }

        // 固定長 64 バイトヘッダの構築と書き込み
        DLogFileHeader header;
        std::memcpy(header.magic, "DLOG", 4);
        header.version = DLOG_FORMAT_VERSION;
        header.headerSize = static_cast<uint32_t>(sizeof(DLogFileHeader));
        header.channels = static_cast<uint32_t>(m_config.channels);
        header.sampleRate = static_cast<uint32_t>(std::llround(targetRate));
        header.sampleFormat = static_cast<uint32_t>(m_config.sampleFormat);
        header.nativeRate = static_cast<uint32_t>(m_config.nativeSampleRate);
        header.decimationFactor = static_cast<uint32_t>(m_config.decimationFactor);
        header.startTimestampUtc = initialStartMs;
        header.fullScaleVoltage = (m_config.fullScaleVoltage > 0.0) ? m_config.fullScaleVoltage : 1.0;

        m_fileStream.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (!m_fileStream.good()) {
            m_hasDiskError.store(true, std::memory_order_relaxed);
            m_fileStream.close();
            std::error_code ec;
            std::filesystem::remove(m_temporaryOutputPath, ec);
            return false;
        }
        m_totalBytesWritten += sizeof(header);
    }

    // Windows スリープ・休止防止
    m_powerGuard.Acquire();

    m_isRunning.store(true);
    try {
        m_thread = std::make_unique<std::thread>(&DataLoggerWorker::WorkerLoop, this);
    } catch (...) {
        m_isRunning.store(false);
        m_powerGuard.Release();
        std::lock_guard<std::mutex> lock(m_streamMutex);
        m_fileStream.close();
        std::error_code ec;
        std::filesystem::remove(m_temporaryOutputPath, ec);
        m_hasDiskError.store(true, std::memory_order_relaxed);
        return false;
    }

    return true;
}

bool DataLoggerWorker::MarkCaptureStartedNow() {
    std::lock_guard<std::mutex> lock(m_streamMutex);
    if (!m_isRunning.load(std::memory_order_relaxed) || !m_fileStream.is_open() ||
        m_totalFramesLogged.load(std::memory_order_relaxed) != 0) {
        return false;
    }

    int64_t startMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    m_startTimeMs.store(startMs, std::memory_order_relaxed);

    m_fileStream.seekp(static_cast<std::streamoff>(offsetof(DLogFileHeader, startTimestampUtc)),
                       std::ios::beg);
    m_fileStream.write(reinterpret_cast<const char*>(&startMs), sizeof(startMs));
    m_fileStream.seekp(0, std::ios::end);
    m_fileStream.flush();
    if (!m_fileStream.good()) {
        m_hasDiskError.store(true, std::memory_order_relaxed);
        return false;
    }
    return true;
}

void DataLoggerWorker::Stop() {
    StopInternal(true);
}

void DataLoggerWorker::Abort() {
    StopInternal(false);
}

void DataLoggerWorker::StopInternal(bool commitOutput) {
    if (!m_isRunning.load()) {
        return;
    }

    m_stopRequested.store(true);
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    m_thread.reset();

    {
        std::lock_guard<std::mutex> lock(m_streamMutex);
        // 残っている全ブロックを完全にフラッシュ
        AudioBlock block;
        while (m_queue && m_queue->try_dequeue(block)) {
            if (!m_hasDiskError.load(std::memory_order_relaxed) && block.frameCount > 0 && m_fileStream.is_open()) {
                size_t bytes = block.frameCount * static_cast<size_t>(block.channels) * sizeof(float);
                m_fileStream.write(reinterpret_cast<const char*>(block.samples), bytes);
                if (!m_fileStream.good()) {
                    m_hasDiskError.store(true, std::memory_order_relaxed);
                } else {
                    m_totalFramesLogged += block.frameCount;
                    m_totalBytesWritten += bytes;
                }
            }
        }

        if (m_fileStream.is_open()) {
            m_fileStream.flush();
            if (!m_fileStream.good()) {
                m_hasDiskError.store(true, std::memory_order_relaxed);
            }
            m_fileStream.close();
        }
    }

    m_powerGuard.Release();
    m_isRunning.store(false);

    m_stopTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t startMs = m_startTimeMs.load(std::memory_order_relaxed);
    m_lastWallClockDurationSec = (m_stopTimeMs > startMs)
        ? static_cast<double>(m_stopTimeMs - startMs) / 1000.0
        : 0.0;
    double targetFs = m_config.getTargetSampleRate();
    m_lastDataDurationSec = (targetFs > 0.0)
        ? static_cast<double>(m_totalFramesLogged.load(std::memory_order_relaxed)) / targetFs
        : 0.0;

    if (commitOutput && !m_hasDiskError.load(std::memory_order_relaxed)) {
        if (!FinalizeOutput()) {
            m_hasDiskError.store(true, std::memory_order_relaxed);
        }
    } else if (!commitOutput) {
        std::error_code ec;
        std::filesystem::remove(m_temporaryOutputPath, ec);
    }
}

bool DataLoggerWorker::FinalizeOutput() {
    if (m_temporaryOutputPath.empty() || m_finalOutputPath.empty()) {
        return false;
    }

#ifdef _WIN32
    std::error_code existsEc;
    const bool targetExists = std::filesystem::exists(m_finalOutputPath, existsEc);
    if (existsEc) {
        return false;
    }
    if (targetExists) {
        if (ReplaceFileW(m_finalOutputPath.c_str(), m_temporaryOutputPath.c_str(), nullptr,
                         REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != 0) {
            return true;
        }
        // フォールバック: 一時的なファイルロックや仮想ドライブ環境でのリトライと置換移動
        for (int retry = 0; retry < 5; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (ReplaceFileW(m_finalOutputPath.c_str(), m_temporaryOutputPath.c_str(), nullptr,
                             REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != 0) {
                return true;
            }
            if (MoveFileExW(m_temporaryOutputPath.c_str(), m_finalOutputPath.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
                return true;
            }
        }
        return false;
    }
    return MoveFileExW(m_temporaryOutputPath.c_str(), m_finalOutputPath.c_str(),
                       MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code ec;
    std::filesystem::rename(m_temporaryOutputPath, m_finalOutputPath, ec);
    return !ec;
#endif
}

bool DataLoggerWorker::EnqueueBlock(const AudioBlock& block) {
    if (!m_isRunning.load(std::memory_order_relaxed)) {
        return false;
    }

    if (block.channels != m_config.channels || block.channels < 1 ||
        block.channels > static_cast<int>(MAX_AUDIO_CHANNELS) ||
        block.frameCount > MAX_AUDIO_FRAMES) {
        m_bufferOverruns.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool success = m_queue->try_enqueue(block);
    if (!success) {
        // キュー溢れ警告（データ欠落防止のためカウントアップ）
        m_bufferOverruns.fetch_add(1, std::memory_order_relaxed);
    }
    return success;
}

void DataLoggerWorker::WorkerLoop() {
    // スレッド優先度を通常より高めに設定（I/O遅延耐性向上）
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif

    AudioBlock block;

    while (!m_stopRequested.load(std::memory_order_relaxed)) {
        bool hasData = false;
        while (m_queue->try_dequeue(block)) {
            hasData = true;
            if (m_hasDiskError.load(std::memory_order_relaxed)) {
                // ディスクエラー発生後はさらなる書き込みを抑止
                continue;
            }

            if (block.frameCount > 0) {
                std::lock_guard<std::mutex> lock(m_streamMutex);
                if (m_fileStream.is_open()) {
                    size_t bytes = block.frameCount * static_cast<size_t>(block.channels) * sizeof(float);
                    m_fileStream.write(reinterpret_cast<const char*>(block.samples), bytes);
                    if (!m_fileStream.good()) {
                        m_hasDiskError.store(true, std::memory_order_relaxed);
                        break;
                    }
                    m_totalFramesLogged.fetch_add(block.frameCount, std::memory_order_relaxed);
                    m_totalBytesWritten.fetch_add(bytes, std::memory_order_relaxed);
                }
            }
        }

        if (!hasData) {
            // データが来ていない時は少しだけ休止 (CPU使用率を抑えつつ低レイテンシを維持)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

LoggerStats DataLoggerWorker::GetStats() const {
    LoggerStats stats;
    stats.totalFramesLogged = m_totalFramesLogged.load(std::memory_order_relaxed);
    stats.totalBytesWritten = m_totalBytesWritten.load(std::memory_order_relaxed);
    stats.bufferOverruns = m_bufferOverruns.load(std::memory_order_relaxed);
    stats.isLogging = m_isRunning.load(std::memory_order_relaxed);
    stats.hasDiskError = m_hasDiskError.load(std::memory_order_relaxed);

    double targetFs = m_config.getTargetSampleRate();
    int64_t startMs = m_startTimeMs.load(std::memory_order_relaxed);
    if (stats.isLogging && startMs > 0) {
        auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        stats.wallClockDurationSec = (nowMs > startMs)
            ? static_cast<double>(nowMs - startMs) / 1000.0
            : 0.0;
        stats.dataDurationSec = (targetFs > 0.0)
            ? static_cast<double>(stats.totalFramesLogged) / targetFs
            : 0.0;
        stats.currentDurationSec = stats.wallClockDurationSec;
    } else {
        stats.wallClockDurationSec = m_lastWallClockDurationSec;
        stats.dataDurationSec = m_lastDataDurationSec;
        stats.currentDurationSec = m_lastWallClockDurationSec;
    }
    return stats;
}

DLogFileHeader DataLoggerWorker::GetWrittenHeader() const {
    DLogFileHeader header{};
    std::memcpy(header.magic, "DLOG", 4);
    header.version = DLOG_FORMAT_VERSION;
    header.headerSize = static_cast<uint32_t>(sizeof(DLogFileHeader));
    header.channels = static_cast<uint32_t>(m_config.channels);
    header.sampleRate = static_cast<uint32_t>(std::llround(m_config.getTargetSampleRate()));
    header.sampleFormat = static_cast<uint32_t>(m_config.sampleFormat);
    header.nativeRate = static_cast<uint32_t>(m_config.nativeSampleRate);
    header.decimationFactor = static_cast<uint32_t>(m_config.decimationFactor);
    header.startTimestampUtc = m_startTimeMs.load(std::memory_order_relaxed);
    header.fullScaleVoltage = (m_config.fullScaleVoltage > 0.0) ? m_config.fullScaleVoltage : 1.0;
    return header;
}

} // namespace dlog

