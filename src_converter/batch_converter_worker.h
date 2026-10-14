#pragma once

#include "converter_types.h"
#include "dlog_reader.h"
#include "csv_writer.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <memory>
#include <functional>

namespace dlog {

class BatchConverterWorker {
public:
    BatchConverterWorker();
    ~BatchConverterWorker();

    /**
     * @brief バッチ変換を開始
     * @param items 変換対象ファイル一覧
     * @param options 変換オプション
     * @return 開始成功したか
     */
    bool Start(const std::vector<BatchItem>& items, const ConversionOptions& options);

    /**
     * @brief 出力ファイルの重複・衝突を検証 (DATA-002)
     * @param items バッチ対象
     * @param options 変換設定
     * @param errorMsg 衝突検出時の詳細メッセージ
     * @return 衝突がなければ true
     */
    static bool CheckCollisions(const std::vector<BatchItem>& items,
                                const ConversionOptions& options,
                                std::string& errorMsg);

    /**
     * @brief 変換を中断（キャンセル）
     */
    void Cancel();

    /**
     * @brief 変換実行中か
     */
    bool IsRunning() const { return m_isRunning.load(std::memory_order_relaxed); }

    /**
     * @brief 中断要求されたか
     */
    bool IsCanceled() const { return m_cancelRequested.load(std::memory_order_relaxed); }

    /**
     * @brief 変換スレッドの完了を待機
     */
    void WaitForCompletion() {
        if (m_thread && m_thread->joinable()) {
            m_thread->join();
        }
    }

    /**
     * @brief 進捗統計を取得
     */
    BatchStats GetStats() const;

    /**
     * @brief 最新のバッチ項目リストを取得（完了状態等の同期用）
     */
    std::vector<BatchItem> GetItems() const;

private:
    void WorkerLoop();
    bool ConvertSingleFile(BatchItem& item);

    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_cancelRequested{false};
    std::unique_ptr<std::thread> m_thread;

    mutable std::mutex m_itemsMutex;
    std::vector<BatchItem> m_items;
    ConversionOptions m_options;

    // リアルタイム進捗追跡
    std::atomic<size_t> m_currentFileIndex{0};
    std::atomic<double> m_currentFileProgress{0.0};
    std::atomic<double> m_overallProgress{0.0};
    std::atomic<double> m_bytesPerSec{0.0};
    std::atomic<double> m_estimatedRemainingSec{0.0};

    uint64_t m_totalBytesAllFiles = 0;
    std::atomic<uint64_t> m_processedBytesAllFiles{0};
};

} // namespace dlog
