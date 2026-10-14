#pragma once

#include "audio/audio_types.h"
#include "dsp/decimator_processor.h"
#include "storage/data_logger_worker.h"
#include "readerwriterqueue.h"

#include <portaudio.h>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <thread>

namespace dlog {

/**
 * @brief オーディオデバイス管理・PortAudioストリーム制御・デシメーション中継クラス
 */
class AudioDeviceManager {
public:
    AudioDeviceManager();
    ~AudioDeviceManager();

    /**
     * @brief 利用可能なオーディオ入力デバイス一覧を取得
     */
    std::vector<DeviceInfo> GetInputDevices();

    /**
     * @brief デフォルトの入力デバイスIDを取得
     */
    int GetDefaultInputDevice() const;

    /**
     * @brief 指定したデバイス・チャネル数・サンプリング周波数の入力形式サポートを確認
     * @param deviceId PortAudio デバイス ID (負値ならデフォルト入力デバイス)
     * @param channels チャネル数
     * @param sampleRate サンプリング周波数 (Hz)
     * @return paFormatIsSupported (0) なら対応、それ以外は PortAudio エラーコード
     */
    PaError CheckInputFormatSupport(
        int deviceId,
        int channels,
        double sampleRate) const;

    /**
     * @brief オーディオストリームを開き、ロギングの準備
     */
    bool OpenStream(const LoggerConfig& config, std::shared_ptr<DataLoggerWorker> loggerWorker = nullptr);

    /**
     * @brief 録音・ストリームの開始
     */
    bool StartStream();

    /**
     * @brief 録音・ストリームの停止
     */
    bool StopStream();

    /**
     * @brief ストリームが実行中か
     */
    bool IsStreaming() const { return m_isStreaming.load(std::memory_order_relaxed); }

    /**
     * @brief UI描画用ブロックの取得（GUIスレッドからポーリング）
     * @param block 取得先
     * @return 取得できたか
     */
    bool TryDequeueUIBlock(AudioBlock& block);

    /**
     * @brief デバイス切断・異常停止エラーが発生したか
     */
    bool HasDeviceError() const;

    /**
     * @brief 統計情報の取得
     */
    LoggerStats GetStats() const;

    /**
     * @brief PortAudioコールバック内部関数
     */
    int AudioCallback(const void* inputBuffer, void* outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags);

    /**
     * @brief PortAudioストリーム終了コールバック（USB切断検知用）
     */
    static void StreamFinishedCallbackBridge(void* userData);
    void OnStreamFinished();

private:
    void DecimatorWorkerLoop();

    PaStream* m_paStream = nullptr;
    LoggerConfig m_config;
    std::shared_ptr<DataLoggerWorker> m_loggerWorker;

    std::atomic<bool> m_isStreaming{false};
    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_stopDecimator{false};
    mutable std::atomic<bool> m_hasDeviceError{false};
    std::unique_ptr<std::thread> m_decimatorThread;

    // 1. コールバックからデシメータスレッドへのRawデータキュー (SPSC)
    static constexpr size_t RAW_QUEUE_CAPACITY = 256;
    std::unique_ptr<moodycamel::ReaderWriterQueue<AudioBlock>> m_rawAudioQueue;

    // 2. デシメータからUIメインスレッドへの波形プレビュー用キュー (SPSC)
    static constexpr size_t UI_QUEUE_CAPACITY = 64;
    std::unique_ptr<moodycamel::ReaderWriterQueue<AudioBlock>> m_uiAudioQueue;

    DecimatorProcessor m_decimator;

    // 統計追跡
    std::atomic<uint64_t> m_totalFramesCaptured{0};
    std::atomic<uint32_t> m_rawBufferOverruns{0};
    std::atomic<uint32_t> m_uiFramesDropped{0};
};

} // namespace dlog
