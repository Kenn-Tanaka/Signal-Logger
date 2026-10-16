#pragma once

#include "wav_export_types.h"
#include "wav_export_plan.h"
#include "interpolator.h"
#include "wav_writer.h"
#include "common/localized_strings.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <string>
#include <memory>
#include <functional>

namespace dlog {

class WavExportWorker {
public:
    WavExportWorker();
    ~WavExportWorker();

    /**
     * @brief バックグラウンドエクスポート処理を開始
     * @param options エクスポートオプション
     * @param onProgress 進捗更新通知コールバック (ワーカースレッドから呼ばれる)
     * @param onFinished 完了通知コールバック (ワーカースレッドから呼ばれる)
     * @return スレッド起動に成功したら true
     */
    bool Start(const WavExportOptions& options,
               std::function<void(double percent, uint64_t processedFrames, uint64_t totalFrames)> onProgress = nullptr,
               std::function<void(const WavExportResult& result, const LocalizedError& error)> onFinished = nullptr);

    /**
     * @brief 処理の中断を要求
     */
    void RequestCancel();

    /**
     * @brief ワーカーの完了を待機
     */
    void Wait();

    /**
     * @brief 実行中か
     */
    bool IsRunning() const;

    /**
     * @brief 中断要求中か
     */
    bool IsCancelRequested() const { return m_cancelRequested.load(); }

    /**
     * @brief 最終結果を取得
     */
    WavExportResult GetResult() const;

    /**
     * @brief 発生した構造化エラーを取得
     */
    LocalizedError GetError() const;

    /**
     * @brief 現在の進捗率 (0.0 .. 1.0)
     */
    double GetProgress() const { return m_progress.load(); }

    /**
     * @brief 処理済み出力フレーム数
     */
    uint64_t GetProcessedFrames() const { return m_processedFrames.load(); }

    /**
     * @brief 総出力フレーム数
     */
    uint64_t GetTotalFrames() const { return m_totalFrames.load(); }

private:
    void WorkerThread();

    WavExportOptions m_options;
    std::function<void(double, uint64_t, uint64_t)> m_progressCallback;
    std::function<void(const WavExportResult&, const LocalizedError&)> m_finishedCallback;

    std::unique_ptr<std::thread> m_thread;
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_isRunning{false};
    std::atomic<double> m_progress{0.0};
    std::atomic<uint64_t> m_processedFrames{0};
    std::atomic<uint64_t> m_totalFrames{0};

    mutable std::mutex m_resultMutex;
    WavExportResult m_result;
    LocalizedError m_error;
};

} // namespace dlog
