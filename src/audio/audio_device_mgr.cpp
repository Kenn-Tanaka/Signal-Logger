#include "audio/audio_device_mgr.h"
#include <iostream>
#include <cstring>
#include <chrono>
#include <cmath>
#include <limits>

namespace dlog {

// PortAudio Cコールバックブリッジ
static int PaCallbackBridge(const void* inputBuffer, void* outputBuffer,
                            unsigned long framesPerBuffer,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags,
                            void* userData) {
    auto* mgr = static_cast<AudioDeviceManager*>(userData);
    if (mgr) {
        return mgr->AudioCallback(inputBuffer, outputBuffer, framesPerBuffer, timeInfo, statusFlags);
    }
    return paContinue;
}

AudioDeviceManager::AudioDeviceManager() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio initialization error: " << Pa_GetErrorText(err) << std::endl;
    }
    m_rawAudioQueue = std::make_unique<moodycamel::ReaderWriterQueue<AudioBlock>>(RAW_QUEUE_CAPACITY);
    m_uiAudioQueue = std::make_unique<moodycamel::ReaderWriterQueue<AudioBlock>>(UI_QUEUE_CAPACITY);
}

AudioDeviceManager::~AudioDeviceManager() {
    StopStream();
    Pa_Terminate();
}

std::vector<DeviceInfo> AudioDeviceManager::GetInputDevices() {
    std::vector<DeviceInfo> devices;
    int count = Pa_GetDeviceCount();
    if (count < 0) return devices;

    for (int i = 0; i < count; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (!info || info->maxInputChannels <= 0) continue;

        DeviceInfo dev;
        dev.id = i;
        dev.name = info->name ? info->name : "Unknown Device";
        dev.maxInputChannels = info->maxInputChannels;
        dev.defaultSampleRate = info->defaultSampleRate;

        const PaHostApiInfo* apiInfo = Pa_GetHostApiInfo(info->hostApi);
        dev.hostApiName = apiInfo && apiInfo->name ? apiInfo->name : "Default";

        devices.push_back(dev);
    }
    return devices;
}

int AudioDeviceManager::GetDefaultInputDevice() const {
    return Pa_GetDefaultInputDevice();
}

PaError AudioDeviceManager::CheckInputFormatSupport(
    int deviceId,
    int channels,
    double sampleRate) const {

    if (deviceId < 0) {
        return paInvalidDevice;
    }
    const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(deviceId);
    if (!devInfo) {
        return paInvalidDevice;
    }
    if (channels <= 0 || channels > devInfo->maxInputChannels) {
        return paInvalidChannelCount;
    }

    PaStreamParameters inputParams;
    std::memset(&inputParams, 0, sizeof(inputParams));
    inputParams.device = deviceId;
    inputParams.channelCount = channels;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = devInfo->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    return Pa_IsFormatSupported(&inputParams, nullptr, sampleRate);
}

bool AudioDeviceManager::OpenStream(const LoggerConfig& config, std::shared_ptr<DataLoggerWorker> loggerWorker) {
    if (m_isStreaming.load() || m_paStream || m_decimatorThread) {
        StopStream();
    }

    if (config.channels < 1 || config.channels > static_cast<int>(MAX_AUDIO_CHANNELS) ||
        config.framesPerBuffer == 0 || config.framesPerBuffer > MAX_AUDIO_FRAMES ||
        config.nativeSampleRate <= 0.0 || !std::isfinite(config.nativeSampleRate) ||
        config.nativeSampleRate > static_cast<double>(std::numeric_limits<uint32_t>::max()) ||
        config.decimationFactor < 1 ||
        !std::isfinite(config.customTargetSampleRate) || config.customTargetSampleRate < 0.0) {
        return false;
    }

    m_config = config;
    m_loggerWorker = loggerWorker;

    // キューの再初期化
    m_rawAudioQueue = std::make_unique<moodycamel::ReaderWriterQueue<AudioBlock>>(RAW_QUEUE_CAPACITY);
    m_uiAudioQueue = std::make_unique<moodycamel::ReaderWriterQueue<AudioBlock>>(UI_QUEUE_CAPACITY);
    m_totalFramesCaptured.store(0);
    m_rawBufferOverruns.store(0);
    m_uiFramesDropped.store(0);
    m_hasDeviceError.store(false);
    m_stopRequested.store(false);

    // デシメータ初期化。記録ヘッダとDSPで同一の整数係数を使うため、
    // LoggerConfig::decimationFactorを唯一の確定値とする。
    m_decimator.Initialize(m_config.channels, m_config.decimationFactor);

    // PortAudio ストリームパラメータ構築
    PaStreamParameters inputParams;
    std::memset(&inputParams, 0, sizeof(inputParams));
    inputParams.device = (m_config.deviceId >= 0) ? m_config.deviceId : Pa_GetDefaultInputDevice();
    if (inputParams.device == paNoDevice) {
        return false;
    }
    inputParams.channelCount = m_config.channels;
    inputParams.sampleFormat = paFloat32; // 内部処理は高精度 float32
    const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(inputParams.device);
    inputParams.suggestedLatency = devInfo ? devInfo->defaultLowInputLatency : 0.050;
    inputParams.hostApiSpecificStreamInfo = nullptr;

    // 入力形式のサポートを直前に再検証 (要求 7)
    PaError formatErr = CheckInputFormatSupport(inputParams.device, m_config.channels, m_config.nativeSampleRate);
    if (formatErr != paFormatIsSupported) {
        std::cerr << "Input format not supported before opening stream: "
                  << Pa_GetErrorText(formatErr) << std::endl;
        return false;
    }

    PaError err = Pa_OpenStream(
        &m_paStream,
        &inputParams,
        nullptr, // 出力なし（入力専用データロガー）
        m_config.nativeSampleRate,
        m_config.framesPerBuffer,
        paClipOff, // クリップ処理を省いてCPU負荷最小化
        PaCallbackBridge,
        this
    );

    if (err != paNoError) {
        std::cerr << "Failed to open PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        m_paStream = nullptr;
        return false;
    }

    return true;
}

bool AudioDeviceManager::StartStream() {
    if (!m_paStream) return false;
    if (m_isStreaming.load()) return true;

    m_stopRequested.store(false);
    m_hasDeviceError.store(false);
    m_stopDecimator.store(false);
    try {
        m_decimatorThread = std::make_unique<std::thread>(&AudioDeviceManager::DecimatorWorkerLoop, this);
    } catch (...) {
        Pa_CloseStream(m_paStream);
        m_paStream = nullptr;
        return false;
    }

    PaError callbackErr = Pa_SetStreamFinishedCallback(m_paStream, StreamFinishedCallbackBridge);
    if (callbackErr != paNoError) {
        m_stopDecimator.store(true, std::memory_order_release);
        m_decimatorThread->join();
        m_decimatorThread.reset();
        Pa_CloseStream(m_paStream);
        m_paStream = nullptr;
        return false;
    }

    if (m_loggerWorker && !m_loggerWorker->MarkCaptureStartedNow()) {
        m_stopDecimator.store(true, std::memory_order_release);
        if (m_decimatorThread && m_decimatorThread->joinable()) {
            m_decimatorThread->join();
        }
        m_decimatorThread.reset();
        Pa_CloseStream(m_paStream);
        m_paStream = nullptr;
        return false;
    }

    PaError err = Pa_StartStream(m_paStream);
    if (err != paNoError) {
        std::cerr << "Failed to start PortAudio stream: " << Pa_GetErrorText(err) << std::endl;
        m_stopDecimator.store(true);
        if (m_decimatorThread && m_decimatorThread->joinable()) {
            m_decimatorThread->join();
        }
        m_decimatorThread.reset();
        Pa_CloseStream(m_paStream);
        m_paStream = nullptr;
        return false;
    }

    m_isStreaming.store(true);
    return true;
}

bool AudioDeviceManager::StopStream() {
    m_stopRequested.store(true);
    if (!m_isStreaming.load() && !m_paStream) return true;

    if (m_paStream) {
        if (Pa_IsStreamActive(m_paStream) == 1) {
            Pa_StopStream(m_paStream);
        }
        Pa_CloseStream(m_paStream);
        m_paStream = nullptr;
    }

    m_stopDecimator.store(true);
    if (m_decimatorThread && m_decimatorThread->joinable()) {
        m_decimatorThread->join();
    }
    m_decimatorThread.reset();

    m_loggerWorker.reset();
    m_isStreaming.store(false);
    return true;
}

// PortAudio リアルタイム受信用コールバック（ミューテックス禁止、malloc禁止、I/O禁止）
int AudioDeviceManager::AudioCallback(const void* inputBuffer, void* /*outputBuffer*/,
                                     unsigned long framesPerBuffer,
                                     const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                     PaStreamCallbackFlags statusFlags) {
    bool dropoutDetected = (statusFlags & paInputOverflow) != 0;
    if (!inputBuffer) {
        dropoutDetected = true;
        m_rawBufferOverruns.fetch_add(1, std::memory_order_relaxed);
        return paContinue;
    }

    AudioBlock block;
    block.channels = m_config.channels;
    block.frameCount = framesPerBuffer;
    block.sampleRate = m_config.nativeSampleRate;
    block.timestampUtcMs = 0; // コールバック内でのシステムコールを避けるため後段スレッドで補完

    size_t samplesToCopy = framesPerBuffer * static_cast<size_t>(m_config.channels);
    if (samplesToCopy > MAX_AUDIO_CHANNELS * MAX_AUDIO_FRAMES) {
        dropoutDetected = true;
        samplesToCopy = MAX_AUDIO_CHANNELS * MAX_AUDIO_FRAMES;
        block.frameCount = samplesToCopy / m_config.channels;
    }

    if (dropoutDetected) {
        m_rawBufferOverruns.fetch_add(1, std::memory_order_relaxed);
    }

    std::memcpy(block.samples, inputBuffer, samplesToCopy * sizeof(float));

    if (!m_rawAudioQueue->try_enqueue(block)) {
        // コールバックからデシメータへのキューが溢れた（万が一のアンダーラン・オーバーラン）
        m_rawBufferOverruns.fetch_add(1, std::memory_order_relaxed);
    }

    m_totalFramesCaptured.fetch_add(framesPerBuffer, std::memory_order_relaxed);
    return paContinue;
}

// デシメーション・中継ワーカースレッド（バックグラウンド）
void AudioDeviceManager::DecimatorWorkerLoop() {
#ifdef _WIN32
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

    AudioBlock rawBlock;
    AudioBlock decimatedBlock;
    uint64_t currentOutputFrameIndex = 0;

    while (true) {
        bool hasData = false;
        while (m_rawAudioQueue->try_dequeue(rawBlock)) {
            hasData = true;

            // デシメーションとアンチエイリアシング
            m_decimator.Process(rawBlock, decimatedBlock);
            decimatedBlock.firstOutputFrameIndex = currentOutputFrameIndex;
            currentOutputFrameIndex += decimatedBlock.frameCount;

            // 1. ファイル保存スレッドへ配送（最優先・必須）
            if (m_loggerWorker && m_loggerWorker->IsRunning()) {
                m_loggerWorker->EnqueueBlock(decimatedBlock);
            }

            // 2. GUI描画用キューへ配送（ドロップ許容）
            if (m_uiAudioQueue) {
                if (!m_uiAudioQueue->try_enqueue(decimatedBlock)) {
                    // UIキューが一杯の場合は安全にドロップ（SPSC契約維持のためプロデューサ側ではdequeueしない）
                    m_uiFramesDropped.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        if (!hasData) {
            if (m_stopDecimator.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool AudioDeviceManager::TryDequeueUIBlock(AudioBlock& block) {
    if (!m_uiAudioQueue) return false;
    return m_uiAudioQueue->try_dequeue(block);
}

LoggerStats AudioDeviceManager::GetStats() const {
    LoggerStats stats;
    stats.totalFramesCaptured = m_totalFramesCaptured.load(std::memory_order_relaxed);
    stats.bufferOverruns = m_rawBufferOverruns.load(std::memory_order_relaxed);
    stats.uiFramesDropped = m_uiFramesDropped.load(std::memory_order_relaxed);
    stats.isStreaming = m_isStreaming.load(std::memory_order_relaxed);
    return stats;
}

void AudioDeviceManager::StreamFinishedCallbackBridge(void* userData) {
    auto* self = static_cast<AudioDeviceManager*>(userData);
    if (self) {
        self->OnStreamFinished();
    }
}

void AudioDeviceManager::OnStreamFinished() {
    // ユーザーによる停止要求ではない場合のストリーム終了（USB切断等）を検知
    if (m_isStreaming.load(std::memory_order_relaxed) && !m_stopRequested.load(std::memory_order_relaxed)) {
        m_hasDeviceError.store(true, std::memory_order_relaxed);
    }
}

bool AudioDeviceManager::HasDeviceError() const {
    if (m_hasDeviceError.load(std::memory_order_relaxed)) {
        return true;
    }
    // ストリーム稼働中に明示的な停止要求がないにもかかわらずストリームが非アクティブになった場合を検出
    if (m_isStreaming.load(std::memory_order_relaxed) && !m_stopRequested.load(std::memory_order_relaxed) && m_paStream) {
        int active = Pa_IsStreamActive(m_paStream);
        if (active <= 0) {
            m_hasDeviceError.store(true, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

} // namespace dlog
