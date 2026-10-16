#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <cstring>
#include <fstream>
#include <vector>
#include <cmath>
#include <limits>
#include <filesystem>
#include <thread>
#include <chrono>

#include "export/wav_export_types.h"
#include "export/wav_export_plan.h"
#include "export/interpolator.h"
#include "export/wav_writer.h"
#include "export/wav_export_worker.h"
#include "playback/playback_time_parser.h"
#include "dlog_reader.h"
#include "common/localized_strings.h"
#include "common/language_preferences.h"

using namespace dlog;

static void SafeRemoveFile(const std::filesystem::path& p) {
    std::error_code ec;
    for (int retry = 0; retry < 25; ++retry) {
        if (!std::filesystem::exists(p, ec)) return;
        if (std::filesystem::remove(p, ec)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// テスト用DLOGファイル生成
static void CreateTestDlog(const std::string& path, uint32_t channels, uint32_t nativeRate,
                           uint32_t decimationFactor, uint64_t frames, double fullScaleVoltage = 1.0) {
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    assert(out.is_open());

    DLogFileHeader h;
    std::memcpy(h.magic, "DLOG", 4);
    h.version = DLOG_FORMAT_VERSION;
    h.headerSize = sizeof(DLogFileHeader);
    h.channels = channels;
    h.nativeRate = nativeRate;
    h.decimationFactor = decimationFactor;
    h.sampleRate = static_cast<uint32_t>(std::round(static_cast<double>(nativeRate) / static_cast<double>(decimationFactor)));
    h.sampleFormat = 0; // Float32
    h.startTimestampUtc = 1791510000000;
    h.fullScaleVoltage = fullScaleVoltage;

    out.write(reinterpret_cast<const char*>(&h), sizeof(h));

    // 各チャネルごとに既知の正弦波を書き込む
    double effectiveRate = static_cast<double>(nativeRate) / static_cast<double>(decimationFactor);
    std::vector<float> frameBuf(channels);

    for (uint64_t f = 0; f < frames; ++f) {
        double t = static_cast<double>(f) / effectiveRate;
        for (uint32_t ch = 0; ch < channels; ++ch) {
            double freq = 10.0 * (ch + 1); // 10Hz, 20Hz, ...
            frameBuf[ch] = static_cast<float>(0.5 * std::sin(2.0 * 3.141592653589793 * freq * t));
        }
        out.write(reinterpret_cast<const char*>(frameBuf.data()), channels * sizeof(float));
    }
}

// 独立した最小限の WAV / RF64 パーサー (第三者検証用)
struct ParsedWav {
    bool isRf64 = false;
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    uint16_t bitsPerSample = 0;
    uint16_t formatTag = 0;
    uint32_t channelMask = 0;
    uint64_t dataBytes = 0;
    uint64_t sampleCount = 0;
    std::vector<float> floatSamples;
    std::vector<int16_t> pcm16Samples;
};

static bool ParseWavFile(const std::string& path, ParsedWav& outWav, std::string& outError) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in.is_open()) {
        outError = "Failed to open WAV file: " + path;
        return false;
    }

    char chunkId[4];
    uint32_t chunkSize = 0;

    in.read(chunkId, 4);
    if (std::memcmp(chunkId, "RIFF", 4) == 0) {
        outWav.isRf64 = false;
    } else if (std::memcmp(chunkId, "RF64", 4) == 0) {
        outWav.isRf64 = true;
    } else {
        outError = "Invalid header chunk ID (expected RIFF or RF64).";
        return false;
    }

    uint32_t riffSize32 = 0;
    in.read(reinterpret_cast<char*>(&riffSize32), 4);

    char waveId[4];
    in.read(waveId, 4);
    if (std::memcmp(waveId, "WAVE", 4) != 0) {
        outError = "Invalid format (expected WAVE).";
        return false;
    }

    bool hasFmt = false;
    bool hasData = false;
    uint64_t ds64DataSize = 0;
    uint64_t ds64SampleCount = 0;

    while (in.read(chunkId, 4)) {
        in.read(reinterpret_cast<char*>(&chunkSize), 4);
        if (!in.good()) break;

        if (std::memcmp(chunkId, "ds64", 4) == 0) {
            uint64_t rSize64 = 0;
            in.read(reinterpret_cast<char*>(&rSize64), 8);
            in.read(reinterpret_cast<char*>(&ds64DataSize), 8);
            in.read(reinterpret_cast<char*>(&ds64SampleCount), 8);
            uint32_t tableLen = 0;
            in.read(reinterpret_cast<char*>(&tableLen), 4);
            outWav.dataBytes = ds64DataSize;
            outWav.sampleCount = ds64SampleCount;
            // 残りのテーブルをスキップ
            if (chunkSize > 28) in.seekg(chunkSize - 28, std::ios::cur);
        } else if (std::memcmp(chunkId, "fmt ", 4) == 0) {
            hasFmt = true;
            uint16_t tag = 0;
            in.read(reinterpret_cast<char*>(&tag), 2);
            outWav.formatTag = tag;
            in.read(reinterpret_cast<char*>(&outWav.channels), 2);
            in.read(reinterpret_cast<char*>(&outWav.sampleRate), 4);
            uint32_t avgBytes = 0;
            in.read(reinterpret_cast<char*>(&avgBytes), 4);
            uint16_t blockAlign = 0;
            in.read(reinterpret_cast<char*>(&blockAlign), 2);
            in.read(reinterpret_cast<char*>(&outWav.bitsPerSample), 2);

            if (tag == 0xFFFE && chunkSize >= 40) {
                // WAVEFORMATEXTENSIBLE
                uint16_t cbSize = 0;
                in.read(reinterpret_cast<char*>(&cbSize), 2);
                uint16_t validBits = 0;
                in.read(reinterpret_cast<char*>(&validBits), 2);
                in.read(reinterpret_cast<char*>(&outWav.channelMask), 4);
                uint8_t sub[16];
                in.read(reinterpret_cast<char*>(sub), 16);
                if (sub[0] == 0x03) {
                    outWav.formatTag = 0x0003; // IEEE Float
                } else if (sub[0] == 0x01) {
                    outWav.formatTag = 0x0001; // PCM
                }
                if (chunkSize > 40) in.seekg(chunkSize - 40, std::ios::cur);
            } else if (chunkSize > 16) {
                in.seekg(chunkSize - 16, std::ios::cur);
            }
        } else if (std::memcmp(chunkId, "fact", 4) == 0) {
            uint32_t factSamples = 0;
            in.read(reinterpret_cast<char*>(&factSamples), 4);
            if (!outWav.isRf64) {
                outWav.sampleCount = factSamples;
            }
            if (chunkSize > 4) in.seekg(chunkSize - 4, std::ios::cur);
        } else if (std::memcmp(chunkId, "data", 4) == 0) {
            hasData = true;
            uint64_t actualDataSize = (outWav.isRf64 && ds64DataSize > 0) ? ds64DataSize : chunkSize;
            outWav.dataBytes = actualDataSize;

            if (outWav.formatTag == 0x0003 && outWav.bitsPerSample == 32) {
                size_t numFloats = actualDataSize / sizeof(float);
                outWav.floatSamples.resize(numFloats);
                in.read(reinterpret_cast<char*>(outWav.floatSamples.data()), actualDataSize);
            } else if (outWav.formatTag == 0x0001 && outWav.bitsPerSample == 16) {
                size_t numShorts = actualDataSize / sizeof(int16_t);
                outWav.pcm16Samples.resize(numShorts);
                in.read(reinterpret_cast<char*>(outWav.pcm16Samples.data()), actualDataSize);
            } else {
                in.seekg(actualDataSize, std::ios::cur);
            }

            if (actualDataSize % 2 != 0) {
                in.seekg(1, std::ios::cur); // padding byte
            }
            break; // data チャンク読込完了
        } else {
            // 未知チャンクのスキップ
            in.seekg(chunkSize, std::ios::cur);
            if (chunkSize % 2 != 0) in.seekg(1, std::ios::cur);
        }
    }

    if (!hasFmt || !hasData) {
        outError = "Missing fmt or data chunk.";
        return false;
    }
    return true;
}

// -------------------------------------------------------------
// Test 1: WavExportPlan の仕様テスト
// -------------------------------------------------------------
void TestWavExportPlanSpecification() {
    std::cout << "[Test 1] Testing WavExportPlan specifications (rates, frames, sizes, 4GiB limits)..." << std::endl;

    DLogFileHeader h;
    h.nativeRate = 44100;
    h.decimationFactor = 44; // 44100 / 44 = 1002.2727... Hz (非整数レート)
    h.channels = 4;
    h.sampleFormat = 0;
    uint64_t totalFrames = 1000;

    WavExportOptions opt;
    opt.sourceDlogPath = "test.bin";
    opt.destinationWavPath = "out.wav";
    opt.exportEntireFile = true;
    opt.sampleFormat = WavSampleFormat::IeeeFloat32;
    opt.interpolationMethod = InterpolationMethod::Linear;
    opt.limitTo4GiB = true;

    WavExportEstimate est = WavExportPlan::AnalyzePlan(h, totalFrames, opt);
    assert(est.isValid);
    assert(est.nativeRate == 44100);
    assert(est.decimationFactor == 44);
    assert(std::abs(est.effectiveInputRate - (44100.0 / 44.0)) < 1e-6);
    assert(est.outputRate == 44100);
    assert(est.interpolationFactor == 44);
    assert(est.inputFrames == 1000);
    assert(est.outputFrames == 44000);
    assert(std::abs(est.outputDurationSec - (44000.0 / 44100.0)) < 1e-6);
    assert(est.channelCount == 4);
    assert(est.containerFormat == WavContainerFormat::StandardWav);
    assert(!est.exceeds4GiBLimit);

    // 時間範囲の変換テスト
    uint64_t sFrame = 0, eFrame = 0;
    std::string err;
    assert(WavExportPlan::CalculateFrameRange(0.0, 0.5, 1000, est.effectiveInputRate, sFrame, eFrame, err));
    assert(sFrame == 0);
    assert(eFrame > 0 && eFrame <= 1000);

    // 範囲境界エラー拒否
    assert(!WavExportPlan::CalculateFrameRange(1.0, 0.5, 1000, est.effectiveInputRate, sFrame, eFrame, err));
    assert(!WavExportPlan::CalculateFrameRange(-0.1, 0.5, 1000, est.effectiveInputRate, sFrame, eFrame, err));
    assert(!WavExportPlan::CalculateFrameRange(1.0, 1.0, 1000, est.effectiveInputRate, sFrame, eFrame, err));

    // マイクロ秒丸め文字列 (2000 / 22050 = 0.0907029478s -> "00:00:00.090703") の境界許容テスト
    {
        double dur22k = 2000.0 / 22050.0;
        std::string formattedEnd = FormatPlaybackTime(dur22k, true);
        double parsedEnd = 0.0;
        assert(ParsePlaybackTimeRaw(formattedEnd, dur22k, parsedEnd, err, 1e-6));
        assert(WavExportPlan::CalculateFrameRange(0.0, parsedEnd, 2000, 22050.0, sFrame, eFrame, err));
        assert(sFrame == 0 && eFrame == 2000);
    }

    // 明示的チャネル未選択 (explicitChannelSelection = true かつ空) のエラー拒否
    {
        WavExportOptions noChOpt = opt;
        noChOpt.explicitChannelSelection = true;
        noChOpt.selectedChannels.clear();
        WavExportEstimate noChEst = WavExportPlan::AnalyzePlan(h, totalFrames, noChOpt);
        assert(!noChEst.isValid);
        assert(noChEst.errorInfo.code == StringId::ErrWavNoChannelsSelected);
    }

    // パス検証テスト
    assert(WavExportPlan::ValidatePaths("test.bin", "output.wav", err));
    assert(!WavExportPlan::ValidatePaths("test.bin", "test.bin", err)); // 自己上書き拒否
    assert(!WavExportPlan::ValidatePaths("test.bin", "test.bin.meta", err)); // メタデータ上書き拒否
    assert(!WavExportPlan::ValidatePaths("test.bin", "test.txt", err)); // 拡張子.wav必須

    // 4 GiB 制限テスト: 巨大フレーム数のシミュレーション
    WavExportOptions hugeOpt = opt;
    uint64_t hugeFrames = 30000000; // 3000万フレーム * 44 = 13.2億フレーム * 4ch * 4B = 21 GB
    WavExportEstimate hugeEst = WavExportPlan::AnalyzePlan(h, hugeFrames, hugeOpt);
    assert(hugeEst.exceeds4GiBLimit);
    assert(!hugeEst.isValid); // 4GiB制限ON時は拒否されること

    hugeOpt.limitTo4GiB = false;
    WavExportEstimate rf64Est = WavExportPlan::AnalyzePlan(h, hugeFrames, hugeOpt);
    assert(rf64Est.isValid);
    assert(rf64Est.containerFormat == WavContainerFormat::Rf64); // 4GiB制限OFF時はRF64が選択されること

    std::cout << "  -> WavExportPlan specifications: PASS!" << std::endl;
}

// -------------------------------------------------------------
// Test 2: Interpolator の補間精度と挙動テスト
// -------------------------------------------------------------
void TestWavInterpolator() {
    std::cout << "[Test 2] Testing Interpolator (Factor 1, ZOH, Linear, Polyphase FIR)..." << std::endl;

    // 1. Factor 1: 直接コピー
    {
        Interpolator interp;
        interp.Initialize(2, 1, InterpolationMethod::Linear);
        assert(interp.GetPreMargin() == 0 && interp.GetPostMargin() == 0);

        std::vector<float> in = { 0.1f, -0.2f, 0.3f, -0.4f };
        std::vector<float> out;
        bool ok = interp.ProcessFrames(in.data(), 2, [&](const float* c, size_t f) {
            out.insert(out.end(), c, c + f * 2);
            return true;
        });
        assert(ok);
        assert(out == in);
    }

    // 2. ゼロ次ホールド (ZOH): factor = 4
    {
        Interpolator interp;
        interp.Initialize(1, 4, InterpolationMethod::ZeroOrderHold);
        std::vector<float> in = { 1.5f, 2.5f };
        std::vector<float> out;
        interp.ProcessFrames(in.data(), 2, [&](const float* c, size_t f) {
            out.insert(out.end(), c, c + f);
            return true;
        });
        assert(out.size() == 8);
        for (int i = 0; i < 4; ++i) assert(out[i] == 1.5f);
        for (int i = 4; i < 8; ++i) assert(out[i] == 2.5f);
    }

    // 3. 線形補間: factor = 4
    {
        Interpolator interp;
        interp.Initialize(1, 4, InterpolationMethod::Linear);
        assert(interp.GetPostMargin() == 1);

        // x[0] = 0.0, x[1] = 1.0, postMargin x[2] = 2.0
        std::vector<float> in = { 0.0f, 1.0f, 2.0f };
        std::vector<float> out;
        interp.ProcessFrames(in.data(), 2, [&](const float* c, size_t f) {
            out.insert(out.end(), c, c + f);
            return true;
        });
        assert(out.size() == 8);
        assert(std::abs(out[0] - 0.0f) < 1e-6);
        assert(std::abs(out[1] - 0.25f) < 1e-6);
        assert(std::abs(out[2] - 0.50f) < 1e-6);
        assert(std::abs(out[3] - 0.75f) < 1e-6);
        assert(std::abs(out[4] - 1.0f) < 1e-6);
        assert(std::abs(out[5] - 1.25f) < 1e-6);
        assert(std::abs(out[6] - 1.50f) < 1e-6);
        assert(std::abs(out[7] - 1.75f) < 1e-6);
    }

    // 4. 帯域制限補間 (FIR): DCゲイン、原点サンプル一致、チャンク非依存性
    {
        Interpolator interp;
        interp.Initialize(1, 4, InterpolationMethod::BandlimitedFir);
        int pre = interp.GetPreMargin();
        int post = interp.GetPostMargin();
        assert(pre == 3 && post == 4);

        // 4.1 DCゲインテスト (定数 0.75f を入力したとき全出力サンプルが 0.75f になること)
        std::vector<float> inDc(10 + pre + post, 0.75f);
        std::vector<float> outDc;
        interp.ProcessFrames(inDc.data(), 10, [&](const float* c, size_t f) {
            outDc.insert(outDc.end(), c, c + f);
            return true;
        });
        assert(outDc.size() == 40);
        for (float val : outDc) {
            assert(std::abs(val - 0.75f) < 1e-5);
        }

        // 4.2 Sincのゼロ交差特性: s = 0 のサンプルは元の入力値と完全に一致すること
        std::vector<float> inSeq = { 0.1f, -0.3f, 0.8f, -0.5f, 0.2f, 0.7f, -0.4f, 0.6f, -0.1f, 0.9f };
        std::vector<float> inWithPad(inSeq.size() + pre + post, 0.0f);
        // パディング (端点クランプ)
        for (int i = 0; i < pre; ++i) inWithPad[i] = inSeq.front();
        for (size_t i = 0; i < inSeq.size(); ++i) inWithPad[pre + i] = inSeq[i];
        for (int i = 0; i < post; ++i) inWithPad[pre + inSeq.size() + i] = inSeq.back();

        std::vector<float> outFir;
        interp.ProcessFrames(inWithPad.data(), inSeq.size(), [&](const float* c, size_t f) {
            outFir.insert(outFir.end(), c, c + f);
            return true;
        });
        assert(outFir.size() == inSeq.size() * 4);
        for (size_t i = 0; i < inSeq.size(); ++i) {
            assert(std::abs(outFir[i * 4] - inSeq[i]) < 1e-5);
        }

        // 4.3 チャンク分割非依存性: チャンク長 2 で小分けに処理しても一括処理と完全一致すること
        std::vector<float> outChunked;
        for (size_t k = 0; k < inSeq.size(); k += 2) {
            size_t cLen = std::min<size_t>(2, inSeq.size() - k);
            const float* subPtr = inWithPad.data() + k;
            interp.ProcessFrames(subPtr, cLen, [&](const float* c, size_t f) {
                outChunked.insert(outChunked.end(), c, c + f);
                return true;
            });
        }
        assert(outChunked.size() == outFir.size());
        for (size_t i = 0; i < outFir.size(); ++i) {
            assert(std::abs(outChunked[i] - outFir[i]) < 1e-6);
        }
    }

    std::cout << "  -> Interpolator specifications: PASS!" << std::endl;
}

// -------------------------------------------------------------
// Test 3: WavWriter と RIFF/RF64 独立検証テスト
// -------------------------------------------------------------
void TestWavWriterAndFormat() {
    std::cout << "[Test 3] Testing WavWriter (Standard WAV, RF64, PCM16 clipping, NaN rejection)..." << std::endl;

    std::string testStdWav = "test_std.wav";
    std::string testMultiWav = "test_multi.wav";
    std::string testRf64 = "test_rf64.wav";
    std::string testPcmWav = "test_pcm16.wav";

    SafeRemoveFile(testStdWav);
    SafeRemoveFile(testMultiWav);
    SafeRemoveFile(testRf64);
    SafeRemoveFile(testPcmWav);

    std::string err;

    // 1. 標準 2ch Float32 WAV
    {
        WavWriter writer;
        assert(writer.Open(testStdWav, WavContainerFormat::StandardWav, 2, 44100, WavSampleFormat::IeeeFloat32, err));
        std::vector<float> samples = { 0.0f, 0.5f, -0.5f, 1.0f }; // 2 frames
        assert(writer.WriteFrames(samples.data(), 2, err));
        assert(writer.Commit(err));

        ParsedWav parsed;
        assert(ParseWavFile(testStdWav, parsed, err));
        assert(!parsed.isRf64);
        assert(parsed.channels == 2);
        assert(parsed.sampleRate == 44100);
        assert(parsed.formatTag == 0x0003); // Float
        assert(parsed.bitsPerSample == 32);
        assert(parsed.sampleCount == 2);
        assert(parsed.floatSamples.size() == 4);
        assert(parsed.floatSamples[1] == 0.5f);
    }

    // 2. 8ch WAVEFORMATEXTENSIBLE Float32 WAV (dwChannelMask == 0 検証)
    {
        WavWriter writer;
        assert(writer.Open(testMultiWav, WavContainerFormat::StandardWav, 8, 48000, WavSampleFormat::IeeeFloat32, err));
        std::vector<float> samples(8 * 10, 0.123f);
        assert(writer.WriteFrames(samples.data(), 10, err));
        assert(writer.Commit(err));

        ParsedWav parsed;
        assert(ParseWavFile(testMultiWav, parsed, err));
        assert(parsed.channels == 8);
        assert(parsed.sampleRate == 48000);
        assert(parsed.channelMask == 0); // 計測チャネル用の未割当マスク (0)
        assert(parsed.sampleCount == 10);
        assert(parsed.floatSamples.size() == 80);
    }

    // 3. RF64 コンテナ形式 (ds64 チャンク検証)
    {
        WavWriter writer;
        assert(writer.Open(testRf64, WavContainerFormat::Rf64, 1, 44100, WavSampleFormat::IeeeFloat32, err));
        std::vector<float> samples(50, -0.75f);
        assert(writer.WriteFrames(samples.data(), 50, err));
        assert(writer.Commit(err));

        ParsedWav parsed;
        assert(ParseWavFile(testRf64, parsed, err));
        assert(parsed.isRf64);
        assert(parsed.sampleCount == 50);
        assert(parsed.dataBytes == 50 * sizeof(float));
        assert(parsed.floatSamples.size() == 50);
        assert(parsed.floatSamples[0] == -0.75f);
    }

    // 4. PCM16 変換、クリップカウント、NaN/Inf 拒否検証
    {
        WavWriter writer;
        assert(writer.Open(testPcmWav, WavContainerFormat::StandardWav, 1, 44100, WavSampleFormat::Pcm16, err));

        // -1.0 -> -32768, 0.0 -> 0, +1.0 -> 32767, クリップサンプル 1.2f, -1.5f
        std::vector<float> samples = { -1.0f, 0.0f, 1.0f, 1.2f, -1.5f };
        assert(writer.WriteFrames(samples.data(), 5, err));
        assert(writer.GetPcm16ClippedSamples() == 2); // 1.2 と -1.5 で 2回クリップ

        // NaN 検知によるエラー終了テスト
        std::vector<float> nanSample = { std::numeric_limits<float>::quiet_NaN() };
        assert(!writer.WriteFrames(nanSample.data(), 1, err));
        assert(err.find("NaN") != std::string::npos);

        writer.Abort();
    }

    SafeRemoveFile(testStdWav);
    SafeRemoveFile(testMultiWav);
    SafeRemoveFile(testRf64);
    SafeRemoveFile(testPcmWav);

    std::cout << "  -> WavWriter and format specifications: PASS!" << std::endl;
}

// -------------------------------------------------------------
// Test 4: WavExportWorker 統合テスト (フルファイル・範囲指定・キャンセル・原子性)
// -------------------------------------------------------------
void TestWavExportWorkerAndIntegration() {
    std::cout << "[Test 4] Testing WavExportWorker integration (End-to-End, Slicing, Cancellation, Overwrite)..." << std::endl;

    std::string testDlogPath = "test_export_in.bin";
    std::string testWavPath = "test_export_out.wav";
    std::string testRangeWavPath = "test_export_range.wav";

    SafeRemoveFile(testDlogPath);
    SafeRemoveFile(testWavPath);
    SafeRemoveFile(testRangeWavPath);

    // 2ch, 44100Hz native, 2x decimation (実効 22050Hz), 2000フレーム
    CreateTestDlog(testDlogPath, 2, 44100, 2, 2000);

    // 1. 全体エクスポート
    {
        WavExportOptions opt;
        opt.sourceDlogPath = testDlogPath;
        opt.destinationWavPath = testWavPath;
        opt.exportEntireFile = true;
        opt.sampleFormat = WavSampleFormat::IeeeFloat32;
        opt.interpolationMethod = InterpolationMethod::Linear;
        opt.limitTo4GiB = true;

        WavExportWorker worker;
        assert(worker.Start(opt));
        worker.Wait();

        WavExportResult res = worker.GetResult();
        assert(res.success);
        assert(!res.canceled);
        assert(res.outputFramesWritten == 4000); // 2000 * 2
        assert(res.totalBytesWritten == std::filesystem::file_size(testWavPath));

        ParsedWav parsed;
        std::string err;
        assert(ParseWavFile(testWavPath, parsed, err));
        assert(parsed.sampleRate == 44100);
        assert(parsed.channels == 2);
        assert(parsed.sampleCount == 4000);
    }

    // 2. 範囲指定エクスポートと全体エクスポートの切り出し整合性検証
    {
        // フレーム [500, 1500) (1000フレーム -> 出力 2000フレーム)
        WavExportOptions optRange;
        optRange.sourceDlogPath = testDlogPath;
        optRange.destinationWavPath = testRangeWavPath;
        optRange.exportEntireFile = false;
        optRange.startFrame = 500;
        optRange.endFrame = 1500;
        optRange.sampleFormat = WavSampleFormat::IeeeFloat32;
        optRange.interpolationMethod = InterpolationMethod::Linear;
        optRange.limitTo4GiB = true;

        WavExportWorker workerRange;
        assert(workerRange.Start(optRange));
        workerRange.Wait();

        WavExportResult resRange = workerRange.GetResult();
        assert(resRange.success);
        assert(resRange.outputFramesWritten == 2000);

        ParsedWav parsedFull, parsedRange;
        std::string err;
        assert(ParseWavFile(testWavPath, parsedFull, err));
        assert(ParseWavFile(testRangeWavPath, parsedRange, err));

        // 範囲指定出力は、全体出力の該当区間 [1000, 3000) と完全一致すること
        // (各フレーム 2ch なので offset 1000 * 2 = 2000, count 2000 * 2 = 4000)
        size_t fullOffset = 500 * 2 * 2; // startFrame(500) * factor(2) * channels(2)
        for (size_t i = 0; i < parsedRange.floatSamples.size(); ++i) {
            float vRange = parsedRange.floatSamples[i];
            float vFull = parsedFull.floatSamples[fullOffset + i];
            assert(std::abs(vRange - vFull) < 1e-6);
        }
    }

    // 3. キャンセル処理と既存ファイルの非破壊保持検証
    {
        // 既存の testWavPath の内容を記憶
        uint64_t originalSize = std::filesystem::file_size(testWavPath);

        WavExportOptions optCancel;
        optCancel.sourceDlogPath = testDlogPath;
        optCancel.destinationWavPath = testWavPath; // 既存ファイルを指定
        optCancel.exportEntireFile = true;

        WavExportWorker workerCancel;
        assert(workerCancel.Start(optCancel));
        workerCancel.RequestCancel(); // 即座にキャンセル
        workerCancel.Wait();

        WavExportResult resCancel = workerCancel.GetResult();
        assert(resCancel.canceled);
        assert(!resCancel.success);

        // 元の既存ファイルが破壊されずにそのまま保持されていること
        assert(std::filesystem::exists(testWavPath));
        assert(std::filesystem::file_size(testWavPath) == originalSize);
    }

    // 4. 日本語および空白文字を含むパスでの動作検証
    {
        std::string jpWavPath = "test_日本語_録音 export.wav";
        SafeRemoveFile(jpWavPath);

        WavExportOptions optJp;
        optJp.sourceDlogPath = testDlogPath;
        optJp.destinationWavPath = jpWavPath;
        optJp.exportEntireFile = true;

        WavExportWorker workerJp;
        assert(workerJp.Start(optJp));
        workerJp.Wait();

        assert(workerJp.GetResult().success);
        assert(std::filesystem::exists(std::filesystem::u8path(jpWavPath)));
        SafeRemoveFile(jpWavPath);
    }

    SafeRemoveFile(testDlogPath);
    SafeRemoveFile(testWavPath);
    SafeRemoveFile(testRangeWavPath);

    std::cout << "  -> WavExportWorker integration: PASS!" << std::endl;
}

int main() {
    std::cout << "=== Running WAV Export Unit & Integration Tests ===" << std::endl;
    TestWavExportPlanSpecification();
    TestWavInterpolator();
    TestWavWriterAndFormat();
    TestWavExportWorkerAndIntegration();
    std::cout << "\n>>> ALL WAV EXPORT TESTS PASSED! <<<" << std::endl;
    return 0;
}
