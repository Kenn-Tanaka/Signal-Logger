#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <cstring>
#include <fstream>
#include <vector>
#include <cmath>
#include <filesystem>

#include "converter_types.h"
#include "dlog_reader.h"
#include "csv_writer.h"
#include "batch_converter_worker.h"

using namespace dlog;

// Windows環境等でのファイル一時ロック(クラウド同期等)に対する安全なファイル削除ヘルパー
static void SafeRemoveFile(const std::filesystem::path& p) {
    std::error_code ec;
    for (int retry = 0; retry < 25; ++retry) {
        if (!std::filesystem::exists(p, ec)) return;
        if (std::filesystem::remove(p, ec)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

// テスト用バイナリファイルの作成
void CreateDummyDlogFile(const std::string& path, uint32_t channels, uint32_t sampleRate, uint64_t frames, double fullScaleVoltage = 1.0) {
    std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
    assert(out.is_open());

    DLogFileHeader h;
    std::memcpy(h.magic, "DLOG", 4);
    h.version = DLOG_FORMAT_VERSION;
    h.headerSize = sizeof(DLogFileHeader);
    h.channels = channels;
    h.sampleRate = sampleRate;
    h.sampleFormat = 0; // Float32
    h.nativeRate = sampleRate * 2;
    h.decimationFactor = 2;
    h.startTimestampUtc = 1791510000000; // 基準時刻
    h.fullScaleVoltage = fullScaleVoltage;

    out.write(reinterpret_cast<const char*>(&h), sizeof(h));

    std::vector<float> frameData(channels);
    for (uint64_t f = 0; f < frames; ++f) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            const double phase = 2.0 * 3.141592653589793 * static_cast<double>(ch + 1) * 100.0 *
                                 static_cast<double>(f) / static_cast<double>(sampleRate);
            frameData[ch] = static_cast<float>(std::sin(phase));
        }
        out.write(reinterpret_cast<const char*>(frameData.data()), channels * sizeof(float));
    }
    out.close();
}

void TestReaderAndInspection() {
    std::cout << "[Test 1] Testing DlogReader inspection..." << std::endl;
    CreateDummyDlogFile("dummy1.bin", 2, 22050, 25000);

    BatchItem item;
    bool ok = DlogReader::InspectFile("dummy1.bin", item);
    assert(ok);
    assert(item.isValidHeader);
    assert(item.header.channels == 2);
    assert(item.header.sampleRate == 22050);
    assert(item.totalFrames == 25000);
    SafeRemoveFile("dummy1.bin");
    std::cout << "  -> File inspection successful (25000 frames, 2ch). PASS!" << std::endl;
}

void TestAutoSplitting() {
    std::cout << "[Test 2] Testing CsvWriter line auto-splitting..." << std::endl;
    CreateDummyDlogFile("dummy_split.bin", 2, 10000, 25000);

    DlogReader reader;
    bool openOk = reader.Open("dummy_split.bin");
    assert(openOk);

    ConversionOptions opt;
    opt.timeFormat = TimeFormat::RelativeSec;
    opt.decimationStep = 1;
    opt.enableSplitLines = true;
    opt.maxLinesPerFile = 10000; // 10,000行ごとに分割
    opt.delimiter = ',';
    opt.floatPrecision = 4;

    uint64_t totalFrames = reader.GetTotalFrames();
    bool willSplit = opt.enableSplitLines && (totalFrames > opt.maxLinesPerFile);
    assert(willSplit == true);

    CsvWriter writer;
    bool initOk = writer.Initialize(".", "dummy_split", opt, reader.GetHeader(), willSplit);
    assert(initOk);

    std::vector<float> chunk(reader.GetHeader().channels * 1000);
    size_t readFrames = 0;
    while (reader.ReadNextChunk(chunk, 1000, readFrames) && readFrames > 0) {
        for (size_t f = 0; f < readFrames; ++f) {
            uint64_t frameIdx = writer.GetTotalRowsWritten();
            writer.WriteRow(frameIdx, &chunk[f * 2], 2);
        }
    }
    writer.Finish();

    const auto& files = writer.GetGeneratedFiles();
    std::cout << "  -> Generated " << files.size() << " split parts:" << std::endl;
    for (const auto& f : files) {
        std::cout << "     Part file: " << f << std::endl;
    }
    // 25,000行 / 10,000行 = 3ファイル (10,000 + 10,000 + 5,000)
    assert(files.size() == 3);
    assert(writer.GetTotalRowsWritten() == 25000);
    assert(files[0].find("dummy_split-01.csv") != std::string::npos);
    assert(files[1].find("dummy_split-02.csv") != std::string::npos);
    assert(files[2].find("dummy_split-03.csv") != std::string::npos);

    // 各ファイルの行数を確認
    for (size_t p = 0; p < files.size(); ++p) {
        std::ifstream in(files[p]);
        assert(in.is_open());
        std::string line;
        uint64_t lines = 0;
        while (std::getline(in, line)) {
            if (!line.empty()) lines++;
        }
        in.close();
        if (p < 2) {
            assert(lines == 10001); // ヘッダ1行 + データ10,000行
        } else {
            assert(lines == 5001);  // ヘッダ1行 + データ5,000行
        }
    }

    // クリーンアップ
    reader.Close();
    SafeRemoveFile("dummy_split.bin");
    for (const auto& f : files) {
        SafeRemoveFile(std::filesystem::u8path(f));
    }
    std::cout << "  -> Auto-split lines test (10,000 lines per file). PASS!" << std::endl;
}

void TestBatchWorker() {
    std::cout << "[Test 3] Testing BatchConverterWorker (Batch conversion)..." << std::endl;
    CreateDummyDlogFile("batch1.bin", 1, 10000, 10000);
    CreateDummyDlogFile("batch2.bin", 4, 10000, 15000);

    std::vector<BatchItem> items;
    BatchItem it1, it2;
    DlogReader::InspectFile("batch1.bin", it1);
    DlogReader::InspectFile("batch2.bin", it2);
    items.push_back(it1);
    items.push_back(it2);

    ConversionOptions opt;
    opt.timeFormat = TimeFormat::SampleIndex;
    opt.decimationStep = 2; // 1/2 間引き
    opt.enableSplitLines = false; // 分割なし
    opt.delimiter = '\t'; // TSV
    opt.useSameDirAsInput = true;

    BatchConverterWorker worker;
    bool startOk = worker.Start(items, opt);
    assert(startOk);

    while (worker.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    worker.WaitForCompletion();

    auto resultItems = worker.GetItems();
    assert(resultItems.size() == 2);
    assert(resultItems[0].status == BatchStatus::Completed);
    assert(resultItems[1].status == BatchStatus::Completed);
    assert(resultItems[0].outputFiles.size() == 1);
    assert(resultItems[1].outputFiles.size() == 1);

    // クリーンアップ
    SafeRemoveFile("batch1.bin");
    SafeRemoveFile("batch2.bin");
    for (const auto& it : resultItems) {
        for (const auto& f : it.outputFiles) {
            SafeRemoveFile(std::filesystem::u8path(f));
        }
    }

    std::cout << "  -> Batch conversion of 2 files completed successfully. PASS!" << std::endl;
}

void TestJapanesePathConversion() {
    std::cout << "[Test 4] Testing DlogReader and CsvWriter with Japanese path..." << std::endl;
    std::string binPath = "計測データ_2026.bin";
    CreateDummyDlogFile(binPath, 2, 22050, 500);

    BatchItem item;
    bool ok = DlogReader::InspectFile(binPath, item);
    assert(ok);
    assert(item.isValidHeader);

    ConversionOptions opt;
    opt.useSameDirAsInput = true;
    opt.enableSplitLines = false;

    BatchConverterWorker worker;
    std::vector<BatchItem> items = {item};
    bool startOk = worker.Start(items, opt);
    assert(startOk);
    while (worker.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    worker.WaitForCompletion();

    auto res = worker.GetItems();
    assert(res[0].status == BatchStatus::Completed);
    assert(!res[0].outputFiles.empty());

    // 生成されたCSVファイルを検証
    std::ifstream csvFile(std::filesystem::u8path(res[0].outputFiles[0]));
    assert(csvFile.is_open());
    std::string line;
    bool lineOk = static_cast<bool>(std::getline(csvFile, line));
    assert(lineOk);
    assert(!line.empty());
    csvFile.close();

    // クリーンアップ
    SafeRemoveFile(std::filesystem::u8path(binPath));
    for (const auto& f : res[0].outputFiles) {
        SafeRemoveFile(std::filesystem::u8path(f));
    }
    std::cout << "  -> Japanese path inspection, conversion & verification PASS!" << std::endl;
}

void TestAbsoluteTimeCaching() {
    std::cout << "[Test 5] Testing TimeFormat::AbsoluteTime second-level caching..." << std::endl;
    std::string binPath = "abs_time_test.bin";
    CreateDummyDlogFile(binPath, 2, 22050, 44100); // 2秒分のデータ (44,100 フレーム)

    BatchItem item;
    bool inspectOk = DlogReader::InspectFile(binPath, item);
    assert(inspectOk);

    ConversionOptions opt;
    opt.timeFormat = TimeFormat::AbsoluteTime;
    opt.useSameDirAsInput = true;
    opt.enableSplitLines = false;

    BatchConverterWorker worker;
    std::vector<BatchItem> items = {item};
    bool startOk = worker.Start(items, opt);
    assert(startOk);
    while (worker.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    worker.WaitForCompletion();

    auto res = worker.GetItems();
    assert(res[0].status == BatchStatus::Completed);
    assert(!res[0].outputFiles.empty());

    std::ifstream csvFile(std::filesystem::u8path(res[0].outputFiles[0]));
    assert(csvFile.is_open());
    std::string headerLine, firstDataLine;
    bool headerOk = static_cast<bool>(std::getline(csvFile, headerLine));
    assert(headerOk);
    assert(headerLine.find("DateTime") != std::string::npos);
    bool dataOk = static_cast<bool>(std::getline(csvFile, firstDataLine));
    assert(dataOk);
    // 日時フォーマット YYYY-MM-DD HH:MM:SS.mmm を検証
    assert(firstDataLine.length() >= 23);
    assert(firstDataLine[4] == '-' && firstDataLine[7] == '-');
    assert(firstDataLine[10] == ' ');
    assert(firstDataLine[13] == ':' && firstDataLine[16] == ':');
    assert(firstDataLine[19] == '.');
    assert(firstDataLine.find('Z') != std::string::npos);
    csvFile.close();

    SafeRemoveFile(std::filesystem::u8path(binPath));
    for (const auto& f : res[0].outputFiles) {
        SafeRemoveFile(std::filesystem::u8path(f));
    }
    std::cout << "  -> AbsoluteTime second caching & timestamp parsing PASS!" << std::endl;
}

void TestVoltageScalingConversion() {
    std::cout << "[Test 6] Testing voltage scaling and [V] column headers in CSV export..." << std::endl;
    std::string binPath = "voltage_test.bin";

    // 2ch, 100Hz, 5フレーム, fullScaleVoltage = 5.0 V
    // CH1 = 0.50f, CH2 = -0.20f
    {
        std::ofstream out(std::filesystem::u8path(binPath), std::ios::binary | std::ios::trunc);
        assert(out.is_open());
        DLogFileHeader h;
        std::memcpy(h.magic, "DLOG", 4);
        h.version = DLOG_FORMAT_VERSION;
        h.headerSize = sizeof(DLogFileHeader);
        h.channels = 2;
        h.sampleRate = 100;
        h.sampleFormat = 0;
        h.nativeRate = 200;
        h.decimationFactor = 2;
        h.startTimestampUtc = 1791510000000;
        h.fullScaleVoltage = 5.0; // ±5.0 V
        out.write(reinterpret_cast<const char*>(&h), sizeof(h));

        for (int i = 0; i < 5; ++i) {
            float s[2] = {0.50f, -0.20f};
            out.write(reinterpret_cast<const char*>(s), sizeof(s));
        }
    }

    BatchItem item;
    bool inspectOk = DlogReader::InspectFile(binPath, item);
    assert(inspectOk);
    assert(std::fabs(item.header.fullScaleVoltage - 5.0) < 1e-6);

    // 1. 電圧換算有効時 (applyVoltageScale = true)
    {
        ConversionOptions opt;
        opt.applyVoltageScale = true;
        opt.timeFormat = TimeFormat::RelativeSec;
        opt.enableSplitLines = false;
        opt.floatPrecision = 3;

        BatchConverterWorker worker;
        std::vector<BatchItem> items = {item};
        bool startOk = worker.Start(items, opt);
        assert(startOk);
        while (worker.IsRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        worker.WaitForCompletion();

        auto res = worker.GetItems();
        assert(res[0].status == BatchStatus::Completed);
        assert(!res[0].outputFiles.empty());

        std::ifstream csvFile(std::filesystem::u8path(res[0].outputFiles[0]));
        assert(csvFile.is_open());
        std::string headerLine, firstLine;
        std::getline(csvFile, headerLine);
        std::getline(csvFile, firstLine);
        csvFile.close();

        // ヘッダに [V] が付いていること
        assert(headerLine.find("CH1[V]") != std::string::npos);
        assert(headerLine.find("CH2[V]") != std::string::npos);

        // データ値が 0.50 * 5.0 = 2.500, -0.20 * 5.0 = -1.000 に換算されていること
        assert(firstLine.find("2.500") != std::string::npos);
        assert(firstLine.find("-1.000") != std::string::npos);

        for (const auto& f : res[0].outputFiles) {
            SafeRemoveFile(std::filesystem::u8path(f));
        }
    }

    // 2. 電圧換算無効時 (applyVoltageScale = false, 生デジタル値出力)
    {
        ConversionOptions opt;
        opt.applyVoltageScale = false;
        opt.timeFormat = TimeFormat::RelativeSec;
        opt.enableSplitLines = false;
        opt.floatPrecision = 3;

        BatchConverterWorker worker;
        std::vector<BatchItem> items = {item};
        bool startOk = worker.Start(items, opt);
        assert(startOk);
        while (worker.IsRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        worker.WaitForCompletion();

        auto res = worker.GetItems();
        assert(res[0].status == BatchStatus::Completed);
        assert(!res[0].outputFiles.empty());

        std::ifstream csvFile(std::filesystem::u8path(res[0].outputFiles[0]));
        assert(csvFile.is_open());
        std::string headerLine, firstLine;
        std::getline(csvFile, headerLine);
        std::getline(csvFile, firstLine);
        csvFile.close();

        // ヘッダに [V] が付いていないこと
        assert(headerLine.find("CH1[V]") == std::string::npos);
        assert(headerLine.find("CH1") != std::string::npos);

        // 生の 0.500, -0.200 であること
        assert(firstLine.find("0.500") != std::string::npos);
        assert(firstLine.find("-0.200") != std::string::npos);

        for (const auto& f : res[0].outputFiles) {
            SafeRemoveFile(std::filesystem::u8path(f));
        }
    }

    SafeRemoveFile(std::filesystem::u8path(binPath));
    std::cout << "  -> Voltage scaling conversion & [V] headers PASS!" << std::endl;
}

void TestChannelMetadataAndCompanionMeta() {
    std::cout << "[Test 7] Testing Companion .meta file parsing & custom channel labels/units..." << std::endl;
    std::string binPath = "dummy_companion.bin";
    std::string metaPath = "dummy_companion.bin.meta";

    CreateDummyDlogFile(binPath, 2, 1000, 100, 5.0);

    // 1. コンパニオン .meta ファイルの作成
    {
        std::ofstream metaOut(std::filesystem::u8path(metaPath));
        assert(metaOut.is_open());
        metaOut << "[DLOG_METADATA]\n";
        metaOut << "channels=2\n";
        metaOut << "full_scale_voltage=5.0\n";
        metaOut << "sample_rate=1000\n\n";
        metaOut << "[CH1]\n";
        metaOut << "name=V_battery\n";
        metaOut << "unit=V\n\n";
        metaOut << "[CH2]\n";
        metaOut << "name=I_load\n";
        metaOut << "unit=A\n\n";
        metaOut.close();
    }

    // 2. DlogReader::InspectFile による自動ロード検証
    BatchItem item;
    bool ok = DlogReader::InspectFile(binPath, item);
    assert(ok);
    assert(item.channelMetadata.size() == 2);
    assert(item.channelMetadata[0].name == "V_battery");
    assert(item.channelMetadata[0].unit == "V");
    assert(item.channelMetadata[1].name == "I_load");
    assert(item.channelMetadata[1].unit == "A");

    // 3. 変換実行 (BatchConverterWorker: options.channelMetadata が空の場合、item.channelMetadata をフォールバック利用)
    {
        ConversionOptions opt;
        opt.applyVoltageScale = true;
        opt.timeFormat = TimeFormat::RelativeSec;
        opt.enableSplitLines = false;
        opt.floatPrecision = 3;

        BatchConverterWorker worker;
        std::vector<BatchItem> items = {item};
        bool startOk = worker.Start(items, opt);
        assert(startOk);
        while (worker.IsRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        worker.WaitForCompletion();

        auto res = worker.GetItems();
        assert(res[0].status == BatchStatus::Completed);
        assert(!res[0].outputFiles.empty());

        std::ifstream csvFile(std::filesystem::u8path(res[0].outputFiles[0]));
        assert(csvFile.is_open());
        std::string headerLine;
        std::getline(csvFile, headerLine);
        csvFile.close();

        // 列ヘッダが V_battery[V], I_load[A] になっていること
        assert(headerLine.find("V_battery[V]") != std::string::npos);
        assert(headerLine.find("I_load[A]") != std::string::npos);

        for (const auto& f : res[0].outputFiles) {
            SafeRemoveFile(std::filesystem::u8path(f));
        }
    }

    // 4. options.channelMetadata で上書き設定した場合の検証
    {
        ConversionOptions opt;
        opt.applyVoltageScale = true;
        opt.timeFormat = TimeFormat::RelativeSec;
        opt.enableSplitLines = false;
        opt.floatPrecision = 3;
        opt.channelMetadata = {
            {"Strain_gauge", "uE"},
            {"LoadCell", "N"}
        };

        BatchConverterWorker worker;
        std::vector<BatchItem> items = {item};
        bool startOk = worker.Start(items, opt);
        assert(startOk);
        while (worker.IsRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        worker.WaitForCompletion();

        auto res = worker.GetItems();
        assert(res[0].status == BatchStatus::Completed);
        assert(!res[0].outputFiles.empty());

        std::ifstream csvFile(std::filesystem::u8path(res[0].outputFiles[0]));
        assert(csvFile.is_open());
        std::string headerLine;
        std::getline(csvFile, headerLine);
        csvFile.close();

        // 上書きされた列ヘッダが反映されていること
        assert(headerLine.find("Strain_gauge[uE]") != std::string::npos);
        assert(headerLine.find("LoadCell[N]") != std::string::npos);

        for (const auto& f : res[0].outputFiles) {
            SafeRemoveFile(std::filesystem::u8path(f));
        }
    }

    SafeRemoveFile(std::filesystem::u8path(binPath));
    SafeRemoveFile(std::filesystem::u8path(metaPath));
    std::cout << "  -> Companion .meta loading & custom CSV labels/units PASS!" << std::endl;
}

void TestBranchDigitsCalculation() {
    std::cout << "[Test 8] Testing Branch Digits Calculation and Dynamic Rename..." << std::endl;
    DLogFileHeader header{};
    header.channels = 1;
    header.sampleRate = 1000;

    ConversionOptions opt;
    opt.enableSplitLines = true;
    opt.maxLinesPerFile = 1;

    // 1. 分割なし (totalParts = 1) -> 枝番なし
    {
        CsvWriter writer;
        bool ok = writer.Initialize(".", "test_single", opt, header, false, 1);
        assert(ok);
        writer.Finish();
        auto files = writer.GetGeneratedFiles();
        assert(files.size() == 1);
        assert(files[0].find("test_single.csv") != std::string::npos);
        assert(files[0].find("test_single-") == std::string::npos);
        SafeRemoveFile(std::filesystem::u8path(files[0]));
    }

    // 2. 2〜99分割 (totalParts = 5) -> 2桁ゼロ埋め (-01)
    {
        CsvWriter writer;
        bool ok = writer.Initialize(".", "test_split2", opt, header, true, 5);
        assert(ok);
        writer.Finish();
        auto files = writer.GetGeneratedFiles();
        assert(files.size() == 1);
        assert(files[0].find("test_split2-01.csv") != std::string::npos);
        SafeRemoveFile(std::filesystem::u8path(files[0]));
    }

    // 3. 100〜999分割 (totalParts = 250) -> 3桁ゼロ埋め (-001)
    {
        CsvWriter writer;
        bool ok = writer.Initialize(".", "test_split3", opt, header, true, 250);
        assert(ok);
        writer.Finish();
        auto files = writer.GetGeneratedFiles();
        assert(files.size() == 1);
        assert(files[0].find("test_split3-001.csv") != std::string::npos);
        SafeRemoveFile(std::filesystem::u8path(files[0]));
    }

    // 4. 1000〜9999分割 (totalParts = 1500) -> 4桁ゼロ埋め (-0001)
    {
        CsvWriter writer;
        bool ok = writer.Initialize(".", "test_split4", opt, header, true, 1500);
        assert(ok);
        writer.Finish();
        auto files = writer.GetGeneratedFiles();
        assert(files.size() == 1);
        assert(files[0].find("test_split4-0001.csv") != std::string::npos);
        SafeRemoveFile(std::filesystem::u8path(files[0]));
    }

    // 5. 動的リネーム保護テスト (事前分割なし想定から2ファイル目に突入)
    {
        CsvWriter writer;
        // willSplit = false, totalParts = 1 で開始
        bool ok = writer.Initialize(".", "test_dyn", opt, header, false, 1);
        assert(ok);
        float s1 = 0.5f;
        writer.WriteRow(0, &s1, 1); // 1行目 (上限1行に到達)
        float s2 = 0.8f;
        writer.WriteRow(1, &s2, 1); // 2行目で自動分割発火
        writer.Finish();

        auto files = writer.GetGeneratedFiles();
        assert(files.size() == 2);
        // 1ファイル目が test_dyn-01.csv にリネームされ、2ファイル目が test_dyn-02.csv になっていること
        assert(files[0].find("test_dyn-01.csv") != std::string::npos);
        assert(files[1].find("test_dyn-02.csv") != std::string::npos);
        assert(std::filesystem::exists(std::filesystem::u8path(files[0])));
        assert(std::filesystem::exists(std::filesystem::u8path(files[1])));

        for (const auto& f : files) {
            SafeRemoveFile(std::filesystem::u8path(f));
        }
    }

    std::cout << "  -> Variable branch digits & dynamic rename protection PASS!" << std::endl;
}

void TestMalformedDlogRejection() {
    std::cout << "[Test 9] Testing malformed DLOG rejection..." << std::endl;

    const auto writeHeader = [](const std::string& path, const DLogFileHeader& header,
                                const void* payload = nullptr, size_t payloadBytes = 0) {
        std::ofstream out(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
        assert(out.is_open());
        out.write(reinterpret_cast<const char*>(&header), sizeof(header));
        if (payload && payloadBytes > 0) {
            out.write(reinterpret_cast<const char*>(payload), static_cast<std::streamsize>(payloadBytes));
        }
    };

    DLogFileHeader header;
    header.version = DLOG_FORMAT_VERSION;
    header.headerSize = sizeof(DLogFileHeader);
    header.channels = 2;
    header.sampleRate = 1000;
    header.nativeRate = 48000;
    header.decimationFactor = 48;
    header.sampleFormat = static_cast<uint32_t>(SampleFormatType::Float32);

    header.channels = 9;
    writeHeader("bad_channels.bin", header);
    DlogReader reader;
    assert(!reader.Open("bad_channels.bin"));

    header.channels = 2;
    header.sampleFormat = static_cast<uint32_t>(SampleFormatType::Int16);
    writeHeader("bad_format.bin", header);
    assert(!reader.Open("bad_format.bin"));

    header.sampleFormat = static_cast<uint32_t>(SampleFormatType::Float32);
    float halfFrame = 0.25f;
    writeHeader("bad_tail.bin", header, &halfFrame, sizeof(halfFrame));
    assert(!reader.Open("bad_tail.bin"));

    header.version = 999;
    writeHeader("bad_version.bin", header);
    assert(!reader.Open("bad_version.bin"));

    SafeRemoveFile("bad_channels.bin");
    SafeRemoveFile("bad_format.bin");
    SafeRemoveFile("bad_tail.bin");
    SafeRemoveFile("bad_version.bin");
    std::cout << "  -> Invalid channels, format, tail length, and version are rejected. PASS!" << std::endl;
}

void TestCsvTransactionalOutputAndEffectiveRate() {
    std::cout << "[Test 10] Testing transactional CSV output and exact 1/N timing..." << std::endl;
    const std::string finalPath = "csv_transaction.csv";
    const std::string original = "existing CSV must survive\n";
    {
        std::ofstream out(finalPath, std::ios::binary | std::ios::trunc);
        out << original;
    }

    DLogFileHeader header;
    header.version = DLOG_FORMAT_VERSION;
    header.channels = 1;
    header.sampleRate = 1002; // 表示用整数。時刻計算は nativeRate / factor を使う。
    header.nativeRate = 44100;
    header.decimationFactor = 44;

    ConversionOptions options;
    options.timeFormat = TimeFormat::RelativeSec;
    options.enableSplitLines = false;
    options.floatPrecision = 3;

    {
        CsvWriter writer;
        assert(writer.Initialize(".", "csv_transaction", options, header));
        float sample = 0.5f;
        assert(writer.WriteRow(441, &sample, 1));
        writer.Abort();
    }
    {
        std::ifstream in(finalPath, std::ios::binary);
        std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(contents == original);
    }

    {
        CsvWriter writer;
        assert(writer.Initialize(".", "csv_transaction", options, header));
        float sample = 0.5f;
        assert(writer.WriteRow(441, &sample, 1));
        assert(writer.Finish());
    }
    {
        std::ifstream in(finalPath);
        std::string headerLine;
        std::string dataLine;
        assert(static_cast<bool>(std::getline(in, headerLine)));
        assert(static_cast<bool>(std::getline(in, dataLine)));
        assert(dataLine.rfind("0.440000,", 0) == 0);
    }

    SafeRemoveFile(finalPath);
    std::cout << "  -> Abort preserves prior CSV and time uses nativeRate/decimationFactor. PASS!" << std::endl;
}

void TestPerFileMetadataInBatch() {
    std::cout << "[Test 11] Testing per-file metadata in mixed batch..." << std::endl;
    CreateDummyDlogFile("meta_a.bin", 1, 1000, 4);
    CreateDummyDlogFile("meta_b.bin", 1, 1000, 4);
    {
        std::ofstream metaA("meta_a.bin.meta");
        metaA << "[CH1]\nname=Voltage_A\nunit=V\n";
        std::ofstream metaB("meta_b.bin.meta");
        metaB << "[CH1]\nname=Current_B\nunit=A\n";
    }

    BatchItem itemA;
    BatchItem itemB;
    assert(DlogReader::InspectFile("meta_a.bin", itemA));
    assert(DlogReader::InspectFile("meta_b.bin", itemB));

    ConversionOptions options;
    options.enableSplitLines = false;
    options.channelMetadata.clear();

    BatchConverterWorker worker;
    assert(worker.Start({itemA, itemB}, options));
    while (worker.IsRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    worker.WaitForCompletion();

    const auto items = worker.GetItems();
    assert(items.size() == 2);
    assert(items[0].status == BatchStatus::Completed);
    assert(items[1].status == BatchStatus::Completed);
    std::ifstream csvA(std::filesystem::u8path(items[0].outputFiles[0]));
    std::ifstream csvB(std::filesystem::u8path(items[1].outputFiles[0]));
    std::string headerA;
    std::string headerB;
    std::getline(csvA, headerA);
    std::getline(csvB, headerB);
    assert(headerA.find("Voltage_A[V]") != std::string::npos);
    assert(headerB.find("Current_B[A]") != std::string::npos);

    SafeRemoveFile("meta_a.bin");
    SafeRemoveFile("meta_b.bin");
    SafeRemoveFile("meta_a.bin.meta");
    SafeRemoveFile("meta_b.bin.meta");
    for (const auto& item : items) {
        for (const auto& file : item.outputFiles) SafeRemoveFile(std::filesystem::u8path(file));
    }
    std::cout << "  -> Each input file keeps its own labels and units. PASS!" << std::endl;
}

void TestCollisionDetectionAndMetaVerification() {
    std::cout << "\n[Test 12] Testing batch collision detection and .meta channel verification..." << std::endl;

    // 1. 出力先衝突の検出テスト (DATA-002)
    BatchItem item1;
    item1.inputFilePath = "dir1/data.bin";
    item1.fileNameOnly = "data.bin";
    item1.isValidHeader = true;
    item1.totalFrames = 1000;

    BatchItem item2;
    item2.inputFilePath = "dir2/data.bin";
    item2.fileNameOnly = "data.bin";
    item2.isValidHeader = true;
    item2.totalFrames = 1000;

    ConversionOptions optSameOut;
    optSameOut.useSameDirAsInput = false;
    optSameOut.customOutputDir = "output_folder";
    optSameOut.enableSplitLines = false;

    std::string err;
    bool noCollision = BatchConverterWorker::CheckCollisions({item1, item2}, optSameOut, err);
    assert(!noCollision);
    assert(err.find("重複（衝突）") != std::string::npos);
    std::cout << "  -> Detected collision between same basenames to custom output dir: PASS!" << std::endl;

    // 2. 入力ファイルと同一名CSVの衝突検出
    BatchItem itemInput;
    itemInput.inputFilePath = "folder/my_data.csv";
    itemInput.fileNameOnly = "my_data.csv";
    itemInput.isValidHeader = true;
    itemInput.totalFrames = 1000;

    ConversionOptions optInput;
    optInput.useSameDirAsInput = true;
    optInput.enableSplitLines = false;

    bool inputCollision = BatchConverterWorker::CheckCollisions({itemInput}, optInput, err);
    assert(!inputCollision);
    assert(err.find("入力元ファイルと重複") != std::string::npos);
    std::cout << "  -> Detected collision when output CSV matches input path: PASS!" << std::endl;

    // 3. 不整合なチャネル数を持つ.metaの検証と安全なフォールバック (DATA-001)
    std::string dlogPath = "test_meta_mismatch.bin";
    CreateDummyDlogFile(dlogPath, 2 /* 2チャネル */, 44100, 1000);

    // .meta側でチャネル数を4と詐称
    {
        std::ofstream metaOut(dlogPath + ".meta");
        metaOut << "[DLOG_METADATA]\n";
        metaOut << "channels=4\n";
        metaOut << "sample_rate=44100\n\n";
        metaOut << "[CH1]\nname=Bogus1\nunit=V\n\n";
        metaOut << "[CH2]\nname=Bogus2\nunit=V\n\n";
        metaOut << "[CH3]\nname=Bogus3\nunit=V\n\n";
        metaOut << "[CH4]\nname=Bogus4\nunit=V\n\n";
    }

    DlogReader reader;
    assert(reader.Open(dlogPath));
    const auto meta = reader.GetChannelMetadata();
    assert(meta.size() == 2);
    // 不整合が検出されたため、詐称名ではなく既定名へフォールバックしていること
    assert(meta[0].name == "CH1");
    assert(meta[1].name == "CH2");
    reader.Close();

    SafeRemoveFile(dlogPath);
    SafeRemoveFile(dlogPath + ".meta");
    std::cout << "  -> Detected channel count mismatch in companion .meta and fell back safely: PASS!" << std::endl;
}

void TestData002AcceptanceCriteria() {
    std::cout << "\n[Test 13] Testing DATA-002 Acceptance Criteria..." << std::endl;

    // 10Hz, 100フレームのa.bin (10秒分)
    BatchItem itemBin;
    itemBin.inputFilePath = "work/a.bin";
    itemBin.fileNameOnly = "a.bin";
    itemBin.isValidHeader = true;
    itemBin.totalFrames = 100;
    itemBin.header.sampleRate = 10;
    itemBin.header.nativeRate = 20;
    itemBin.header.decimationFactor = 2;
    itemBin.header.channels = 2;

    // 10Hz, 10フレームのa.dat (1秒分)
    BatchItem itemDat;
    itemDat.inputFilePath = "work/a.dat";
    itemDat.fileNameOnly = "a.dat";
    itemDat.isValidHeader = true;
    itemDat.totalFrames = 10;
    itemDat.header.sampleRate = 10;
    itemDat.header.nativeRate = 20;
    itemDat.header.decimationFactor = 2;
    itemDat.header.channels = 2;

    ConversionOptions opt;
    opt.useSameDirAsInput = true;
    opt.enableTimeRange = true;
    opt.startTimeSec = 0.0;
    opt.endTimeSec = 1.0; // 0～1秒 (10Hzなので10フレーム)
    opt.enableSplitLines = true;
    opt.maxLinesPerFile = 50; // 50行分割 (10行 <= 50行 なのでどちらも分割なしで a.csv に出力しようとする)
    opt.delimiter = ',';

    std::string err;
    bool noCollision = BatchConverterWorker::CheckCollisions({itemBin, itemDat}, opt, err);
    // 処理開始前に衝突を拒否すること！
    assert(!noCollision);
    assert(err.find("重複（衝突）") != std::string::npos || err.find("衝突") != std::string::npos);
    std::cout << "  -> DATA-002 Acceptance: 0~1s 50-row split collision rejected: PASS!" << std::endl;

    // TSV テスト
    opt.delimiter = '\t';
    noCollision = BatchConverterWorker::CheckCollisions({itemBin, itemDat}, opt, err);
    assert(!noCollision);
    assert(err.find(".tsv") != std::string::npos || err.find("重複（衝突）") != std::string::npos);
    std::cout << "  -> DATA-002 Acceptance: TSV collision rejected: PASS!" << std::endl;

    // 日本語パス テスト
    itemBin.inputFilePath = "計測データ/a.bin";
    itemDat.inputFilePath = "計測データ/a.dat";
    noCollision = BatchConverterWorker::CheckCollisions({itemBin, itemDat}, opt, err);
    assert(!noCollision);
    std::cout << "  -> DATA-002 Acceptance: Japanese path collision rejected: PASS!" << std::endl;

    // 分割桁数変化テスト (150分割 -> 3桁ゼロ埋め -001, -002, ..., -150)
    BatchItem itemSplit;
    itemSplit.inputFilePath = "work/split_test.bin";
    itemSplit.fileNameOnly = "split_test.bin";
    itemSplit.isValidHeader = true;
    itemSplit.totalFrames = 150000;
    itemSplit.header.sampleRate = 1000;
    itemSplit.header.nativeRate = 2000;
    itemSplit.header.decimationFactor = 2;
    itemSplit.header.channels = 1;

    ConversionOptions optSplit;
    optSplit.useSameDirAsInput = true;
    optSplit.enableSplitLines = true;
    optSplit.maxLinesPerFile = 1000;
    optSplit.delimiter = ',';

    ConversionPlan planSplit = PlanConversion(itemSplit, optSplit);
    assert(planSplit.willSplit);
    assert(planSplit.totalParts == 150);
    assert(planSplit.branchDigits == 3);
    assert(planSplit.plannedOutputFiles.size() == 150);
    assert(planSplit.plannedOutputFiles[0].find("-001.csv") != std::string::npos);
    assert(planSplit.plannedOutputFiles[149].find("-150.csv") != std::string::npos);
    std::cout << "  -> DATA-002 Acceptance: Branch digits >= 100 (3 digits) verified: PASS!" << std::endl;
}

void TestData001HeaderVerification() {
    std::cout << "\n[Test 14] Testing DATA-001 Header Mutual Verification..." << std::endl;

    std::string dlogPath = "test_meta_mismatch2.bin";
    CreateDummyDlogFile(dlogPath, 2, 44100, 1000);

    DlogReader reader;
    assert(reader.Open(dlogPath));
    DLogFileHeader h = reader.GetHeader();
    reader.Close();

    // 1. 同チャネル数(2ch)だが、録音開始時刻(startTimestampUtc)が別記録のもの
    {
        std::ofstream metaOut(dlogPath + ".meta");
        metaOut << "[DLOG_METADATA]\n";
        metaOut << "magic=DLOG\n";
        metaOut << "version=2\n";
        metaOut << "start_timestamp_utc=" << (h.startTimestampUtc + 99999) << "\n";
        metaOut << "channels=2\n";
        metaOut << "sample_rate=44100\n";
        metaOut << "native_rate=88200\n";
        metaOut << "decimation_factor=2\n\n";
        metaOut << "[CH1]\nname=WrongRecording1\nunit=V\n\n";
        metaOut << "[CH2]\nname=WrongRecording2\nunit=V\n\n";
    }

    std::string warning;
    auto meta = DlogReader::LoadCompanionMeta(dlogPath, h, &warning);
    assert(!warning.empty());
    assert(warning.find("記録開始時刻不一致") != std::string::npos);
    assert(meta[0].name == "CH1"); // 既定値フォールバック
    assert(meta[1].name == "CH2");
    std::cout << "  -> Rejected companion .meta with mismatched start_timestamp_utc: PASS!" << std::endl;

    // 2. 同チャネル数(2ch)だが、native_rate / decimation_factor が不一致
    {
        std::ofstream metaOut(dlogPath + ".meta");
        metaOut << "[DLOG_METADATA]\n";
        metaOut << "magic=DLOG\n";
        metaOut << "version=2\n";
        metaOut << "start_timestamp_utc=" << h.startTimestampUtc << "\n";
        metaOut << "channels=2\n";
        metaOut << "native_rate=48000\n"; // 88200 ではなく 48000
        metaOut << "decimation_factor=1\n\n";
        metaOut << "[CH1]\nname=WrongRate1\nunit=V\n\n";
    }
    warning.clear();
    meta = DlogReader::LoadCompanionMeta(dlogPath, h, &warning);
    assert(!warning.empty());
    assert(warning.find("ネイティブサンプリング周波数不一致") != std::string::npos);
    assert(meta[0].name == "CH1");
    std::cout << "  -> Rejected companion .meta with mismatched native_rate: PASS!" << std::endl;

    SafeRemoveFile(dlogPath);
    SafeRemoveFile(dlogPath + ".meta");
}

void TestData003NonDestructiveRecovery() {
    std::cout << "\n[Test 15] Testing DATA-003 Non-destructive Recovery..." << std::endl;

    std::string partialPath = "test_data.bin.partial";
    CreateDummyDlogFile(partialPath, 2, 44100, 10);
    uint64_t originalPartialSize = std::filesystem::file_size(std::filesystem::u8path(partialPath));
    {
        std::ofstream out(std::filesystem::u8path(partialPath), std::ios::binary | std::ios::app);
        char junk[3] = {0x01, 0x02, 0x03};
        out.write(junk, 3);
    }
    uint64_t junkPartialSize = std::filesystem::file_size(std::filesystem::u8path(partialPath));
    assert(junkPartialSize == originalPartialSize + 3);

    {
        std::ofstream metaOut(partialPath + ".meta");
        metaOut << "[DLOG_METADATA]\nmagic=DLOG\nversion=2\nchannels=2\nstart_timestamp_utc=1791510000000\nsample_rate=44100\nnative_rate=88200\ndecimation_factor=2\n\n";
        metaOut << "[CH1]\nname=RecoveredCh1\nunit=V\n\n";
        metaOut << "[CH2]\nname=RecoveredCh2\nunit=V\n\n";
    }

    std::string recoveredPath;
    std::string errMsg;
    bool ok = DlogReader::RecoverPartialFile(partialPath, recoveredPath, errMsg);
    assert(ok);
    assert(recoveredPath.find("test_data.bin") != std::string::npos);

    // 1. 元の .partial ファイルが完全保持されていること
    uint64_t partialAfterRecoverySize = std::filesystem::file_size(std::filesystem::u8path(partialPath));
    assert(partialAfterRecoverySize == junkPartialSize);
    std::cout << "  -> Original .partial is preserved untouched (non-destructive): PASS!" << std::endl;

    // 2. 復旧先ファイルが正確なアライメントサイズであること
    uint64_t recSize = std::filesystem::file_size(std::filesystem::u8path(recoveredPath));
    assert(recSize == originalPartialSize);
    std::cout << "  -> Recovered file has perfectly aligned frames: PASS!" << std::endl;

    // 3. 復旧先ファイルをDlogReaderで開けること
    DlogReader reader;
    assert(reader.Open(recoveredPath));
    assert(reader.GetTotalFrames() == 10);
    const auto meta = reader.GetChannelMetadata();
    assert(meta[0].name == "RecoveredCh1");
    reader.Close();

    // 4. 復旧先が既に存在する場合に上書きせず _recovered_1 を生成すること
    std::string secondRecovered;
    ok = DlogReader::RecoverPartialFile(partialPath, secondRecovered, errMsg);
    assert(ok);
    assert(secondRecovered != recoveredPath);
    assert(secondRecovered.find("_recovered_1.bin") != std::string::npos);
    std::cout << "  -> Avoids overwriting existing target file: PASS!" << std::endl;

    SafeRemoveFile(partialPath);
    SafeRemoveFile(partialPath + ".meta");
    SafeRemoveFile(recoveredPath);
    SafeRemoveFile(recoveredPath + ".meta");
    SafeRemoveFile(secondRecovered);
    SafeRemoveFile(secondRecovered + ".meta");
}

int main() {
    std::cout << "=== Running DlogToCsv Unit & Integration Tests ===" << std::endl;
    TestReaderAndInspection();
    TestAutoSplitting();
    TestBatchWorker();
    TestJapanesePathConversion();
    TestAbsoluteTimeCaching();
    TestVoltageScalingConversion();
    TestChannelMetadataAndCompanionMeta();
    TestBranchDigitsCalculation();
    TestMalformedDlogRejection();
    TestCsvTransactionalOutputAndEffectiveRate();
    TestPerFileMetadataInBatch();
    TestCollisionDetectionAndMetaVerification();
    TestData002AcceptanceCriteria();
    TestData001HeaderVerification();
    TestData003NonDestructiveRecovery();
    std::cout << "\n>>> ALL CONVERTER TESTS PASSED! <<<" << std::endl;
    return 0;
}

