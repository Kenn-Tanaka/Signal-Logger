#include "playback_loader_worker.h"
#include <cmath>
#include <algorithm>

namespace dlog {

PlaybackLoaderWorker::PlaybackLoaderWorker() {
    m_thread = std::thread(&PlaybackLoaderWorker::WorkerLoop, this);
}

PlaybackLoaderWorker::~PlaybackLoaderWorker() {
    Stop();
}

void PlaybackLoaderWorker::Stop() {
    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        if (m_stopRequested.load()) return;
        m_stopRequested.store(true);
        m_hasNewRequest = true;
    }
    m_cv.notify_all();


    if (m_thread.joinable()) {
        m_thread.join();
    }

    std::lock_guard<std::mutex> rLock(m_readerMutex);
    if (m_reader) {
        m_reader->Close();
        m_reader.reset();
    }
}

bool PlaybackLoaderWorker::OpenFile(const std::string& filePath, std::string& errorMsg) {
    auto newReader = std::make_unique<DlogReader>();
    if (!newReader->Open(filePath)) {
        errorMsg = newReader->GetLastError().empty() ? "DLOGファイルを開けませんでした。" : newReader->GetLastError();
        return false;
    }

    PlaybackFileInfo info;
    info.isOpen = true;
    info.filePath = filePath;
    info.header = newReader->GetHeader();
    info.channelMetadata = newReader->GetChannelMetadata();
    info.totalFrames = newReader->GetTotalFrames();
    info.durationSec = newReader->GetDurationSec();
    info.sampleRate = GetDLogEffectiveSampleRate(newReader->GetHeader());
    info.channels = newReader->GetHeader().channels;
    info.fullScaleVoltage = newReader->GetHeader().fullScaleVoltage;
    info.fileSize = newReader->GetFileSize();

    {
        std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
        m_fileInfo = std::move(info);
    }

    {
        std::lock_guard<std::mutex> rLock(m_readerMutex);
        m_reader = std::move(newReader);
        m_filePath = filePath;
    }

    // 新しい世代番号へ進めて古い要求・結果を無効化
    m_currentEpoch.fetch_add(1);
    m_requestSeq.fetch_add(1);
    {
        std::lock_guard<std::mutex> resLock(m_resultMutex);
        m_hasNewResult = false;
    }
    return true;
}

void PlaybackLoaderWorker::CloseFile() {
    {
        std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
        m_fileInfo = PlaybackFileInfo{};
    }
    {
        std::lock_guard<std::mutex> rLock(m_readerMutex);
        if (m_reader) {
            m_reader->Close();
            m_reader.reset();
        }
        m_filePath.clear();
    }
    m_currentEpoch.fetch_add(1);
    m_requestSeq.fetch_add(1);
    {
        std::lock_guard<std::mutex> resLock(m_resultMutex);
        m_hasNewResult = false;
    }
}

bool PlaybackLoaderWorker::IsOpen() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.isOpen;
}

DLogFileHeader PlaybackLoaderWorker::GetHeader() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.header;
}

std::vector<ChannelMetadata> PlaybackLoaderWorker::GetChannelMetadata() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.channelMetadata;
}

uint64_t PlaybackLoaderWorker::GetTotalFrames() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.totalFrames;
}

double PlaybackLoaderWorker::GetDurationSec() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.durationSec;
}

double PlaybackLoaderWorker::GetSampleRate() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.sampleRate;
}

int PlaybackLoaderWorker::GetChannels() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.channels;
}

double PlaybackLoaderWorker::GetFullScaleVoltage() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.fullScaleVoltage;
}

std::string PlaybackLoaderWorker::GetFilePath() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.filePath;
}

uint64_t PlaybackLoaderWorker::GetFileSize() const {
    std::lock_guard<std::mutex> fLock(m_fileInfoMutex);
    return m_fileInfo.fileSize;
}

uint64_t PlaybackLoaderWorker::InvalidateGeneration() {
    m_currentEpoch.fetch_add(1);
    uint64_t seq = m_requestSeq.fetch_add(1) + 1;
    {
        std::lock_guard<std::mutex> resLock(m_resultMutex);
        m_hasNewResult = false;
    }
    return seq;
}

uint64_t PlaybackLoaderWorker::RequestWindow(double centerTimeSec, double timeWindowSec, int pixelWidth, bool isNewEpoch) {
    uint64_t ep;
    if (isNewEpoch) {
        ep = m_currentEpoch.fetch_add(1) + 1;
    } else {
        ep = m_currentEpoch.load();
    }
    uint64_t seq = m_requestSeq.fetch_add(1) + 1;

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_latestRequest.epoch = ep;
        m_latestRequest.generation = seq;
        m_latestRequest.requestSeq = seq;
        m_latestRequest.centerTimeSec = centerTimeSec;
        m_latestRequest.timeWindowSec = (timeWindowSec > 0.0) ? timeWindowSec : 2.0;
        m_latestRequest.pixelWidth = (pixelWidth > 10) ? pixelWidth : 1000;
        m_hasNewRequest = true;
    }
    m_cv.notify_one();
    return seq;
}

bool PlaybackLoaderWorker::PollResult(PlaybackWindowData& outData) {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    if (!m_hasNewResult) {
        return false;
    }
    if (m_latestResult.epoch < m_currentEpoch.load()) {
        m_hasNewResult = false;
        return false;
    }
    outData = std::move(m_latestResult);
    m_hasNewResult = false;
    return true;
}

void PlaybackLoaderWorker::WorkerLoop() {
    constexpr size_t CHUNK_FRAMES = 65536;
    std::vector<float> chunkBuffer;

    while (true) {
        PlaybackRequest req;
        {
            std::unique_lock<std::mutex> lock(m_requestMutex);
            m_cv.wait(lock, [this] {
                return m_stopRequested.load() || m_hasNewRequest;
            });

            if (m_stopRequested.load()) {
                break;
            }

            req = m_latestRequest;
            m_hasNewRequest = false;
        }

        // 要求が到着した時点で既に古いepochなら処理をスキップ
        if (req.epoch < m_currentEpoch.load()) {
            continue;
        }

        try {
            PlaybackWindowData result;
            result.epoch = req.epoch;
            result.generation = req.generation;
            result.requestSeq = req.requestSeq;
            result.centerTimeSec = req.centerTimeSec;
            result.timeWindowSec = req.timeWindowSec;

            bool isCancelled = false;

            // リーダーの基本情報取得（短時間ロック）
            double Fs = 0.0;
            uint64_t totalFrames = 0;
            int channels = 0;
            double fullScale = 1.0;
            bool isOpen = false;

            {
                std::lock_guard<std::mutex> rLock(m_readerMutex);
                if (m_reader && m_reader->IsOpen()) {
                    isOpen = true;
                    Fs = GetDLogEffectiveSampleRate(m_reader->GetHeader());
                    totalFrames = m_reader->GetTotalFrames();
                    channels = m_reader->GetHeader().channels;
                    fullScale = m_reader->GetHeader().fullScaleVoltage;
                }
            }

            if (!isOpen) {
                result.hasError = true;
                result.errorMessage = "ファイルが開かれていません。";
            } else {
                result.sampleRate = Fs;
                result.totalFrames = totalFrames;
                result.channels = channels;
                result.fullScaleVoltage = fullScale;
                result.peakAbsPerChannel.assign(channels, 0.0f);

                double leftTime = req.centerTimeSec - req.timeWindowSec / 2.0;
                double rightTime = req.centerTimeSec + req.timeWindowSec / 2.0;

                int64_t startFrame = static_cast<int64_t>(std::floor(leftTime * Fs));
                int64_t endFrame = static_cast<int64_t>(std::ceil(rightTime * Fs));

                int64_t clampedStart = std::max<int64_t>(0, startFrame);
                int64_t clampedEnd = std::min<int64_t>(static_cast<int64_t>(totalFrames) - 1, endFrame);

                result.validLeftTime = (clampedStart <= clampedEnd && Fs > 0.0)
                    ? (static_cast<double>(clampedStart) / Fs) : 0.0;
                result.validRightTime = (clampedStart <= clampedEnd && Fs > 0.0)
                    ? (static_cast<double>(clampedEnd + 1) / Fs) : 0.0;
                result.startFrame = clampedStart;

                if (clampedStart > clampedEnd || totalFrames == 0 || Fs <= 0.0) {
                    result.frameCount = 0;
                    result.isAggregated = false;
                } else {
                    size_t totalFramesToRead = static_cast<size_t>(clampedEnd - clampedStart + 1);
                    int numCols = std::max(10, req.pixelWidth);

                    if (totalFramesToRead <= static_cast<size_t>(numCols * 2)) {
                        // 低密度: Rawサンプルモード
                        result.isAggregated = false;
                        result.rawSamples.assign(channels, std::vector<float>(totalFramesToRead, 0.0f));

                        size_t framesRead = 0;
                        bool readOk = false;
                        std::string readErr;
                        {
                            std::lock_guard<std::mutex> rLock(m_readerMutex);
                            if (m_reader && m_reader->IsOpen()) {
                                readOk = m_reader->ReadFrames(static_cast<uint64_t>(clampedStart), totalFramesToRead, chunkBuffer, framesRead);
                                if (!readOk) readErr = m_reader->GetLastError();
                            } else {
                                readErr = "リーダーが閉じられました";
                            }
                        }

                        if (!readOk) {
                            result.hasError = true;
                            result.errorMessage = readErr.empty() ? "波形データの読み出しに失敗しました。" : readErr;
                        } else {
                            result.frameCount = framesRead;
                            for (size_t f = 0; f < framesRead; ++f) {
                                for (int ch = 0; ch < channels; ++ch) {
                                    float val = chunkBuffer[f * channels + ch];
                                    result.rawSamples[ch][f] = val;
                                    float absVal = std::fabs(val);
                                    if (absVal > result.peakAbsPerChannel[ch]) {
                                        result.peakAbsPerChannel[ch] = absVal;
                                    }
                                }
                            }
                        }
                    } else {
                        // 高密度: Min/Max エンベロープ集約モード
                        result.isAggregated = true;
                        result.numColumns = numCols;
                        result.minPerChannel.assign(channels, std::vector<float>(numCols, 1e9f));
                        result.maxPerChannel.assign(channels, std::vector<float>(numCols, -1e9f));

                        size_t totalProcessed = 0;
                        while (totalProcessed < totalFramesToRead) {
                            // epochが更新された場合、または停止要求がある場合のみ即座に中断
                            // （同一epoch内の連続要求では処理を中断しない）
                            if (m_currentEpoch.load() > req.epoch || m_stopRequested.load()) {
                                isCancelled = true;
                                break;
                            }

                            size_t thisChunk = std::min<size_t>(CHUNK_FRAMES, totalFramesToRead - totalProcessed);
                            uint64_t readPos = static_cast<uint64_t>(clampedStart + totalProcessed);
                            size_t actualRead = 0;
                            bool readOk = false;
                            std::string readErr;

                            // チャンク単位のロック (GUIスレッドのOpenFile/CloseFileをブロックしない)
                            {
                                std::lock_guard<std::mutex> rLock(m_readerMutex);
                                if (m_reader && m_reader->IsOpen()) {
                                    readOk = m_reader->ReadFrames(readPos, thisChunk, chunkBuffer, actualRead);
                                    if (!readOk) readErr = m_reader->GetLastError();
                                } else {
                                    readErr = "リーダーが閉じられました";
                                }
                            }

                            if (!readOk || actualRead == 0) {
                                result.hasError = true;
                                result.errorMessage = readErr.empty() ? "波形データの読み出しに失敗しました。" : readErr;
                                break;
                            }

                            for (size_t f = 0; f < actualRead; ++f) {
                                int64_t curFrame = clampedStart + totalProcessed + f;
                                double t = static_cast<double>(curFrame) / Fs;
                                int col = static_cast<int>((t - leftTime) / req.timeWindowSec * numCols);
                                if (col < 0) col = 0;
                                if (col >= numCols) col = numCols - 1;

                                for (int ch = 0; ch < channels; ++ch) {
                                    float val = chunkBuffer[f * channels + ch];
                                    if (val < result.minPerChannel[ch][col]) {
                                        result.minPerChannel[ch][col] = val;
                                    }
                                    if (val > result.maxPerChannel[ch][col]) {
                                        result.maxPerChannel[ch][col] = val;
                                    }
                                    float absVal = std::fabs(val);
                                    if (absVal > result.peakAbsPerChannel[ch]) {
                                        result.peakAbsPerChannel[ch] = absVal;
                                    }
                                }
                            }

                            totalProcessed += actualRead;
                            if (actualRead < thisChunk) {
                                break;
                            }
                        }

                        result.frameCount = totalProcessed;

                        // 未通過カラムのクランプ
                        for (int ch = 0; ch < channels; ++ch) {
                            for (int c = 0; c < numCols; ++c) {
                                if (result.minPerChannel[ch][c] > 1e8f) {
                                    result.minPerChannel[ch][c] = 0.0f;
                                    result.maxPerChannel[ch][c] = 0.0f;
                                }
                            }
                        }
                    }
                }
            }

            if (!isCancelled && req.epoch == m_currentEpoch.load()) {
                std::lock_guard<std::mutex> resLock(m_resultMutex);
                if (m_latestResult.epoch < req.epoch || m_latestResult.requestSeq <= req.requestSeq) {
                    m_latestResult = std::move(result);
                    m_hasNewResult = true;
                }
            }
        } catch (const std::exception& ex) {
            PlaybackWindowData errResult;
            errResult.epoch = req.epoch;
            errResult.generation = req.epoch;
            errResult.requestSeq = req.requestSeq;
            errResult.hasError = true;
            errResult.errorMessage = std::string("再生データ読み出し例外: ") + ex.what();
            std::lock_guard<std::mutex> resLock(m_resultMutex);
            m_latestResult = std::move(errResult);
            m_hasNewResult = true;
        } catch (...) {
            PlaybackWindowData errResult;
            errResult.epoch = req.epoch;
            errResult.generation = req.epoch;
            errResult.requestSeq = req.requestSeq;
            errResult.hasError = true;
            errResult.errorMessage = "再生データ読み出し中に未知の例外が発生しました。";
            std::lock_guard<std::mutex> resLock(m_resultMutex);
            m_latestResult = std::move(errResult);
            m_hasNewResult = true;
        }
    }
}

} // namespace dlog
