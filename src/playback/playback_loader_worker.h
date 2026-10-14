#pragma once

#include "playback_types.h"
#include "dlog_reader.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <vector>

namespace dlog {

class PlaybackLoaderWorker {
public:
    PlaybackLoaderWorker();
    ~PlaybackLoaderWorker();

    /**
     * @brief DLOGファイルを開く
     * @param filePath ファイルパス
     * @param errorMsg 失敗時のエラーメッセージ
     * @return 成功したか
     */
    bool OpenFile(const std::string& filePath, std::string& errorMsg);

    /**
     * @brief ファイルを閉じる
     */
    void CloseFile();

    /**
     * @brief ファイルが開かれているか
     */
    bool IsOpen() const;

    DLogFileHeader GetHeader() const;
    std::vector<ChannelMetadata> GetChannelMetadata() const;
    uint64_t GetTotalFrames() const;
    double GetDurationSec() const;
    double GetSampleRate() const;
    int GetChannels() const;
    double GetFullScaleVoltage() const;
    std::string GetFilePath() const;
    uint64_t GetFileSize() const;

    /**
     * @brief 破棄世代を進め、先行要求をすべて無効化 (シーク、ファイル切替、時間幅変更時)
     */
    uint64_t InvalidateGeneration();

    /**
     * @brief ウィンドウ読み込み要求を発行 (GUIスレッドから呼出)
     * @param centerTimeSec 中心時刻
     * @param timeWindowSec 時間幅
     * @param pixelWidth 描画ピクセル幅
     * @param isNewEpoch シーク等による世代更新を伴う要求か
     * @return 発行された世代番号
     */
    uint64_t RequestWindow(double centerTimeSec, double timeWindowSec, int pixelWidth, bool isNewEpoch = false);

    /**
     * @brief 完了した描画データを取得 (GUIスレッドからタイマー等で呼出)
     * @param outData 取得先
     * @return 新しい完了データがあれば true
     */
    bool PollResult(PlaybackWindowData& outData);

    /**
     * @brief 現在の世代番号を取得
     */
    uint64_t GetCurrentGeneration() const { return m_currentGeneration.load(); }

    /**
     * @brief ワーカーを停止してスレッドを終了
     */
    void Stop();

private:
    void WorkerLoop();

    // GUIスレッド用メタデータキャッシュ（I/O中も即時返却）
    struct PlaybackFileInfo {
        bool isOpen = false;
        std::string filePath;
        DLogFileHeader header{};
        std::vector<ChannelMetadata> channelMetadata;
        uint64_t totalFrames = 0;
        double durationSec = 0.0;
        double sampleRate = 0.0;
        int channels = 0;
        double fullScaleVoltage = 1.0;
        uint64_t fileSize = 0;
    };

    mutable std::mutex m_fileInfoMutex;
    PlaybackFileInfo m_fileInfo;

    mutable std::mutex m_readerMutex;
    std::unique_ptr<DlogReader> m_reader;
    std::string m_filePath;

    std::thread m_thread;
    std::mutex m_requestMutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stopRequested{false};

    bool m_hasNewRequest = false;
    PlaybackRequest m_latestRequest;
    std::atomic<uint64_t> m_currentGeneration{0};


    std::mutex m_resultMutex;
    bool m_hasNewResult = false;
    PlaybackWindowData m_latestResult;
};

} // namespace dlog
