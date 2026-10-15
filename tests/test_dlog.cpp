#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <cmath>
#include <fstream>
#include <vector>
#include <filesystem>
#include <cstring>

#include "audio/audio_types.h"
#include "dsp/decimator_processor.h"
#include "dsp/calibration_calculator.h"
#include "storage/data_logger_worker.h"
#include "audio/audio_device_mgr.h"
#include "ui/time_axis_calculator.h"
#include "playback/playback_time_parser.h"
#include "playback/playback_loader_worker.h"
#include "dlog_reader.h"
#include "common/localized_strings.h"
#include "common/language_preferences.h"

using namespace dlog;

void TestHeaderSpecification() {
    std::cout << "[Test 1] Testing DLogFileHeader specification..." << std::endl;
    assert(sizeof(DLogFileHeader) == 64);

    DLogFileHeader h;
    assert(h.magic[0] == 'D' && h.magic[1] == 'L' && h.magic[2] == 'O' && h.magic[3] == 'G');
    assert(h.headerSize == 64);
    assert(h.version == DLOG_FORMAT_VERSION);
    std::cout << "  -> DLogFileHeader is exactly 64 bytes and magic is 'DLOG'. PASS!" << std::endl;
}

void TestDecimatorProcessor() {
    std::cout << "[Test 2] Testing DecimatorProcessor (Downsampling & Anti-aliasing)..." << std::endl;

    DecimatorProcessor decimator;
    const int channels = 4;
    const int factor = 2;
    decimator.Initialize(channels, factor);
    assert(decimator.GetFactor() == 2);

    AudioBlock inBlock;
    inBlock.channels = channels;
    inBlock.frameCount = 512;
    inBlock.sampleRate = 44100.0;
    inBlock.timestampUtcMs = 123456789;

    // 4ch の正弦波テスト信号を生成
    for (size_t f = 0; f < inBlock.frameCount; ++f) {
        for (int ch = 0; ch < channels; ++ch) {
            float freq = 1000.0f * static_cast<float>(ch + 1); // 1kHz, 2kHz, 3kHz, 4kHz
            inBlock.samples[f * channels + ch] = std::sin(
                2.0f * 3.14159265f * freq * static_cast<float>(f) / 44100.0f);
        }
    }

    AudioBlock outBlock;
    decimator.Process(inBlock, outBlock);

    assert(outBlock.channels == channels);
    assert(outBlock.frameCount == 256); // 512 / 2 = 256
    assert(outBlock.sampleRate == 22050.0);
    assert(outBlock.timestampUtcMs == inBlock.timestampUtcMs);

    // 出力サンプルの検証
    for (size_t f = 0; f < outBlock.frameCount; ++f) {
        for (int ch = 0; ch < channels; ++ch) {
            float val = outBlock.samples[f * channels + ch];
            assert(!std::isnan(val) && !std::isinf(val));
            assert(std::fabs(val) <= 1.5f); // フィルタ通過後も有限範囲内
        }
    }
    std::cout << "  -> Decimation 44.1kHz -> 22.05kHz (4ch, 512 -> 256 frames). PASS!" << std::endl;
}

void TestDataLoggerWorker() {
    std::cout << "[Test 3] Testing DataLoggerWorker (Binary logging & stats)..." << std::endl;

    DataLoggerWorker worker;
    LoggerConfig config;
    config.channels = 2;
    config.nativeSampleRate = 44100.0;
    config.decimationFactor = 2;
    config.outputFilePath = "test_output.bin";
    config.sampleFormat = SampleFormatType::Float32;

    bool startOk = worker.Start(config);
    std::cout << "  Start returned: " << startOk << std::endl;
    assert(startOk);
    assert(worker.IsRunning());

    // 10 ブロック（各 256 フレーム, 2ch）をエンキュー
    const size_t numBlocks = 10;
    const size_t framesPerBlock = 256;
    for (size_t i = 0; i < numBlocks; ++i) {
        AudioBlock b;
        b.channels = 2;
        b.frameCount = framesPerBlock;
        b.sampleRate = 22050.0;
        for (size_t f = 0; f < framesPerBlock; ++f) {
            b.samples[f * 2 + 0] = static_cast<float>(static_cast<double>(i) * 0.1);
            b.samples[f * 2 + 1] = static_cast<float>(-static_cast<double>(i) * 0.1);
        }
        bool enqueued = worker.EnqueueBlock(b);
        assert(enqueued);
    }
    std::cout << "  Enqueued blocks" << std::endl;

    // 少し待機してワーカースレッドに書き込ませる
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    worker.Stop();
    std::cout << "  Worker stopped" << std::endl;
    assert(!worker.IsRunning());

    LoggerStats stats = worker.GetStats();
    std::cout << "  Logged frames: " << stats.totalFramesLogged 
              << ", Bytes: " << stats.totalBytesWritten << std::endl;
    assert(stats.totalFramesLogged == numBlocks * framesPerBlock);
    size_t expectedPcmBytes = numBlocks * framesPerBlock * 2 * sizeof(float);
    size_t expectedTotalBytes = sizeof(DLogFileHeader) + expectedPcmBytes;
    assert(stats.totalBytesWritten == expectedTotalBytes);
    assert(stats.bufferOverruns == 0);

    // 生成されたファイルを検証
    std::ifstream in("test_output.bin", std::ios::binary);
    assert(in.is_open());

    DLogFileHeader header;
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    assert(std::string(header.magic, 4) == "DLOG");
    assert(header.version == DLOG_FORMAT_VERSION);
    assert(header.channels == 2);
    assert(header.sampleRate == 22050);
    assert(header.decimationFactor == 2);
    assert(header.nativeRate == 44100);

    // PCM データを読み込んでサイズ検証
    in.seekg(0, std::ios::end);
    size_t fileSize = in.tellg();
    assert(fileSize == expectedTotalBytes);
    in.close();

    std::cout << "  -> Written " << fileSize << " bytes successfully. Binary header & payload verified. PASS!" << std::endl;
}

void TestAudioDeviceManagerEnumeration() {
    std::cout << "[Test 4] Testing AudioDeviceManager device enumeration..." << std::endl;
    AudioDeviceManager mgr;
    auto devices = mgr.GetInputDevices();
    std::cout << "  -> Found " << devices.size() << " audio input device(s):" << std::endl;
    for (const auto& dev : devices) {
        std::cout << "     [" << dev.id << "] " << dev.name << " ("
                  << dev.maxInputChannels << " ch, API: " << dev.hostApiName << ")" << std::endl;
    }
    std::cout << "  -> Device enumeration test PASS!" << std::endl;
}

void TestDataLoggerJapanesePathAndError() {
    std::cout << "[Test 5] Testing DataLoggerWorker with Japanese path..." << std::endl;
    DataLoggerWorker worker;
    LoggerConfig config;
    config.channels = 2;
    config.nativeSampleRate = 44100.0;
    config.decimationFactor = 2;
    config.outputFilePath = "テスト_録音データ_2026.bin";
    config.sampleFormat = SampleFormatType::Float32;

    bool startOk = worker.Start(config);
    assert(startOk);
    assert(!worker.HasDiskError());

    AudioBlock b;
    b.channels = 2;
    b.frameCount = 128;
    b.sampleRate = 22050.0;
    for (size_t f = 0; f < 128; ++f) {
        b.samples[f * 2 + 0] = 0.5f;
        b.samples[f * 2 + 1] = -0.5f;
    }
    bool enqOk = worker.EnqueueBlock(b);
    assert(enqOk);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    worker.Stop();
    assert(!worker.HasDiskError());

    // ファイルが存在し、ヘッダが読めるか検証
    std::ifstream in(std::filesystem::u8path("テスト_録音データ_2026.bin"), std::ios::binary);
    assert(in.is_open());
    DLogFileHeader h;
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    assert(std::string(h.magic, 4) == "DLOG");
    in.close();
    std::filesystem::remove(std::filesystem::u8path("テスト_録音データ_2026.bin"));
    std::filesystem::remove(std::filesystem::u8path("test_output.bin"));
    std::cout << "  -> Japanese path logging & cleanup PASS!" << std::endl;
}

void TestAudioDeviceManagerErrorTracking() {
    std::cout << "[Test 6] Testing AudioDeviceManager device error tracking..." << std::endl;
    AudioDeviceManager mgr;
    assert(!mgr.HasDeviceError());
    assert(!mgr.IsStreaming());

    // 異常終了コールバックのエミュレーション（未ストリーミング状態）
    mgr.OnStreamFinished();
    assert(!mgr.HasDeviceError());

    std::cout << "  -> AudioDeviceManager error tracking initial state PASS!" << std::endl;
}

void TestDecimation1HzAndVoltageHeader() {
    std::cout << "[Test 7] Testing 1 Hz Decimation & Full-Scale Voltage Header..." << std::endl;

    // 1. 1 Hz Boxcar デシメータの検証 (44,100 Hz -> 1 Hz, factor = 44100)
    DecimatorProcessor decimator;
    decimator.Initialize(2, 44100);
    assert(decimator.GetFactor() == 44100);

    // 512 サンプルのブロックを 86 回 (44,032 samples) + 1 回 (68 samples) = 44,100 samples 送信
    AudioBlock block;
    block.channels = 2;
    block.frameCount = 512;
    block.sampleRate = 44100.0;
    for (size_t f = 0; f < 512; ++f) {
        block.samples[f * 2 + 0] = 0.50f;
        block.samples[f * 2 + 1] = -0.25f;
    }

    AudioBlock outBlock;
    for (int i = 0; i < 86; ++i) {
        decimator.Process(block, outBlock);
        assert(outBlock.frameCount == 0); // まだ 44,100 サンプルに達していないため 0 フレーム
    }

    block.frameCount = 68; // 残り 68 サンプル
    decimator.Process(block, outBlock);
    assert(outBlock.frameCount == 1); // ちょうど 44,100 サンプル蓄積で 1 フレーム出力
    assert(outBlock.sampleRate == 1.0);
    assert(std::fabs(outBlock.samples[0] - 0.50f) < 1e-4f);
    assert(std::fabs(outBlock.samples[1] - (-0.25f)) < 1e-4f);

    // 2. DataLoggerWorker の fullScaleVoltage ヘッダ記録検証
    DataLoggerWorker worker;
    LoggerConfig config;
    config.channels = 2;
    config.nativeSampleRate = 44100.0;
    config.customTargetSampleRate = 1.0;
    config.decimationFactor = 44100;
    config.fullScaleVoltage = 5.0; // ±5.0 V
    config.outputFilePath = "test_voltage_header.bin";
    config.sampleFormat = SampleFormatType::Float32;

    bool startOk = worker.Start(config);
    assert(startOk);
    worker.Stop();

    std::ifstream in("test_voltage_header.bin", std::ios::binary);
    assert(in.is_open());
    DLogFileHeader h;
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    assert(std::string(h.magic, 4) == "DLOG");
    assert(h.sampleRate == 1);
    assert(h.decimationFactor == 44100);
    assert(std::fabs(h.fullScaleVoltage - 5.0) < 1e-6);
    in.close();
    std::filesystem::remove("test_voltage_header.bin");

    std::cout << "  -> 1 Hz boxcar averaging (44100:1) & Full-Scale Voltage Header (5.0V) PASS!" << std::endl;
}

void TestChannelMetadataConfig() {
    std::cout << "[Test 8] Testing ChannelMetadata in LoggerConfig..." << std::endl;
    LoggerConfig config;
    config.channels = 3;
    config.channelMetadata = {
        {"V_batt", "V"},
        {"I_load", "A"},
        {"Temp", "degC"}
    };
    assert(config.channelMetadata.size() == 3);
    assert(config.channelMetadata[0].name == "V_batt" && config.channelMetadata[0].unit == "V");
    assert(config.channelMetadata[1].name == "I_load" && config.channelMetadata[1].unit == "A");
    assert(config.channelMetadata[2].name == "Temp" && config.channelMetadata[2].unit == "degC");
    std::cout << "  -> ChannelMetadata configuration PASS!" << std::endl;
}

void TestDecimation48kHzAndTargetRates() {
    std::cout << "[Test 9] Testing 48 kHz / 96 kHz Decimation & LoggerConfig Target Rates..." << std::endl;

    // 1. 48 kHz -> 24 kHz (1/2, factor = 2)
    {
        DecimatorProcessor decimator;
        decimator.Initialize(2, 2);
        assert(decimator.GetFactor() == 2);

        AudioBlock inBlock;
        inBlock.channels = 2;
        inBlock.frameCount = 512;
        inBlock.sampleRate = 48000.0;
        inBlock.timestampUtcMs = 10000;
        for (size_t f = 0; f < 512; ++f) {
            inBlock.samples[f * 2 + 0] = 0.8f;
            inBlock.samples[f * 2 + 1] = -0.4f;
        }

        AudioBlock outBlock;
        decimator.Process(inBlock, outBlock);
        assert(outBlock.frameCount == 256);
        assert(outBlock.sampleRate == 24000.0);
        // FIR過渡応答（17タップ）収束後の定常値を確認
        assert(std::fabs(outBlock.samples[50 * 2 + 0] - 0.8f) < 0.05f);
        assert(std::fabs(outBlock.samples[50 * 2 + 1] - (-0.4f)) < 0.05f);
    }

    // 2. 48 kHz -> 1000 Hz (customRate = 1000.0, factor = 48)
    {
        DecimatorProcessor decimator;
        decimator.Initialize(1, 48);
        assert(decimator.GetFactor() == 48);

        AudioBlock inBlock;
        inBlock.channels = 1;
        inBlock.frameCount = 480;
        inBlock.sampleRate = 48000.0;
        for (size_t f = 0; f < 480; ++f) {
            inBlock.samples[f] = 0.5f;
        }

        AudioBlock outBlock;
        decimator.Process(inBlock, outBlock);
        assert(outBlock.frameCount == 10);
        assert(outBlock.sampleRate == 1000.0);
        assert(std::fabs(outBlock.samples[0] - 0.5f) < 1e-4f);
    }

    // 3. 96 kHz -> 24 kHz (1/4, factor = 4)
    {
        DecimatorProcessor decimator;
        decimator.Initialize(2, 4);
        assert(decimator.GetFactor() == 4);

        AudioBlock inBlock;
        inBlock.channels = 2;
        inBlock.frameCount = 512;
        inBlock.sampleRate = 96000.0;
        for (size_t f = 0; f < 512; ++f) {
            inBlock.samples[f * 2 + 0] = 0.2f;
            inBlock.samples[f * 2 + 1] = -0.2f;
        }

        AudioBlock outBlock;
        decimator.Process(inBlock, outBlock);
        assert(outBlock.frameCount == 128);
        assert(outBlock.sampleRate == 24000.0);
    }

    // 4. LoggerConfig::getTargetSampleRate() 検証
    {
        LoggerConfig cfg48;
        cfg48.nativeSampleRate = 48000.0;
        cfg48.decimationFactor = 2;
        assert(cfg48.getTargetSampleRate() == 24000.0);

        cfg48.decimationFactor = 48;
        cfg48.customTargetSampleRate = 1000.0;
        assert(cfg48.getTargetSampleRate() == 1000.0);

        LoggerConfig cfg96;
        cfg96.nativeSampleRate = 96000.0;
        cfg96.decimationFactor = 4;
        assert(cfg96.getTargetSampleRate() == 24000.0);
    }

    std::cout << "  -> 48 kHz / 96 kHz decimation & target sample rates PASS!" << std::endl;
}

void TestTransactionalOutputAndValidation() {
    std::cout << "[Test 10] Testing transactional output preservation and validation..." << std::endl;
    const std::filesystem::path outputPath = "transactional_output.bin";
    const std::filesystem::path partialPath = "transactional_output.bin.partial";
    std::filesystem::remove(outputPath);
    std::filesystem::remove(partialPath);

    const std::string original = "existing measurement must survive";
    {
        std::ofstream out(outputPath, std::ios::binary | std::ios::trunc);
        out.write(original.data(), static_cast<std::streamsize>(original.size()));
    }

    LoggerConfig config;
    config.channels = 2;
    config.nativeSampleRate = 44100.0;
    config.decimationFactor = 44;
    config.outputFilePath = outputPath.u8string();

    {
        DataLoggerWorker worker;
        assert(worker.Start(config));
        const std::filesystem::path activePartial = std::filesystem::u8path(worker.GetRecoveryFilePath());
        assert(std::filesystem::exists(activePartial));

        std::ifstream beforeAbort(outputPath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(beforeAbort)), std::istreambuf_iterator<char>());
        beforeAbort.close();
        assert(contents == original);

        worker.Abort();
        assert(!std::filesystem::exists(activePartial));
        std::ifstream afterAbort(outputPath, std::ios::binary);
        contents.assign(std::istreambuf_iterator<char>(afterAbort), std::istreambuf_iterator<char>());
        afterAbort.close();
        assert(contents == original);
    }

    {
        DataLoggerWorker worker;
        assert(worker.Start(config));
        worker.Stop();
        assert(!worker.HasDiskError());

        std::ifstream in(outputPath, std::ios::binary);
        DLogFileHeader header;
        in.read(reinterpret_cast<char*>(&header), sizeof(header));
        in.close();
        assert(header.version == DLOG_FORMAT_VERSION);
        assert(header.channels == 2);
        assert(header.sampleRate == 1002);
        assert(std::fabs(GetDLogEffectiveSampleRate(header) - (44100.0 / 44.0)) < 1e-9);
    }

    {
        DataLoggerWorker worker;
        LoggerConfig invalid = config;
        invalid.channels = 9;
        invalid.outputFilePath = "invalid_9ch.bin";
        assert(!worker.Start(invalid));
        assert(!std::filesystem::exists("invalid_9ch.bin"));
    }

    std::filesystem::remove(outputPath);
    std::filesystem::remove(partialPath);
    std::cout << "  -> Existing output preservation, v2 rate metadata, and 8ch limit PASS!" << std::endl;
}

void TestPortAudioOverflowStatusCounting() {
    std::cout << "[Test 11] Testing PortAudio input-overflow status counting..." << std::endl;
    AudioDeviceManager mgr;
    float samples[2 * 16] = {};
    int result = mgr.AudioCallback(samples, nullptr, 16, nullptr, paInputOverflow);
    assert(result == paContinue);
    assert(mgr.GetStats().bufferOverruns == 1);
    std::cout << "  -> paInputOverflow is reflected in the dropout counter. PASS!" << std::endl;
}

void TestInvalidAudioBlockRejection() {
    std::cout << "[Test 12] Testing invalid audio block rejection..." << std::endl;

    DecimatorProcessor decimator;
    decimator.Initialize(8, 2);
    AudioBlock invalidInput;
    invalidInput.channels = 9;
    invalidInput.frameCount = 1;
    invalidInput.sampleRate = 48000.0;
    AudioBlock output;
    decimator.Process(invalidInput, output);
    assert(output.channels == 8);
    assert(output.frameCount == 0);

    DataLoggerWorker worker;
    LoggerConfig config;
    config.channels = 2;
    config.outputFilePath = "invalid_block.bin";
    assert(worker.Start(config));
    AudioBlock wrongChannels;
    wrongChannels.channels = 8;
    wrongChannels.frameCount = 1;
    assert(!worker.EnqueueBlock(wrongChannels));
    assert(worker.GetStats().bufferOverruns == 1);
    worker.Abort();
    assert(!std::filesystem::exists("invalid_block.bin"));

    std::cout << "  -> DSP and logger reject out-of-contract block dimensions. PASS!" << std::endl;
}

void TestCandidateRatesAndFallbackSelection() {
    std::cout << "[Test 13] Testing Candidate Sample Rates & Selection Fallback..." << std::endl;

    // 1. レート候補定数配列の検証 (要求 1)
    assert(NUM_CANDIDATE_SAMPLE_RATES == 6);
    assert(CANDIDATE_SAMPLE_RATES[0] == 32000.0);
    assert(CANDIDATE_SAMPLE_RATES[1] == 44100.0);
    assert(CANDIDATE_SAMPLE_RATES[2] == 48000.0);
    assert(CANDIDATE_SAMPLE_RATES[3] == 88200.0);
    assert(CANDIDATE_SAMPLE_RATES[4] == 96000.0);
    assert(CANDIDATE_SAMPLE_RATES[5] == 192000.0);

    // 2. 空リストの場合
    assert(SelectAppropriateSampleRate({}, 48000.0, 44100.0) == 0.0);

    // 3. 以前の選択値が維持できる場合 (要求 5: 優先度 1)
    std::vector<double> rates1 = {32000.0, 44100.0, 48000.0, 96000.0};
    assert(SelectAppropriateSampleRate(rates1, 48000.0, 44100.0) == 48000.0);
    assert(SelectAppropriateSampleRate(rates1, 32000.0, 44100.0) == 32000.0);

    // 4. 以前の選択値が維持できず、デバイスの defaultSampleRate が利用可能な場合 (要求 5: 優先度 2)
    assert(SelectAppropriateSampleRate(rates1, 88200.0, 48000.0) == 48000.0);

    // 5. 以前の値も defaultSampleRate も利用不可だが 44100 Hz がある場合 (要求 5: 優先度 3)
    // 32 kHz が配列先頭にあっても、無条件に 32 kHz にならず 44.1 kHz が選ばれること！
    assert(SelectAppropriateSampleRate(rates1, 88200.0, 192000.0) == 44100.0);
    assert(SelectAppropriateSampleRate(rates1, 0.0, 0.0) == 44100.0);

    // 6. 44100 Hz もない場合、最初の対応レートが選ばれる (要求 5: 優先度 4)
    std::vector<double> rates2 = {32000.0, 48000.0, 96000.0};
    assert(SelectAppropriateSampleRate(rates2, 88200.0, 44100.0) == 32000.0);

    std::cout << "  -> Candidate rates and fallback priority selection logic PASS!" << std::endl;
}

void TestDecimation32kHzTo1000HzAndHeader() {
    std::cout << "[Test 14] Testing 32 kHz Recording, 1/32 Decimation (1000 Hz) & DLOG Header..." << std::endl;

    // 1. 32 kHz から 1/32 へのデシメーション (32000 / 32 = 1000 Hz)
    DecimatorProcessor decimator;
    const int channels = 2;
    const int factor = 32;
    decimator.Initialize(channels, factor);
    assert(decimator.GetFactor() == 32);

    AudioBlock inBlock;
    inBlock.channels = channels;
    inBlock.frameCount = 1024;
    inBlock.sampleRate = 32000.0;
    inBlock.timestampUtcMs = 1700000000000LL;

    for (size_t f = 0; f < inBlock.frameCount; ++f) {
        float val = std::sin(2.0f * 3.14159265f * 100.0f * static_cast<float>(f) / 32000.0f);
        inBlock.samples[f * channels + 0] = val;
        inBlock.samples[f * channels + 1] = -val;
    }

    AudioBlock outBlock;
    decimator.Process(inBlock, outBlock);

    assert(outBlock.channels == channels);
    assert(outBlock.frameCount == 1024 / 32); // 32 frames
    assert(outBlock.sampleRate == 1000.0);   // 正確に 1000 Hz
    assert(outBlock.timestampUtcMs == inBlock.timestampUtcMs);

    for (size_t f = 0; f < outBlock.frameCount; ++f) {
        for (int ch = 0; ch < channels; ++ch) {
            float val = outBlock.samples[f * channels + ch];
            assert(!std::isnan(val) && !std::isinf(val));
            assert(std::fabs(val) <= 1.5f);
        }
    }

    // 2. DataLoggerWorker での 32 kHz 記録とヘッダ検証
    DataLoggerWorker worker;
    LoggerConfig config;
    config.channels = channels;
    config.nativeSampleRate = 32000.0;
    config.decimationFactor = 32;
    config.customTargetSampleRate = 1000.0;
    config.outputFilePath = "test_32k_output.bin";
    config.sampleFormat = SampleFormatType::Float32;

    assert(worker.Start(config));
    assert(worker.EnqueueBlock(outBlock));
    worker.Stop();

    // 出力バイナリヘッダの確認
    std::ifstream in("test_32k_output.bin", std::ios::binary);
    assert(in.is_open());
    DLogFileHeader h;
    in.read(reinterpret_cast<char*>(&h), sizeof(h));
    in.close();

    assert(h.nativeRate == 32000);
    assert(h.decimationFactor == 32);
    assert(h.sampleRate == 1000);
    assert(GetDLogEffectiveSampleRate(h) == 1000.0);

    std::filesystem::remove("test_32k_output.bin");
    std::cout << "  -> 32 kHz to 1/32 decimation is exactly 1000 Hz, header nativeRate == 32000. PASS!" << std::endl;
}

void TestCheckInputFormatSupportAPI() {
    std::cout << "[Test 15] Testing AudioDeviceManager::CheckInputFormatSupport API..." << std::endl;

    AudioDeviceManager mgr;

    // 不正なデバイスID
    PaError errDev = mgr.CheckInputFormatSupport(-999, 2, 44100.0);
    assert(errDev == paInvalidDevice);

    // 不正なチャネル数
    PaError errCh0 = mgr.CheckInputFormatSupport(mgr.GetDefaultInputDevice(), 0, 44100.0);
    assert(errCh0 == paInvalidChannelCount || errCh0 == paInvalidDevice);
    PaError errCh9 = mgr.CheckInputFormatSupport(mgr.GetDefaultInputDevice(), 99, 44100.0);
    assert(errCh9 == paInvalidChannelCount || errCh9 == paInvalidDevice);

    // デバイスが存在する場合の照会動作
    auto devices = mgr.GetInputDevices();
    if (!devices.empty()) {
        int devId = devices[0].id;
        PaError err = mgr.CheckInputFormatSupport(devId, 1, 44100.0);
        std::cout << "  -> Device 0 support query for 44.1kHz: " << Pa_GetErrorText(err) << std::endl;
        assert(err == paFormatIsSupported || err == paInvalidSampleRate || err < 0);
    }

    std::cout << "  -> CheckInputFormatSupport API contract and error codes. PASS!" << std::endl;
}

void TestTimeAxisCalculations() {
    std::cout << "[Test 16] Testing TimeAxisCalculator (Time scale, range, labels, drops)..." << std::endl;

    // 1. 未記録状態 (hasData == false) では中心時刻が "—"
    TimeRangeInfo noDataRange = CalculateTimeRange(0, false, 2.0, 32000.0);
    assert(!noDataRange.hasData);
    assert(noDataRange.centerTimeString == "\u2014");

    // 2. 32 kHzかつ時間幅2秒のとき、右端が10.0秒なら中心時刻が9.000000秒、時間スケールが200 ms/divになること
    // 右端 10.0 秒: lastFrameIndex + 1 = 10.0 * 32000 = 320000 -> lastFrameIndex = 319999
    TimeScaleInfo scale2s = CalculateTimeScale(2.0);
    assert(scale2s.scaleString == "200 ms/div");
    assert(scale2s.scaleUnit == "ms");
    assert(scale2s.divLabels.size() == 11);
    assert(scale2s.divLabels[0] == "-1000");
    assert(scale2s.divLabels[5] == "0");
    assert(scale2s.divLabels[10] == "+1000 ms");

    TimeRangeInfo range32k = CalculateTimeRange(319999, true, 2.0, 32000.0);
    assert(range32k.hasData);
    assert(std::fabs(range32k.rightTime - 10.0) < 1e-6);
    assert(std::fabs(range32k.leftTime - 8.0) < 1e-6);
    assert(std::fabs(range32k.centerTime - 9.0) < 1e-6);
    assert(range32k.centerTimeString == "9.000000 s");

    // 3. 32 kHzかつ時間幅1 msのとき、時間スケールが100 μs/divになること
    TimeScaleInfo scale1ms = CalculateTimeScale(0.001);
    assert(scale1ms.scaleString == "100 \u03BCs/div");
    assert(scale1ms.scaleUnit == "\u03BCs");
    assert(scale1ms.divLabels.size() == 11);
    assert(scale1ms.divLabels[0] == "-500");
    assert(scale1ms.divLabels[5] == "0");
    assert(scale1ms.divLabels[10] == "+500 \u03BCs");

    // 4. 10 s のとき、1 s/div
    TimeScaleInfo scale10s = CalculateTimeScale(10.0);
    assert(scale10s.scaleString == "1 s/div");
    assert(scale10s.scaleUnit == "s");
    assert(scale10s.divLabels[0] == "-5");
    assert(scale10s.divLabels[5] == "0");
    assert(scale10s.divLabels[10] == "+5 s");

    // 5. 44.1 kHzから1000 Hzへのデシメーション（間引き率44、実際は44100/44 = 1002.2727... Hz）のような
    //    非整数レートでも、累積インデックスとFsの除算により中心時刻の誤差が累積しないこと
    double nativeRate = 44100.0;
    int decFactor = 44;
    double exactFs = nativeRate / static_cast<double>(decFactor); // 1002.272727... Hz
    uint64_t oneHourFrames = static_cast<uint64_t>(std::round(exactFs * 3600.0));
    uint64_t lastFrame1Hour = oneHourFrames - 1;
    TimeRangeInfo range1Hour = CalculateTimeRange(lastFrame1Hour, true, 2.0, exactFs);
    assert(std::fabs(range1Hour.rightTime - 3600.0) < 0.001);
    assert(std::fabs(range1Hour.centerTime - 3599.0) < 0.001);
    assert(range1Hour.centerTimeString.rfind("3599.000", 0) == 0);

    // 6. AudioBlock の firstOutputFrameIndex と UI破棄ギャップのシミュレーション検証
    const size_t capacity = 1000;
    std::vector<uint64_t> frameSlots(capacity, UINT64_MAX);
    std::vector<float> sampleSlots(capacity, 0.0f);

    AudioBlock b0;
    b0.firstOutputFrameIndex = 0;
    b0.frameCount = 100;
    for (size_t i = 0; i < b0.frameCount; ++i) {
        uint64_t f = b0.firstOutputFrameIndex + i;
        frameSlots[f % capacity] = f;
        sampleSlots[f % capacity] = 0.5f;
    }

    // ブロック1 (firstOutputFrameIndex = 100, frameCount = 100) はUIキューでドロップ

    AudioBlock b2;
    b2.firstOutputFrameIndex = 200;
    b2.frameCount = 100;
    for (size_t i = 0; i < b2.frameCount; ++i) {
        uint64_t f = b2.firstOutputFrameIndex + i;
        frameSlots[f % capacity] = f;
        sampleSlots[f % capacity] = 0.8f;
    }

    // ドロップされたフレーム 100..199 が有効として誤認識されないことを検証
    for (uint64_t f = 100; f < 200; ++f) {
        assert(frameSlots[f % capacity] != f);
    }
    // 受信したフレーム 0..99 および 200..299 は正しく有効であること
    for (uint64_t f = 0; f < 100; ++f) {
        assert(frameSlots[f % capacity] == f);
    }
    for (uint64_t f = 200; f < 300; ++f) {
        assert(frameSlots[f % capacity] == f);
    }

    std::cout << "  -> TimeAxisCalculator scale, range, non-integer rate & gap preservation. PASS!" << std::endl;
}

void TestPreviewModeAndTransitions() {
    std::cout << "[Test 17] Testing Preview Mode (Null Worker) and State Transitions..." << std::endl;

    AudioDeviceManager mgr;
    auto devices = mgr.GetInputDevices();

    // 1. 基本的なAPI・冪等性テスト
    mgr.StopStream(); // 未起動時の StopStream() がクラッシュしないこと
    mgr.StopStream(); // 連続呼び出しが安全であること
    assert(!mgr.IsStreaming());

    // 2. 有効な入力デバイスが存在する場合のプレビューおよび状態遷移テスト
    int targetDeviceId = -1;
    double supportedRate = 44100.0;
    int supportedChannels = 1;

    for (const auto& dev : devices) {
        if (dev.maxInputChannels >= 1) {
            // 対応レートを調査
            const double candidateRates[] = { 44100.0, 48000.0, 32000.0, 88200.0, 96000.0 };
            for (double rate : candidateRates) {
                if (mgr.CheckInputFormatSupport(dev.id, 1, rate) == paFormatIsSupported) {
                    targetDeviceId = dev.id;
                    supportedRate = rate;
                    supportedChannels = 1;
                    break;
                }
            }
            if (targetDeviceId != -1) break;
        }
    }

    if (targetDeviceId != -1) {
        LoggerConfig config;
        config.deviceId = targetDeviceId;
        config.channels = supportedChannels;
        config.nativeSampleRate = supportedRate;
        config.decimationFactor = 1;
        config.fullScaleVoltage = 1.0;
        config.outputFilePath = "test_preview_transition.bin";

        // --- ステップ A: プレビュー開始 (loggerWorker == nullptr) ---
        // プレビュー時はファイルが生成されないことを確認するため、既存ファイルを削除
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path("test_preview_transition.bin"), ec);
        std::filesystem::remove(std::filesystem::u8path("test_preview_transition.bin.meta"), ec);

        bool openOk = mgr.OpenStream(config, nullptr);
        assert(openOk);
        assert(mgr.StartStream());
        assert(mgr.IsStreaming());

        // プレビュー動作 (UIキューへの投入確認)
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        AudioBlock uiBlock;
        while (mgr.TryDequeueUIBlock(uiBlock)) {
            assert(uiBlock.channels == supportedChannels);
            assert(uiBlock.sampleRate == supportedRate);
            assert(!std::isnan(uiBlock.samples[0]));
        }
        // プレビュー中ファイルが生成されていないことを検証
        assert(!std::filesystem::exists(std::filesystem::u8path("test_preview_transition.bin")));
        assert(!std::filesystem::exists(std::filesystem::u8path("test_preview_transition.bin.meta")));

        // プレビュー停止 (最後の波形保持・多重停止の安全性)
        mgr.StopStream();
        mgr.StopStream();
        assert(!mgr.IsStreaming());

        // --- ステップ B: 停止中 → プレビュー → 記録開始 (プレビューから記録への移行) ---
        // プレビューを再開
        assert(mgr.OpenStream(config, nullptr));
        assert(mgr.StartStream());
        assert(mgr.IsStreaming());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // 記録開始: プレビューを停止してから記録ストリームを開始
        mgr.StopStream();
        assert(!mgr.IsStreaming());

        auto loggerWorker = std::make_shared<DataLoggerWorker>();
        assert(loggerWorker->Start(config));
        assert(mgr.OpenStream(config, loggerWorker));
        assert(mgr.StartStream());
        assert(mgr.IsStreaming());

        // 記録中の統計リセット検証 (新しいセッションで統計が初期化されていること)
        LoggerStats recStats = mgr.GetStats();
        assert(recStats.bufferOverruns == 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(120));

        // 記録停止 → 正常確定
        mgr.StopStream();
        loggerWorker->Stop();
        assert(!loggerWorker->HasDiskError());

        // 出力 DLOG ファイルの存在とヘッダ検証
        assert(std::filesystem::exists(std::filesystem::u8path("test_preview_transition.bin")));
        {
            std::ifstream in(std::filesystem::u8path("test_preview_transition.bin"), std::ios::binary);
            assert(in.is_open());
            DLogFileHeader h;
            in.read(reinterpret_cast<char*>(&h), sizeof(h));
            assert(std::string(h.magic, 4) == "DLOG");
            assert(h.channels == static_cast<uint32_t>(supportedChannels));
        }

        // --- ステップ C: 記録停止 → プレビュー再開 ---
        // 記録終了後、同じ入力設定でプレビューを自動再開
        assert(mgr.OpenStream(config, nullptr));
        assert(mgr.StartStream());
        assert(mgr.IsStreaming());

        // 新しいプレビューセッションで統計がリセットされていること
        LoggerStats previewStats = mgr.GetStats();
        assert(previewStats.bufferOverruns == 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        mgr.StopStream();
        assert(!mgr.IsStreaming());

        // クリーンアップ
        std::filesystem::remove(std::filesystem::u8path("test_preview_transition.bin"), ec);

        // --- ステップ D: 記録開始失敗時の復旧 (エラー時にプレビューを再開) ---
        // プレビュー中に、無効な保存パスで記録開始を試行して失敗するケース
        assert(mgr.OpenStream(config, nullptr));
        assert(mgr.StartStream());
        assert(mgr.IsStreaming());

        // 記録開始を試みるためプレビュー停止
        mgr.StopStream();

        // 確実に作成失敗するパス（通常ファイルを親ディレクトリとして指定）
        std::filesystem::path dummyFile = "test_cannot_be_parent_dir.txt";
        {
            std::ofstream f(dummyFile);
            f << "not a directory";
        }
        LoggerConfig invalidConfig = config;
        invalidConfig.outputFilePath = (dummyFile / "sub_dir" / "invalid.bin").u8string();
        auto failWorker = std::make_shared<DataLoggerWorker>();
        bool failStart = failWorker->Start(invalidConfig);
        std::filesystem::remove(dummyFile, ec);
        assert(!failStart); // 保存ファイル作成に失敗

        // 復旧: プレビューを再開できること
        assert(mgr.OpenStream(config, nullptr));
        assert(mgr.StartStream());
        assert(mgr.IsStreaming());
        mgr.StopStream();
        assert(!mgr.IsStreaming());
    } else {
        std::cout << "  (Note: No functional input audio device found, verified API robustness)" << std::endl;
    }

    std::cout << "  -> Preview mode (null worker), state transitions & recovery PASS!" << std::endl;
}

void TestPlaybackCoreAndClock() {
    std::cout << "[Test 18] Testing Playback Core, Time Parser, 64-bit Random Access, Generation Worker & Boundary cases..." << std::endl;

    // 1. Time parsing tests
    const double Fs = 1000.0;
    const uint64_t totalFrames = 10000; // 10.0 seconds
    const double maxPos = CalculateMaxPosition(totalFrames, Fs); // (10000 - 1) / 1000 = 9.999 s
    const double totalDur = CalculateTotalDuration(totalFrames, Fs); // 10.0 s

    assert(std::fabs(totalDur - 10.0) < 1e-9);
    assert(std::fabs(maxPos - 9.999) < 1e-9);

    double aligned = 0.0;
    std::string err;

    // 1.1 Pure seconds
    assert(ParsePlaybackTimeString("5", maxPos, Fs, aligned, err));
    assert(std::fabs(aligned - 5.0) < 1e-9);

    assert(ParsePlaybackTimeString("5.123", maxPos, Fs, aligned, err));
    assert(std::fabs(aligned - 5.123) < 1e-9);

    // Nearest sample alignment
    // Frame step is 0.001 s. Input 1.0004 s should align to 1.000 s. Input 1.0006 s should align to 1.001 s.
    assert(ParsePlaybackTimeString("1.0004", maxPos, Fs, aligned, err));
    assert(std::fabs(aligned - 1.000) < 1e-9);

    assert(ParsePlaybackTimeString("1.0006", maxPos, Fs, aligned, err));
    assert(std::fabs(aligned - 1.001) < 1e-9);

    // 1.2 MM:SS.fff
    assert(ParsePlaybackTimeString("00:05.500", maxPos, Fs, aligned, err));
    assert(std::fabs(aligned - 5.5) < 1e-9);

    // 1.3 HH:MM:SS.ffffff
    assert(ParsePlaybackTimeString("00:00:08.250", maxPos, Fs, aligned, err));
    assert(std::fabs(aligned - 8.25) < 1e-9);

    // 1.4 Strict validation: Rejection of negative numbers
    assert(!ParsePlaybackTimeString("-1.0", maxPos, Fs, aligned, err));
    assert(!err.empty());
    assert(!ParsePlaybackTimeString("-00:01:00", maxPos, Fs, aligned, err));

    // 1.5 Strict validation: Rejection of out of bounds (> maxPosition)
    assert(!ParsePlaybackTimeString("10.000", maxPos, Fs, aligned, err)); // maxPos is 9.999
    assert(!err.empty());
    assert(!ParsePlaybackTimeString("10.001", maxPos, Fs, aligned, err));
    assert(!ParsePlaybackTimeString("99:00:00", maxPos, Fs, aligned, err));

    // Exact maxPosition boundary check
    assert(ParsePlaybackTimeString("9.999", maxPos, Fs, aligned, err));
    assert(std::fabs(aligned - 9.999) < 1e-9);

    // Exact 0 boundary check
    assert(ParsePlaybackTimeString("0", maxPos, Fs, aligned, err));
    assert(std::fabs(aligned - 0.0) < 1e-9);

    // 1.6 Malformed strings and non-finite
    assert(!ParsePlaybackTimeString("abc", maxPos, Fs, aligned, err));
    assert(!ParsePlaybackTimeString("12:34:56:78", maxPos, Fs, aligned, err));
    assert(!ParsePlaybackTimeString("nan", maxPos, Fs, aligned, err));
    assert(!ParsePlaybackTimeString("inf", maxPos, Fs, aligned, err));
    assert(!ParsePlaybackTimeString("", maxPos, Fs, aligned, err));

    // 2. FormatPlaybackTime tests
    assert(FormatPlaybackTime(0.0) == "00:00:00.000");
    assert(FormatPlaybackTime(65.123) == "00:01:05.123");
    assert(FormatPlaybackTime(3661.050) == "01:01:01.050");
    assert(FormatPlaybackTime(65.123456, true) == "00:01:05.123456");

    // 3. 64-bit Frame Index & File Offset math
    // 8 channels, 4 bytes per sample = 32 bytes per frame.
    // Frame 1,000,000,000 -> offset = 64 + 1,000,000,000 * 32 = 32,000,000,064 (> 32 GB)
    uint64_t largeFrame = 1000000000ULL;
    uint64_t offset = FrameIndexToFileOffset(largeFrame, 64, 8, 4);
    assert(offset == 32000000064ULL);

    // Time to frame and frame to time roundtrip
    uint64_t fIdx = TimeToFrameIndex(5.123, 1000.0, totalFrames);
    assert(fIdx == 5123);
    double tBack = FrameIndexToTime(fIdx, 1000.0);
    assert(std::fabs(tBack - 5.123) < 1e-9);

    // 4. File-based testing with PlaybackLoaderWorker & DlogReader
    std::string testDlogPath = "test_playback_file.bin";
    std::string testMetaPath = "test_playback_file.bin.meta";
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(testDlogPath), ec);
    std::filesystem::remove(std::filesystem::u8path(testMetaPath), ec);

    const int channels = 2;
    const double testFs = 2000.0;
    const size_t testFrames = 4000; // 2.0 seconds

    // 4.1 Create test binary DLOG file
    {
        std::ofstream out(std::filesystem::u8path(testDlogPath), std::ios::binary);
        assert(out.is_open());

        DLogFileHeader header;
        std::memcpy(header.magic, "DLOG", 4);
        header.version = DLOG_FORMAT_VERSION;
        header.headerSize = sizeof(DLogFileHeader);
        header.channels = channels;
        header.sampleRate = static_cast<uint32_t>(testFs);
        header.sampleFormat = 0; // Float32
        header.nativeRate = static_cast<uint32_t>(testFs);
        header.decimationFactor = 1;
        header.startTimestampUtc = 1600000000000LL;
        header.fullScaleVoltage = 5.0; // 5.0 V full scale
        std::memset(header.reserved, 0, sizeof(header.reserved));

        out.write(reinterpret_cast<const char*>(&header), sizeof(header));

        std::vector<float> data(testFrames * channels);
        for (size_t f = 0; f < testFrames; ++f) {
            float t = static_cast<float>(f) / static_cast<float>(testFs);
            data[f * 2 + 0] = std::sin(2.0f * 3.14159265f * 10.0f * t); // 10 Hz sine
            data[f * 2 + 1] = std::cos(2.0f * 3.14159265f * 20.0f * t); // 20 Hz cosine
        }
        out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
    }

    // 4.2 Create companion .meta file
    {
        std::ofstream meta(std::filesystem::u8path(testMetaPath));
        meta << "[DLOG_METADATA]\n";
        meta << "magic=DLOG\n";
        meta << "version=2\n";
        meta << "channels=2\n";
        meta << "full_scale_voltage=5.0\n";
        meta << "sample_rate=2000\n";
        meta << "native_rate=2000\n";
        meta << "decimation_factor=1\n";
        meta << "start_timestamp_utc=1600000000000\n\n";
        meta << "[CH1]\nname=Voltage_A\nunit=V\n\n";
        meta << "[CH2]\nname=Current_B\nunit=mA\n\n";
    }

    // 4.3 Test PlaybackLoaderWorker open
    PlaybackLoaderWorker worker;
    std::string openErr;
    bool opened = worker.OpenFile(testDlogPath, openErr);
    if (!opened) {
        std::cerr << "OpenFile failed: " << openErr << std::endl;
    }
    assert(opened);
    assert(worker.IsOpen());
    assert(worker.GetChannels() == 2);
    assert(std::fabs(worker.GetSampleRate() - 2000.0) < 1e-4);
    assert(worker.GetTotalFrames() == testFrames);
    assert(std::fabs(worker.GetFullScaleVoltage() - 5.0) < 1e-4);
    assert(std::fabs(worker.GetDurationSec() - 2.0) < 1e-4);

    const auto& metaLoaded = worker.GetChannelMetadata();
    assert(metaLoaded.size() == 2);
    assert(metaLoaded[0].name == "Voltage_A");
    assert(metaLoaded[0].unit == "V");
    assert(metaLoaded[1].name == "Current_B");
    assert(metaLoaded[1].unit == "mA");

    // 4.4 Test RequestWindow & PollResult (Center = 1.0 s, Window = 0.5 s, pixelWidth = 100)
    uint64_t gen1 = worker.RequestWindow(1.0, 0.5, 100);
    assert(gen1 > 0);

    // Wait for worker result
    PlaybackWindowData res1;
    bool gotResult = false;
    for (int retry = 0; retry < 50; ++retry) {
        if (worker.PollResult(res1)) {
            gotResult = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(gotResult);
    assert(res1.generation == gen1);
    assert(!res1.hasError);
    assert(res1.channels == 2);
    assert(std::fabs(res1.centerTimeSec - 1.0) < 1e-9);
    assert(std::fabs(res1.timeWindowSec - 0.5) < 1e-9);
    assert(res1.frameCount > 0);
    assert(res1.peakAbsPerChannel.size() == 2);
    assert(res1.peakAbsPerChannel[0] > 0.5f); // Sine peak ~ 1.0

    // 4.5 Test Generation Counter Discarding
    // Fire rapid requests gen2, gen3, gen4.
    uint64_t gen2 = worker.RequestWindow(0.2, 0.2, 50);
    uint64_t gen3 = worker.RequestWindow(0.5, 0.2, 50);
    uint64_t gen4 = worker.RequestWindow(0.8, 0.2, 50);
    assert(gen4 > gen3 && gen3 > gen2);

    // Poll until latest result received
    PlaybackWindowData resLatest;
    gotResult = false;
    for (int retry = 0; retry < 50; ++retry) {
        if (worker.PollResult(resLatest)) {
            if (resLatest.generation == gen4) {
                gotResult = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(gotResult);
    assert(resLatest.generation == gen4);
    assert(std::fabs(resLatest.centerTimeSec - 0.8) < 1e-9);

    // 4.5b Test InvalidateGeneration and isNewEpoch
    uint64_t genOld = worker.RequestWindow(0.3, 0.2, 50, false);
    uint64_t invalidatedGen = worker.InvalidateGeneration();
    assert(invalidatedGen > genOld);
    uint64_t genNewEpoch = worker.RequestWindow(0.4, 0.2, 50, true);
    assert(genNewEpoch > invalidatedGen);
    gotResult = false;
    for (int retry = 0; retry < 50; ++retry) {
        if (worker.PollResult(resLatest)) {
            if (resLatest.generation == genNewEpoch) {
                gotResult = true;
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(gotResult);
    assert(resLatest.generation == genNewEpoch);
    assert(std::fabs(resLatest.centerTimeSec - 0.4) < 1e-9);

    // 4.6 Boundary Cases: Seeking before start and after end
    // Center = -0.5 s, window = 1.0 s ([center - win/2, center + win/2] = [-1.0, 0.0])
    uint64_t genBefore = worker.RequestWindow(-0.5, 1.0, 50);
    gotResult = false;
    PlaybackWindowData resBefore;
    for (int retry = 0; retry < 50; ++retry) {
        if (worker.PollResult(resBefore) && resBefore.generation == genBefore) {
            gotResult = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(gotResult);
    assert(!resBefore.hasError);

    // Center = 3.0 s, window = 1.0 s ([2.5, 3.5] > duration 2.0 s)
    uint64_t genAfter = worker.RequestWindow(3.0, 1.0, 50);
    gotResult = false;
    PlaybackWindowData resAfter;
    for (int retry = 0; retry < 50; ++retry) {
        if (worker.PollResult(resAfter) && resAfter.generation == genAfter) {
            gotResult = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(gotResult);
    assert(!resAfter.hasError);

    worker.CloseFile();
    assert(!worker.IsOpen());
    worker.Stop();

    // 4.7 Rejection of .partial file
    std::string partialPath = "test_data.bin.partial";
    {
        std::ofstream partialOut(std::filesystem::u8path(partialPath), std::ios::binary);
        partialOut << "dummy partial content";
    }
    DlogReader readerPartial;
    assert(!readerPartial.Open(partialPath));
    assert(!readerPartial.GetLastError().empty());
    std::filesystem::remove(std::filesystem::u8path(partialPath), ec);

    // 4.8 Rejection of 0-frame file (header only)
    std::string emptyFramesPath = "test_empty_frames.bin";
    {
        std::ofstream emptyOut(std::filesystem::u8path(emptyFramesPath), std::ios::binary);
        DLogFileHeader h;
        std::memcpy(h.magic, "DLOG", 4);
        h.version = DLOG_FORMAT_VERSION;
        h.headerSize = sizeof(DLogFileHeader);
        h.channels = 2;
        h.sampleRate = 44100;
        h.sampleFormat = 0;
        h.nativeRate = 44100;
        h.decimationFactor = 1;
        h.fullScaleVoltage = 1.0;
        emptyOut.write(reinterpret_cast<const char*>(&h), sizeof(h));
    }
    DlogReader readerEmpty;
    assert(!readerEmpty.Open(emptyFramesPath));
    assert(readerEmpty.GetLastErrorInfo().code == StringId::ErrZeroFramesInFile);
    assert(!readerEmpty.GetLastError().empty());
    std::filesystem::remove(std::filesystem::u8path(emptyFramesPath), ec);

    // 4.9 Single frame file test (boundary totalFrames == 1)
    std::string singleFramePath = "test_single_frame.bin";
    {
        std::ofstream sfOut(std::filesystem::u8path(singleFramePath), std::ios::binary);
        DLogFileHeader h;
        std::memcpy(h.magic, "DLOG", 4);
        h.version = DLOG_FORMAT_VERSION;
        h.headerSize = sizeof(DLogFileHeader);
        h.channels = 1;
        h.sampleRate = 1000;
        h.sampleFormat = 0;
        h.nativeRate = 1000;
        h.decimationFactor = 1;
        h.fullScaleVoltage = 1.0;
        sfOut.write(reinterpret_cast<const char*>(&h), sizeof(h));
        float smp = 0.5f;
        sfOut.write(reinterpret_cast<const char*>(&smp), sizeof(float));
    }
    DlogReader readerSingle;
    assert(readerSingle.Open(singleFramePath));
    assert(readerSingle.GetTotalFrames() == 1);
    assert(CalculateMaxPosition(1, 1000.0) == 0.0);
    assert(std::fabs(CalculateTotalDuration(1, 1000.0) - 0.001) < 1e-9);
    readerSingle.Close();
    std::filesystem::remove(std::filesystem::u8path(singleFramePath), ec);

    // Cleanup
    std::filesystem::remove(std::filesystem::u8path(testDlogPath), ec);
    std::filesystem::remove(std::filesystem::u8path(testMetaPath), ec);

    std::cout << "  -> Playback Core, Clock, Time Parser, 64-bit Offset, Worker Generation & Boundaries PASS!" << std::endl;
}

void TestPartialFileRecoveryAndParserValidation() {
    std::cout << "[Test 19] Testing .partial file recovery (FUNC-001) and strict time parser (UI-003)..." << std::endl;

    // 1. 厳格なタイムパーサー検証 (UI-003)
    double alignedSec = 0.0;
    std::string errMsg;
    assert(!ParsePlaybackTimeString("00:12abc:34", 100.0, 1000.0, alignedSec, errMsg));
    assert(!ParsePlaybackTimeString("00:65:00", 100.0, 1000.0, alignedSec, errMsg));
    assert(!ParsePlaybackTimeString("12:00:88", 100000.0, 1000.0, alignedSec, errMsg));
    assert(!ParsePlaybackTimeString("abc", 100.0, 1000.0, alignedSec, errMsg));
    assert(ParsePlaybackTimeString("00:01:23.500", 100.0, 1000.0, alignedSec, errMsg));
    assert(std::fabs(alignedSec - 83.5) < 1e-4);
    std::cout << "  -> Strict time parsing and rejection of non-numeric tokens: PASS!" << std::endl;

    // 2. .partial ファイルリカバリー検証 (FUNC-001)
    std::string partialPath = "test_interrupted.bin.partial";
    {
        std::ofstream pOut(partialPath, std::ios::binary | std::ios::trunc);
        DLogFileHeader h;
        std::memcpy(h.magic, "DLOG", 4);
        h.version = 2;
        h.headerSize = sizeof(DLogFileHeader);
        h.channels = 2;
        h.sampleRate = 1000;
        h.sampleFormat = 0;
        h.nativeRate = 1000;
        h.decimationFactor = 1;
        h.fullScaleVoltage = 1.0;
        pOut.write(reinterpret_cast<const char*>(&h), sizeof(h));

        // 10フレーム (10 * 2 * 4 = 80 bytes)
        std::vector<float> samples(20, 0.123f);
        pOut.write(reinterpret_cast<const char*>(samples.data()), samples.size() * sizeof(float));

        // 電源断で途切れた3バイトの不完全フレームを追記
        char junk[3] = {'\x01', '\x02', '\x03'};
        pOut.write(junk, 3);
    }

    std::string recoveredPath;
    std::string recErr;
    bool recOk = DlogReader::RecoverPartialFile(partialPath, recoveredPath, recErr);
    assert(recOk);
    assert(recoveredPath == "test_interrupted.bin");

    DlogReader recReader;
    assert(recReader.Open(recoveredPath));
    assert(recReader.GetTotalFrames() == 10);
    assert(recReader.GetHeader().channels == 2);
    recReader.Close();

    assert(std::filesystem::exists(std::filesystem::u8path(partialPath)));
    std::error_code ec;
    std::filesystem::remove(std::filesystem::u8path(recoveredPath), ec);
    std::filesystem::remove(std::filesystem::u8path(partialPath), ec);
    std::cout << "  -> .partial recovery trimmed trailing incomplete frame, restored clean DLOG file, and preserved original .partial: PASS!" << std::endl;
}

void TestPlaybackEpochIsolationAndDurationRetention() {
    std::cout << "[Test 20] Testing Playback epoch isolation and worker post-stop duration retention..." << std::endl;

    // 1. Worker post-stop duration retention (UI-002)
    {
        DataLoggerWorker worker;
        LoggerConfig config;
        config.channels = 2;
        config.nativeSampleRate = 1000.0;
        config.decimationFactor = 1;
        config.outputFilePath = "test_duration_retention.bin";
        config.sampleFormat = SampleFormatType::Float32;

        bool startOk = worker.Start(config);
        assert(startOk);

        // Enqueue 2 blocks (500 frames total = 0.5s)
        for (int i = 0; i < 2; ++i) {
            AudioBlock b;
            b.channels = 2;
            b.frameCount = 250;
            b.sampleRate = 1000.0;
            b.timestampUtcMs = 1000000;
            std::fill_n(b.samples, b.channels * b.frameCount, 0.5f);
            assert(worker.EnqueueBlock(b));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        worker.Stop();

        LoggerStats postStats = worker.GetStats();
        assert(postStats.wallClockDurationSec > 0.0);
        assert(postStats.dataDurationSec >= 0.5);
        assert(postStats.totalBytesWritten == sizeof(DLogFileHeader) + 500 * 2 * sizeof(float));

        std::error_code ec;
        std::filesystem::remove("test_duration_retention.bin", ec);
        std::cout << "  -> Worker post-stop duration retention: PASS!" << std::endl;
    }

    // 2. Playback epoch isolation (PLAY-002)
    {
        std::string testPlayFile = "test_play_epoch.bin";
        {
            std::ofstream out(testPlayFile, std::ios::binary | std::ios::trunc);
            DLogFileHeader h;
            std::memcpy(h.magic, "DLOG", 4);
            h.version = 2;
            h.headerSize = sizeof(DLogFileHeader);
            h.channels = 1;
            h.sampleRate = 1000;
            h.sampleFormat = 0;
            h.nativeRate = 1000;
            h.decimationFactor = 1;
            h.fullScaleVoltage = 1.0;
            out.write(reinterpret_cast<const char*>(&h), sizeof(h));
            std::vector<float> data(10000, 0.25f);
            out.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(float));
        }

        PlaybackLoaderWorker loader;
        std::string err;
        assert(loader.OpenFile(testPlayFile, err));

        uint64_t epoch1 = loader.GetCurrentEpoch();
        assert(epoch1 > 0);
        loader.RequestWindow(2.0, 1.0, 500, false);

        loader.InvalidateGeneration();
        uint64_t epoch2 = loader.GetCurrentEpoch();
        assert(epoch2 > epoch1);

        loader.RequestWindow(5.0, 1.0, 500, false);

        PlaybackWindowData result;
        bool gotResult = false;
        for (int retry = 0; retry < 50; ++retry) {
            if (loader.PollResult(result)) {
                gotResult = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        assert(gotResult);
        assert(result.epoch == epoch2);
        assert(std::fabs(result.centerTimeSec - 5.0) < 1e-3);
        loader.CloseFile();

        std::error_code ec;
        std::filesystem::remove(testPlayFile, ec);
        std::cout << "  -> Playback epoch isolation and request invalidation: PASS!" << std::endl;
    }
}

void TestDecimatorAntiAliasingToggle() {
    std::cout << "[Test 21] Testing Decimator Anti-Aliasing Toggle (ON vs OFF)..." << std::endl;

    // 1. Getter and default initialization
    {
        DecimatorProcessor dDefault;
        dDefault.Initialize(2, 2);
        assert(dDefault.IsAntiAliasingEnabled() == true);

        DecimatorProcessor dOff;
        dOff.Initialize(2, 2, false);
        assert(dOff.IsAntiAliasingEnabled() == false);

        DecimatorProcessor dOn;
        dOn.Initialize(2, 2, true);
        assert(dOn.IsAntiAliasingEnabled() == true);
    }

    // 2. Step signal test (factor <= 8: factor = 2)
    // Step signal: 0.0f for first 256 frames, 1.0f for subsequent 256 frames
    {
        const int channels = 2;
        const int factor = 2;
        AudioBlock inBlock;
        inBlock.channels = channels;
        inBlock.frameCount = 512;
        inBlock.sampleRate = 44100.0;
        inBlock.timestampUtcMs = 1000;
        for (size_t f = 0; f < 512; ++f) {
            float v = (f < 256) ? 0.0f : 1.0f;
            inBlock.samples[f * 2 + 0] = v;
            inBlock.samples[f * 2 + 1] = v;
        }

        // Without Anti-Aliasing (pure point sampling)
        DecimatorProcessor dOff;
        dOff.Initialize(channels, factor, false);
        AudioBlock outOff;
        dOff.Process(inBlock, outOff);
        assert(outOff.frameCount == 256);

        for (size_t outIdx = 0; outIdx < outOff.frameCount; ++outIdx) {
            float expected = (outIdx < 128) ? 0.0f : 1.0f;
            assert(outOff.samples[outIdx * 2 + 0] == expected);
            assert(outOff.samples[outIdx * 2 + 1] == expected);
        }

        // With Anti-Aliasing (FIR LPF)
        DecimatorProcessor dOn;
        dOn.Initialize(channels, factor, true);
        AudioBlock outOn;
        dOn.Process(inBlock, outOn);
        assert(outOn.frameCount == 256);

        // FIR filter creates an intermediate transition around outIdx = 128
        bool foundTransition = false;
        for (size_t outIdx = 120; outIdx < 135; ++outIdx) {
            float val = outOn.samples[outIdx * 2 + 0];
            if (val > 0.01f && val < 0.99f) {
                foundTransition = true;
                break;
            }
        }
        assert(foundTransition);
    }

    // 3. Frequency domain / aliasing attenuation test (factor = 2)
    // Input 15kHz signal into 44.1kHz -> 22.05kHz decimation (Nyquist = 11.025kHz)
    {
        const int channels = 1;
        const int factor = 2;
        AudioBlock inBlock;
        inBlock.channels = channels;
        inBlock.frameCount = 512;
        inBlock.sampleRate = 44100.0;
        inBlock.timestampUtcMs = 2000;
        const double fIn = 15000.0;
        for (size_t f = 0; f < 512; ++f) {
            inBlock.samples[f] = static_cast<float>(std::sin(2.0 * 3.141592653589793 * fIn * f / 44100.0));
        }

        DecimatorProcessor dOn;
        dOn.Initialize(channels, factor, true);
        AudioBlock outOn;
        dOn.Process(inBlock, outOn);

        DecimatorProcessor dOff;
        dOff.Initialize(channels, factor, false);
        AudioBlock outOff;
        dOff.Process(inBlock, outOff);

        // Find max amplitude in steady state (skip initial filter delay frames)
        float maxOn = 0.0f;
        for (size_t f = 20; f < outOn.frameCount; ++f) {
            maxOn = std::max(maxOn, std::fabs(outOn.samples[f]));
        }
        float maxOff = 0.0f;
        for (size_t f = 20; f < outOff.frameCount; ++f) {
            maxOff = std::max(maxOff, std::fabs(outOff.samples[f]));
        }

        // Anti-aliased signal should be significantly attenuated (stopband)
        // Non-anti-aliased signal retains full aliased sinusoidal amplitude ~1.0
        assert(maxOn < 0.20f);
        assert(maxOff > 0.85f);
    }

    // 4. Low frequency mode test (factor > 8: factor = 10)
    // Constant 1.0f with alternating high-frequency noise (+0.5f, -0.5f)
    {
        const int channels = 1;
        const int factor = 10;
        AudioBlock inBlock;
        inBlock.channels = channels;
        inBlock.frameCount = 500;
        inBlock.sampleRate = 10000.0;
        inBlock.timestampUtcMs = 3000;
        for (size_t f = 0; f < 500; ++f) {
            float noise = (f % 2 == 0) ? 0.5f : -0.5f;
            inBlock.samples[f] = 1.0f + noise;
        }

        DecimatorProcessor dOn;
        dOn.Initialize(channels, factor, true);
        AudioBlock outOn;
        dOn.Process(inBlock, outOn);

        DecimatorProcessor dOff;
        dOff.Initialize(channels, factor, false);
        AudioBlock outOff;
        dOff.Process(inBlock, outOff);

        assert(outOn.frameCount == 50);
        assert(outOff.frameCount == 50);

        // With Anti-Aliasing (boxcar moving average):
        // Average of 10 samples (five 1.5f and five 0.5f) is exactly 1.0f
        for (size_t f = 0; f < outOn.frameCount; ++f) {
            assert(std::fabs(outOn.samples[f] - 1.0f) < 1e-5f);
        }

        // Without Anti-Aliasing (point sampling):
        // Every 10th sample (at f = 9, 19, 29... odd indices) is 1.0f - 0.5f = 0.5f
        for (size_t f = 0; f < outOff.frameCount; ++f) {
            assert(std::fabs(outOff.samples[f] - 0.5f) < 1e-5f);
        }
    }

    std::cout << "  -> Anti-Aliasing Toggle (FIR & Boxcar vs Point Sampling) PASS!" << std::endl;
}

void TestLocalizationAndLanguagePreferences() {
    std::cout << "[Test 20] Testing Localization Integrity & LanguagePreferences..." << std::endl;

    // 1. 翻訳テーブル完全性検証
    std::string integErr;
    bool integrityOk = VerifyLocalizationIntegrity(&integErr);
    if (!integrityOk) {
        std::cerr << "Localization integrity failure: " << integErr << std::endl;
    }
    assert(integrityOk);
    assert(integErr.empty());

    // 2. 言語コード相互変換
    assert(std::string(LanguageToCode(Language::Japanese)) == "ja");
    assert(std::string(LanguageToCode(Language::English)) == "en");
    assert(LanguageFromCode("ja") == Language::Japanese);
    assert(LanguageFromCode("en") == Language::English);
    assert(LanguageFromCode("unknown", Language::Japanese) == Language::Japanese);
    assert(LanguageFromCode("corrupted", Language::English) == Language::English);

    // 3. 一時設定ファイルを用いた保存・読込・アトミック更新検証
    const std::string testIniPath = "test_settings_pref.ini";
    std::filesystem::remove(testIniPath);
    LanguagePreferences::SetCustomSettingsPathForTesting(testIniPath);

    // デフォルト状態での初期化（CLI指定なし）
    LanguagePreferences::Initialize("");
    assert(!LanguagePreferences::HasCliOverride());

    // 英語へ切り替え・保存
    std::string saveErr;
    bool saved = LanguagePreferences::SetCurrentLanguage(Language::English, true, &saveErr);
    assert(saved);
    assert(saveErr.empty());
    assert(LanguagePreferences::GetCurrentLanguage() == Language::English);
    assert(std::filesystem::exists(testIniPath));

    // ファイル内容検証
    {
        std::ifstream in(testIniPath);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(content.find("language=en") != std::string::npos);
        assert(content.find("schema_version=1") != std::string::npos);
    }

    // 別インスタンス相当で再読み込み
    Language loadedLang = Language::Japanese;
    bool loadOk = LanguagePreferences::LoadFromDisk(loadedLang, testIniPath);
    assert(loadOk);
    assert(loadedLang == Language::English);

    // 日本語へ更新
    saved = LanguagePreferences::SetCurrentLanguage(Language::Japanese, true, &saveErr);
    assert(saved);
    assert(LanguagePreferences::GetCurrentLanguage() == Language::Japanese);
    loadOk = LanguagePreferences::LoadFromDisk(loadedLang, testIniPath);
    assert(loadOk);
    assert(loadedLang == Language::Japanese);

    // 4. CLI言語オーバーライドの検証
    LanguagePreferences::Initialize("en");
    assert(LanguagePreferences::HasCliOverride());
    assert(LanguagePreferences::GetCurrentLanguage() == Language::English);

    // CLI指定時はディスクへの保存がスキップされる
    LanguagePreferences::SetCurrentLanguage(Language::Japanese, true, &saveErr);
    assert(LanguagePreferences::GetCurrentLanguage() == Language::Japanese);
    loadOk = LanguagePreferences::LoadFromDisk(loadedLang, testIniPath);
    assert(loadOk);
    assert(loadedLang == Language::Japanese); // 前回の保存値のまま

    // 5. CFG-001: 非ASCIIパス、INI破損耐性、書込不可時の非破壊性検証
    {
        LanguagePreferences::Initialize(""); // CLIオーバーライド解除
        std::filesystem::path nonAsciiDir = "test_設定_unicode_path";
        std::error_code ec;
        std::filesystem::create_directories(nonAsciiDir, ec);
        std::filesystem::path unicodeIni = nonAsciiDir / "settings_言語.ini";
        std::filesystem::remove(unicodeIni, ec);

        LanguagePreferences::SetCustomSettingsPathForTesting(unicodeIni.u8string());
        std::string err;
        bool ok = LanguagePreferences::SetCurrentLanguage(Language::English, true, &err);
        assert(ok);
        assert(std::filesystem::exists(unicodeIni));

        Language loaded = Language::Japanese;
        assert(LanguagePreferences::LoadFromDisk(loaded, unicodeIni.u8string()));
        assert(loaded == Language::English);

        // 破損INI耐性 (不正セクションや構文エラーがあってもクラッシュしない)
        {
            std::ofstream out(unicodeIni, std::ios::trunc);
            out << "[InvalidSection\ncorrupt_data=@@@\nlanguage=alien_martian\n";
            out.close();
            Language corruptLoaded = Language::English;
            LanguagePreferences::LoadFromDisk(corruptLoaded, unicodeIni.u8string());
        }

        // 書込不可（リードオンリー）時の非破壊アトミック置換検証
        {
            // まず正常な日本語設定を書き込む
            bool saveOk = LanguagePreferences::SaveToDisk(Language::Japanese, &err, unicodeIni.u8string());
            assert(saveOk);
            std::string originalContent;
            {
                std::ifstream in(unicodeIni);
                originalContent.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }
            assert(originalContent.find("language=ja") != std::string::npos);

            // リードオンリー属性を付与
            #ifdef _WIN32
            ::SetFileAttributesW(unicodeIni.wstring().c_str(), FILE_ATTRIBUTE_READONLY);
            #endif

            // 英語での保存を試みる（失敗するはず）
            std::string failErr;
            bool failSave = LanguagePreferences::SaveToDisk(Language::English, &failErr, unicodeIni.u8string());
            assert(!failSave);
            assert(!failErr.empty());

            // 【重要 CFG-001】保存失敗時に既存設定ファイルが削除されていないことを確認
            assert(std::filesystem::exists(unicodeIni));
            {
                std::ifstream in(unicodeIni);
                std::string afterContent((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                assert(afterContent == originalContent); // 旧内容が完全保持されている
            }

            // 属性を復帰して削除
            #ifdef _WIN32
            ::SetFileAttributesW(unicodeIni.wstring().c_str(), FILE_ATTRIBUTE_NORMAL);
            #endif
        }

        LanguagePreferences::SetCustomSettingsPathForTesting("");
        std::filesystem::remove_all(nonAsciiDir, ec);
    }

    // クリーンアップ
    LanguagePreferences::SetCustomSettingsPathForTesting("");
    std::filesystem::remove(testIniPath);

    std::cout << "  -> Localization table integrity and preferences persistence PASS!" << std::endl;
}

void TestPlaybackSessionRaceAlternatingFiles() {
    std::cout << "[Test 22] Testing Playback session isolation with alternating 8ch and 1ch files (PLAY-001)..." << std::endl;

    const std::string file8ch = "test_play_race_8ch.bin";
    const std::string file1ch = "test_play_race_1ch.bin";

    std::error_code ec;
    std::filesystem::remove(file8ch, ec);
    std::filesystem::remove(file1ch, ec);

    // 8ch 合成ファイル作成 (10000 frames @ 1000 Hz = 10s)
    {
        std::ofstream out(file8ch, std::ios::binary | std::ios::trunc);
        DLogFileHeader h;
        std::memcpy(h.magic, "DLOG", 4);
        h.version = 2;
        h.headerSize = sizeof(DLogFileHeader);
        h.channels = 8;
        h.sampleRate = 1000;
        h.sampleFormat = 0;
        h.nativeRate = 1000;
        h.decimationFactor = 1;
        h.fullScaleVoltage = 1.0;
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));

        std::vector<float> frames(8 * 10000);
        for (size_t f = 0; f < 10000; ++f) {
            for (int ch = 0; ch < 8; ++ch) {
                frames[f * 8 + ch] = static_cast<float>(ch + 1) * 0.1f;
            }
        }
        out.write(reinterpret_cast<const char*>(frames.data()), frames.size() * sizeof(float));
    }

    // 1ch 合成ファイル作成 (10000 frames @ 1000 Hz = 10s)
    {
        std::ofstream out(file1ch, std::ios::binary | std::ios::trunc);
        DLogFileHeader h;
        std::memcpy(h.magic, "DLOG", 4);
        h.version = 2;
        h.headerSize = sizeof(DLogFileHeader);
        h.channels = 1;
        h.sampleRate = 1000;
        h.sampleFormat = 0;
        h.nativeRate = 1000;
        h.decimationFactor = 1;
        h.fullScaleVoltage = 1.0;
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));

        std::vector<float> frames(10000, 0.99f);
        out.write(reinterpret_cast<const char*>(frames.data()), frames.size() * sizeof(float));
    }

    PlaybackLoaderWorker loader;
    std::string err;

    // --- Phase A: Rawモード (低密度: 500 frames <= 2 * 1000) ---
    // 読込直前 (stage 0) の同期フックで 1ch への OpenFile を割り込ませる
    {
        assert(loader.OpenFile(file8ch, err));
        uint64_t epoch8 = loader.GetCurrentEpoch();
        assert(epoch8 > 0);

        std::atomic<bool> hooked{false};
        loader.SetTestSyncHook([&](int stage, uint64_t epoch) {
            if (stage == 0 && epoch == epoch8 && !hooked.exchange(true)) {
                std::string err2;
                loader.OpenFile(file1ch, err2);
            }
        });

        // 8ch raw 要求を発行
        loader.RequestWindow(1.0, 0.5, 1000, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 1ch raw 要求を発行
        uint64_t epoch1 = loader.GetCurrentEpoch();
        assert(epoch1 > epoch8);
        loader.RequestWindow(2.0, 0.5, 1000, false);

        PlaybackWindowData result;
        bool gotResult = false;
        for (int retry = 0; retry < 50; ++retry) {
            if (loader.PollResult(result) && result.epoch == epoch1) {
                gotResult = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        assert(gotResult);
        assert(!result.isAggregated);
        assert(result.channels == 1);
        assert(result.rawSamples.size() == 1);
        assert(result.rawSamples[0].size() > 0);
        assert(std::fabs(result.rawSamples[0][0] - 0.99f) < 1e-4f);
    }

    // --- Phase B: Min/Max Envelopeモード (高密度: 5000 frames > 2 * 50) ---
    // チャンクループ内 (stage 1) の同期フックで交互切替を発生させる
    {
        assert(loader.OpenFile(file8ch, err));
        uint64_t epoch8 = loader.GetCurrentEpoch();

        std::atomic<bool> hooked{false};
        loader.SetTestSyncHook([&](int stage, uint64_t epoch) {
            if (stage == 1 && epoch == epoch8 && !hooked.exchange(true)) {
                std::string err2;
                loader.OpenFile(file1ch, err2);
            }
        });

        // 8ch min/max 要求 (5000 frames, 50 columns)
        loader.RequestWindow(3.0, 5.0, 50, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        uint64_t epoch1 = loader.GetCurrentEpoch();
        assert(epoch1 > epoch8);

        // 1ch 読込中に 8ch へ再切替
        std::atomic<bool> hooked1ch{false};
        loader.SetTestSyncHook([&](int stage, uint64_t epoch) {
            if (stage == 1 && epoch == epoch1 && !hooked1ch.exchange(true)) {
                std::string err3;
                loader.OpenFile(file8ch, err3);
            }
        });

        loader.RequestWindow(3.0, 5.0, 50, false);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        uint64_t epoch8_second = loader.GetCurrentEpoch();
        assert(epoch8_second > epoch1);

        loader.SetTestSyncHook(nullptr); // フック解除
        loader.RequestWindow(3.0, 5.0, 50, false);

        PlaybackWindowData result;
        bool gotResult = false;
        for (int retry = 0; retry < 50; ++retry) {
            if (loader.PollResult(result) && result.epoch == epoch8_second) {
                gotResult = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        assert(gotResult);
        assert(result.isAggregated);
        assert(result.channels == 8);
        assert(result.minPerChannel.size() == 8);
        assert(result.maxPerChannel.size() == 8);
        assert(result.numColumns == 50);
    }

    loader.CloseFile();
    std::filesystem::remove(file8ch, ec);
    std::filesystem::remove(file1ch, ec);
    std::cout << "  -> Playback session isolation with alternating 8ch/1ch (raw and min/max): PASS!" << std::endl;
}

void TestCalibrationCalculator() {
    std::cout << "[Test 23] Testing CalibrationCalculator (DSP Signal Metrics & Vfs Calculation)..." << std::endl;

    // 1. 空・無効バッファ
    {
        SignalStats s = CalibrationCalculator::AnalyzeSamples(nullptr, 0);
        assert(s.vpp == 0.0);
        assert(s.peakVal == 0.0);
        assert(s.status == CalibrationSignalStatus::NoSignal);

        float emptyData[1] = { 0.0f };
        SignalStats s2 = CalibrationCalculator::AnalyzeSamples(emptyData, 0);
        assert(s2.status == CalibrationSignalStatus::NoSignal);
    }

    // 2. 標準的な正弦波 (振幅 0.5, Vpp 1.0, 1kHz, 44.1kHzサンプリング)
    {
        std::vector<float> sineWave(1000);
        for (size_t i = 0; i < sineWave.size(); ++i) {
            sineWave[i] = 0.5f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 44100.0f);
        }
        SignalStats s = CalibrationCalculator::AnalyzeSamples(sineWave.data(), sineWave.size());
        assert(std::abs(s.vpp - 1.0) < 0.02);
        assert(std::abs(s.peakVal - 0.5) < 0.02);
        assert(s.status == CalibrationSignalStatus::Good);
    }

    // 3. 微小信号 (振幅 0.015, Vpp = 0.03 -> LowSignal)
    {
        std::vector<float> lowSignal(1000);
        for (size_t i = 0; i < lowSignal.size(); ++i) {
            lowSignal[i] = 0.015f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 44100.0f);
        }
        SignalStats s = CalibrationCalculator::AnalyzeSamples(lowSignal.data(), lowSignal.size());
        assert(std::abs(s.vpp - 0.03) < 0.005);
        assert(s.status == CalibrationSignalStatus::LowSignal);
    }

    // 4. 無信号 / ノイズフロア (振幅 0.002, Vpp = 0.004 -> NoSignal)
    {
        std::vector<float> noSignal(1000);
        for (size_t i = 0; i < noSignal.size(); ++i) {
            noSignal[i] = 0.002f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 44100.0f);
        }
        SignalStats s = CalibrationCalculator::AnalyzeSamples(noSignal.data(), noSignal.size());
        assert(s.status == CalibrationSignalStatus::NoSignal);
    }

    // 5. 過大入力・クリッピング (振幅 1.1, ±0.995 で飽和)
    {
        std::vector<float> clipSignal(1000);
        for (size_t i = 0; i < clipSignal.size(); ++i) {
            float v = 1.1f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 44100.0f);
            clipSignal[i] = std::clamp(v, -0.995f, 0.995f);
        }
        SignalStats s = CalibrationCalculator::AnalyzeSamples(clipSignal.data(), clipSignal.size());
        assert(s.peakVal >= 0.99);
        assert(s.status == CalibrationSignalStatus::Clipping);
    }

    // 6. DCオフセットを伴う交流信号 (DC +0.3, Vpp 0.4: min +0.1, max +0.5)
    {
        std::vector<float> dcSignal(1000);
        for (size_t i = 0; i < dcSignal.size(); ++i) {
            dcSignal[i] = 0.3f + 0.2f * std::sin(2.0f * 3.14159265f * 1000.0f * static_cast<float>(i) / 44100.0f);
        }
        SignalStats s = CalibrationCalculator::AnalyzeSamples(dcSignal.data(), dcSignal.size());
        assert(std::abs(s.vpp - 0.4) < 0.01);
        assert(std::abs(s.peakVal - 0.5) < 0.01);
        assert(s.status == CalibrationSignalStatus::Good);
    }

    // 7. フルスケール電圧 (V_FS) 計算の検証
    {
        double vfs = 0.0;
        // target 1.0 Vpp, measured 0.5 digital -> V_FS = 2.0 V
        assert(CalibrationCalculator::CalculateFullScale(1.0, 0.5, vfs));
        assert(std::abs(vfs - 2.0) < 1e-9);

        // target 2.5 Vpp, measured 1.0 digital -> V_FS = 2.5 V
        assert(CalibrationCalculator::CalculateFullScale(2.5, 1.0, vfs));
        assert(std::abs(vfs - 2.5) < 1e-9);

        // 異常値・境界値保護
        assert(!CalibrationCalculator::CalculateFullScale(0.0, 1.0, vfs));
        assert(!CalibrationCalculator::CalculateFullScale(-1.0, 1.0, vfs));
        assert(!CalibrationCalculator::CalculateFullScale(1.0, 0.0, vfs));
        assert(!CalibrationCalculator::CalculateFullScale(1.0, 0.00005, vfs));
        assert(!CalibrationCalculator::CalculateFullScale(std::numeric_limits<double>::quiet_NaN(), 1.0, vfs));
        assert(!CalibrationCalculator::CalculateFullScale(1.0, std::numeric_limits<double>::infinity(), vfs));
    }

    // 8. 矩形波および直流(DC)信号
    {
        std::vector<float> squareWave(100);
        for (size_t i = 0; i < squareWave.size(); ++i) {
            squareWave[i] = (i % 20 < 10) ? 0.4f : -0.4f;
        }
        SignalStats sSquare = CalibrationCalculator::AnalyzeSamples(squareWave.data(), squareWave.size());
        assert(std::abs(sSquare.vpp - 0.8) < 1e-5);
        assert(sSquare.status == CalibrationSignalStatus::Good);

        std::vector<float> pureDc(100, 0.5f);
        SignalStats sDc = CalibrationCalculator::AnalyzeSamples(pureDc.data(), pureDc.size());
        assert(sDc.vpp == 0.0);
        assert(sDc.status == CalibrationSignalStatus::NoSignal);
    }

    // 9. NaN/Inf 混入時の耐障害性
    {
        std::vector<float> dirtySamples = {
            std::numeric_limits<float>::quiet_NaN(),
            -0.3f,
            std::numeric_limits<float>::infinity(),
            0.4f,
            -std::numeric_limits<float>::infinity()
        };
        SignalStats sDirty = CalibrationCalculator::AnalyzeSamples(dirtySamples.data(), dirtySamples.size());
        assert(std::abs(sDirty.vpp - 0.7) < 1e-5);
        assert(std::abs(sDirty.peakVal - 0.4) < 1e-5);
        assert(sDirty.status == CalibrationSignalStatus::Good);
    }

    // 10. 校正成功メッセージ書式の検証
    {
        std::string msgJa = FormatString(StringId::CalibrateSuccessWithChFormat, {"1", "0.5000", "1.000", "2.0000"}, Language::Japanese);
        assert(msgJa.find("チャネル 1") != std::string::npos);
        assert(msgJa.find("±2.0000 V") != std::string::npos);

        std::string msgEn = FormatString(StringId::CalibrateSuccessWithChFormat, {"2", "0.5000", "1.000", "2.0000"}, Language::English);
        assert(msgEn.find("Channel 2") != std::string::npos);
        assert(msgEn.find("±2.0000 V") != std::string::npos);
    }

    std::cout << "  -> CalibrationCalculator DSP metrics, signal health, and V_FS calculation: PASS!" << std::endl;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--hardware-only") {
        TestPreviewModeAndTransitions();
        return 0;
    }

    // TEST-001: 実環境の設定ファイルを記録し、テスト実行中の保存先を完全隔離
    std::filesystem::path realSettingsPath(LanguagePreferences::GetSettingsFilePath());
    bool realExisted = std::filesystem::exists(realSettingsPath);
    std::vector<uint8_t> realSettingsContent;
    if (realExisted) {
        std::ifstream in(realSettingsPath, std::ios::binary);
        realSettingsContent.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    std::filesystem::path isolatedSettings = std::filesystem::temp_directory_path() / "dlog_test_isolated_settings.ini";
    LanguagePreferences::SetCustomSettingsPathForTesting(isolatedSettings.u8string());

    std::cout << "=== Running Data-Logger Unit & System Tests ===" << std::endl;
    TestHeaderSpecification();
    TestDecimatorProcessor();
    TestDataLoggerWorker();
    TestAudioDeviceManagerEnumeration();
    TestDataLoggerJapanesePathAndError();
    TestAudioDeviceManagerErrorTracking();
    TestDecimation1HzAndVoltageHeader();
    TestChannelMetadataConfig();
    TestDecimation48kHzAndTargetRates();
    TestTransactionalOutputAndValidation();
    TestPortAudioOverflowStatusCounting();
    TestInvalidAudioBlockRejection();
    TestCandidateRatesAndFallbackSelection();
    TestDecimation32kHzTo1000HzAndHeader();
    TestCheckInputFormatSupportAPI();
    TestTimeAxisCalculations();
    // Real audio capture is opt-in via the separate TestAudioHardware CTest entry.
    TestPlaybackCoreAndClock();
    TestPartialFileRecoveryAndParserValidation();
    TestPlaybackEpochIsolationAndDurationRetention();
    TestDecimatorAntiAliasingToggle();
    TestLocalizationAndLanguagePreferences();
    TestPlaybackSessionRaceAlternatingFiles();
    TestCalibrationCalculator();

    // TEST-001: テスト完了後に実環境の設定ファイルが 1 バイトも変化していないことを厳格検証
    LanguagePreferences::SetCustomSettingsPathForTesting("");
    std::error_code ec;
    std::filesystem::remove(isolatedSettings, ec);

    bool realExistedAfter = std::filesystem::exists(realSettingsPath);
    assert(realExisted == realExistedAfter);
    if (realExisted) {
        std::ifstream in(realSettingsPath, std::ios::binary);
        std::vector<uint8_t> realSettingsContentAfter((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(realSettingsContent == realSettingsContentAfter);
    }
    std::cout << "  -> [TEST-001] Real user settings isolation: 100% untouched verified!" << std::endl;

    std::cout << "\n>>> ALL TESTS PASSED! <<<" << std::endl;
    return 0;
}

