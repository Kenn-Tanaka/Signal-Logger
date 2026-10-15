#pragma once

#include "audio/audio_types.h"
#include "storage/power_manager.h"
#include "readerwriterqueue.h"

#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>
#include <string>
#include <memory>

namespace dlog {

/**
 * @brief ファイル保存ワーカースレッドクラス
 * ロックフリー・キューからデシメーション済みデータを取り出し、高速バイナリ書き込みを実施
 */
class DataLoggerWorker {
public:
    DataLoggerWorker();
    ~DataLoggerWorker();

    /**
     * @brief ロギングの開始
     * @param config 設定情報
     * @return 成功したか
     */
    bool Start(const LoggerConfig& config);

    /**
     * @brief オーディオ開始直前の時刻でヘッダと経過時間基準を更新
     */
    bool MarkCaptureStartedNow();

    /**
     * @brief ロギングの停止
     */
    void Stop();

    /**
     * @brief 開始失敗時に一時出力を破棄して停止
     */
    void Abort();

    /**
     * @brief ロギング実行中か
     */
    bool IsRunning() const { return m_isRunning.load(std::memory_order_relaxed); }

    /**
     * @brief キューへのブロック追加（Decimatorスレッドから呼ばれる）
     * @param block オーディオブロック
     * @return キュー追加に成功したか（falseならキュー満杯）
     */
    bool EnqueueBlock(const AudioBlock& block);

    /**
     * @brief ディスク書き込みエラー（容量不足・切断等）が発生したか
     */
    bool HasDiskError() const { return m_hasDiskError.load(std::memory_order_relaxed); }

    /**
     * @brief 異常終了時に回収可能な一時ファイルのパス
     */
    std::string GetRecoveryFilePath() const { return m_temporaryOutputPath.u8string(); }

    /**
     * @brief 現在の統計情報を取得
     */
    LoggerStats GetStats() const;

    /**
     * @brief 実際に書き込まれたヘッダ情報を取得 (DATA-001)
     */
    DLogFileHeader GetWrittenHeader() const;

private:
    void WorkerLoop();
    void StopInternal(bool commitOutput);
    bool FinalizeOutput();

    std::atomic<bool> m_isRunning{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_hasDiskError{false};
    std::unique_ptr<std::thread> m_thread;

    LoggerConfig m_config;
    std::ofstream m_fileStream;
    std::filesystem::path m_finalOutputPath;
    std::filesystem::path m_temporaryOutputPath;
    PowerManagementGuard m_powerGuard;

    // ロックフリー SPSC キュー（十分なバッファ長: 512ブロック ≒ 約12秒〜25秒分の余裕）
    static constexpr size_t QUEUE_CAPACITY = 512;
    std::unique_ptr<moodycamel::ReaderWriterQueue<AudioBlock>> m_queue;

    // 統計値（スレッドセーフなアトミック変数）
    std::atomic<uint64_t> m_totalFramesLogged{0};
    std::atomic<uint64_t> m_totalBytesWritten{0};
    std::atomic<uint32_t> m_bufferOverruns{0};
    int64_t m_startTimeMs = 0;
    int64_t m_stopTimeMs = 0;
    double m_lastWallClockDurationSec = 0.0;
    double m_lastDataDurationSec = 0.0;
};

} // namespace dlog
