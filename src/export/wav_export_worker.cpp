#include "wav_export_worker.h"
#include "dlog_reader.h"
#include <algorithm>
#include <iostream>

namespace dlog {

WavExportWorker::WavExportWorker() = default;

WavExportWorker::~WavExportWorker() {
    RequestCancel();
    Wait();
}

bool WavExportWorker::Start(const WavExportOptions& options,
                            std::function<void(double, uint64_t, uint64_t)> onProgress,
                            std::function<void(const WavExportResult&, const LocalizedError&)> onFinished) {
    if (m_isRunning.load()) {
        return false;
    }

    Wait();

    m_options = options;
    m_progressCallback = std::move(onProgress);
    m_finishedCallback = std::move(onFinished);

    m_cancelRequested.store(false);
    m_progress.store(0.0);
    m_processedFrames.store(0);
    m_totalFrames.store(0);

    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_result = WavExportResult{};
        m_error = LocalizedError{};
    }

    m_isRunning.store(true);
    m_thread = std::make_unique<std::thread>(&WavExportWorker::WorkerThread, this);
    return true;
}

void WavExportWorker::RequestCancel() {
    m_cancelRequested.store(true);
}

void WavExportWorker::Wait() {
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
    }
    m_thread.reset();
    m_isRunning.store(false);
}

bool WavExportWorker::IsRunning() const {
    return m_isRunning.load();
}

WavExportResult WavExportWorker::GetResult() const {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    return m_result;
}

LocalizedError WavExportWorker::GetError() const {
    std::lock_guard<std::mutex> lock(m_resultMutex);
    return m_error;
}

void WavExportWorker::WorkerThread() {
    WavExportResult localResult;
    LocalizedError localError;
    localResult.destinationPath = m_options.destinationWavPath;

    try {
        // 1. 専用 DlogReader インスタンスで DLOG を開く (再生 Reader とシーク位置を共有しない)
        DlogReader reader;
        if (!reader.Open(m_options.sourceDlogPath)) {
            localError = reader.GetLastErrorInfo();
            if (!localError.HasError()) {
                localError = MakeError(StringId::ErrFileOpenFailed);
            }
            throw std::runtime_error("Failed to open source DLOG file.");
        }

        const DLogFileHeader& header = reader.GetHeader();
        uint64_t fileTotalFrames = reader.GetTotalFrames();

        // 2. プランと見積もりの検証
        WavExportEstimate est = WavExportPlan::AnalyzePlan(header, fileTotalFrames, m_options);
        if (!est.isValid) {
            localError = est.errorInfo.HasError() ? est.errorInfo : MakeError(StringId::ErrWavGeneral);
            localError.rawDetail = est.validationError;
            throw std::runtime_error(est.validationError);
        }

        // 3. 入出力パスの非破壊妥当性検証
        std::string pathErr;
        LocalizedError pathErrInfo;
        if (!WavExportPlan::ValidatePaths(m_options.sourceDlogPath, m_options.destinationWavPath, pathErr, &pathErrInfo)) {
            localError = pathErrInfo.HasError() ? pathErrInfo : MakeError(StringId::ErrWavGeneral);
            localError.rawDetail = pathErr;
            throw std::runtime_error(pathErr);
        }

        m_totalFrames.store(est.outputFrames);
        localResult.containerFormat = est.containerFormat;

        // 4. WavWriter を一時ファイルで初期化
        WavWriter writer;
        std::string openErr;
        if (!writer.Open(m_options.destinationWavPath, est.containerFormat, est.channelCount,
                         est.outputRate, m_options.sampleFormat, openErr)) {
            localError = MakeError(StringId::ErrTempFileCreationFailed);
            localError.rawDetail = openErr;
            throw std::runtime_error(openErr);
        }

        // 5. 補間器の初期化
        Interpolator interpolator;
        interpolator.Initialize(est.channelCount, est.interpolationFactor, m_options.interpolationMethod);
        int preMargin = interpolator.GetPreMargin();
        int postMargin = interpolator.GetPostMargin();

        // 出力チャネル一覧の確定 (入力のチャネル番号昇順を維持、重複除去)
        std::vector<int> chs;
        if (m_options.selectedChannels.empty() && !m_options.explicitChannelSelection) {
            for (uint32_t c = 0; c < header.channels; ++c) {
                chs.push_back(static_cast<int>(c));
            }
        } else {
            for (int ch : m_options.selectedChannels) {
                if (ch >= 0 && static_cast<uint32_t>(ch) < header.channels &&
                    std::find(chs.begin(), chs.end(), ch) == chs.end()) {
                    chs.push_back(ch);
                }
            }
            std::sort(chs.begin(), chs.end());
        }

        // 6. チャンク単位のストリーミング補間・書き込みループ
        uint64_t startFrame = m_options.exportEntireFile ? 0 : m_options.startFrame;
        uint64_t endFrame = m_options.exportEntireFile ? fileTotalFrames : m_options.endFrame;
        uint64_t M = endFrame - startFrame;

        const size_t CHUNK_IN = 1024;
        uint64_t processedOutFrames = 0;
        std::string streamError;

        std::vector<float> rawFileBuf;
        std::vector<float> inputWithMargins;

        for (uint64_t k = 0; k < M; k += CHUNK_IN) {
            if (m_cancelRequested.load()) {
                break;
            }

            size_t chunkLen = static_cast<size_t>(std::min<uint64_t>(CHUNK_IN, M - k));
            uint64_t curGlobalStart = startFrame + k;
            uint64_t curGlobalEnd = curGlobalStart + chunkLen;

            // マージンを含む必要な全フレーム範囲 [reqStart, reqEnd)
            int64_t reqStart = static_cast<int64_t>(curGlobalStart) - preMargin;
            int64_t reqEnd = static_cast<int64_t>(curGlobalEnd) + postMargin;
            size_t totalWindowFrames = static_cast<size_t>(reqEnd - reqStart);

            // ファイル内に実在する範囲 [validFileStart, validFileEnd)
            int64_t validFileStart = std::max<int64_t>(0, reqStart);
            int64_t validFileEnd = std::min<int64_t>(static_cast<int64_t>(fileTotalFrames), reqEnd);
            size_t validFileFrames = (validFileEnd > validFileStart) ? static_cast<size_t>(validFileEnd - validFileStart) : 0;

            // ファイルから実サンプルを読み出し
            if (validFileFrames > 0) {
                size_t framesRead = 0;
                if (!reader.ReadFrames(static_cast<uint64_t>(validFileStart), validFileFrames, rawFileBuf, framesRead) || framesRead != validFileFrames) {
                    localError = reader.GetLastErrorInfo();
                    streamError = "Failed to read samples from DLOG file: " + reader.GetLastError();
                    break;
                }
            }

            // 前後マージンを端点値でパディングし、選択チャネルのみを抽出
            inputWithMargins.assign(totalWindowFrames * est.channelCount, 0.0f);
            uint32_t fileChannels = header.channels;

            for (size_t r = 0; r < totalWindowFrames; ++r) {
                int64_t g = reqStart + static_cast<int64_t>(r);
                int64_t clampedG = std::clamp<int64_t>(g, 0, static_cast<int64_t>(fileTotalFrames) - 1);

                size_t rawFrameOffset = static_cast<size_t>(clampedG - validFileStart) * fileChannels;
                size_t targetFrameOffset = r * est.channelCount;

                for (size_t c = 0; c < est.channelCount; ++c) {
                    int origCh = chs[c];
                    inputWithMargins[targetFrameOffset + c] = rawFileBuf[rawFrameOffset + origCh];
                }
            }

            // 補間処理の実行と逐次出力
            bool procOk = interpolator.ProcessFrames(
                inputWithMargins.data(), chunkLen,
                [&](const float* outChunk, size_t outFrames) -> bool {
                    if (m_cancelRequested.load()) {
                        return false;
                    }
                    std::string wErr;
                    if (!writer.WriteFrames(outChunk, outFrames, wErr)) {
                        streamError = wErr;
                        return false;
                    }
                    processedOutFrames += outFrames;
                    m_processedFrames.store(processedOutFrames);
                    if (est.outputFrames > 0) {
                        m_progress.store(static_cast<double>(processedOutFrames) / static_cast<double>(est.outputFrames));
                    }
                    if (m_progressCallback) {
                        m_progressCallback(m_progress.load(), processedOutFrames, est.outputFrames);
                    }
                    return true;
                },
                4096 /* maxOutputChunkFrames: メモリ上限保証 */
            );

            if (!procOk) {
                if (m_cancelRequested.load()) {
                    break;
                }
                if (!streamError.empty()) {
                    if (!localError.HasError()) {
                        localError = MakeError(StringId::ErrDataWriteFailed);
                    }
                    localError.rawDetail = streamError;
                    throw std::runtime_error(streamError);
                }
                break;
            }
        }

        // 7. 確定または中断
        if (m_cancelRequested.load()) {
            writer.Abort();
            localResult.canceled = true;
            localResult.success = false;
        } else if (!streamError.empty()) {
            writer.Abort();
            localResult.success = false;
            if (!localError.HasError()) {
                localError = MakeError(StringId::ErrDataWriteFailed);
            }
            localError.rawDetail = streamError;
        } else {
            std::string commitErr;
            if (!writer.Commit(commitErr)) {
                localResult.success = false;
                localError = MakeError(StringId::ErrRenameFailed);
                localError.rawDetail = commitErr;
            } else {
                localResult.success = true;
                localResult.outputFramesWritten = writer.GetFramesWritten();
                localResult.totalBytesWritten = writer.GetTotalFileBytes();
                localResult.pcm16ClippedSamples = writer.GetPcm16ClippedSamples();
                m_progress.store(1.0);
            }
        }

    } catch (const std::exception& ex) {
        localResult.success = false;
        if (!localError.HasError()) {
            localError.rawDetail = ex.what();
            localError.code = StringId::ErrWavGeneral;
        }
    } catch (...) {
        localResult.success = false;
        localError.rawDetail = "Unknown exception occurred in WAV export worker.";
        localError.code = StringId::ErrWavGeneral;
    }

    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        m_result = localResult;
        m_error = localError;
    }

    m_isRunning.store(false);

    if (m_finishedCallback) {
        m_finishedCallback(localResult, localError);
    }
}

} // namespace dlog
