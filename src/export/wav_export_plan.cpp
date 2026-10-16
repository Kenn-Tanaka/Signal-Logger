#include "wav_export_plan.h"
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <limits>

namespace dlog {

bool WavExportPlan::CalculateFrameRange(double timeStart, double timeEnd, uint64_t totalFrames,
                                        double effectiveRate, uint64_t& outStartFrame,
                                        uint64_t& outEndFrame, std::string& outError,
                                        LocalizedError* outLocError) {
    if (outLocError) outLocError->Clear();

    if (!std::isfinite(timeStart) || !std::isfinite(timeEnd) || !std::isfinite(effectiveRate)) {
        outError = "Time values must be finite numbers.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavInvalidFrameRange);
        return false;
    }
    if (effectiveRate <= 0.0) {
        outError = "Effective sample rate must be positive.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavInvalidHeader);
        return false;
    }
    if (totalFrames == 0) {
        outError = "Input file has 0 frames.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavInvalidHeader);
        return false;
    }
    if (timeStart < 0.0) {
        outError = "Start time cannot be negative.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavInvalidFrameRange);
        return false;
    }
    if (timeEnd <= timeStart) {
        outError = "End time must be strictly greater than start time.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavInvalidFrameRange);
        return false;
    }

    double maxDuration = static_cast<double>(totalFrames) / effectiveRate;
    if (timeStart >= maxDuration) {
        outError = "Start time is beyond the end of the file.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavInvalidFrameRange);
        return false;
    }

    // サンプル時刻 t_i = i / effectiveRate が [timeStart, timeEnd) に入るインデックスを算出
    // 浮動小数点境界誤差を考慮 (epsilon = 1e-7 / Fs)
    double eps = 1e-7 / effectiveRate;
    double rawStart = (timeStart - eps) * effectiveRate;
    double rawEnd = (timeEnd - eps) * effectiveRate;

    int64_t sFrame = static_cast<int64_t>(std::ceil(rawStart));
    int64_t eFrame = static_cast<int64_t>(std::floor(rawEnd)) + 1;

    if (sFrame < 0) sFrame = 0;
    if (static_cast<uint64_t>(sFrame) >= totalFrames) {
        outError = "Start frame is beyond the end of the file.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavInvalidFrameRange);
        return false;
    }
    if (eFrame < sFrame + 1) {
        outError = "Selected time range contains no samples.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavInvalidFrameRange);
        return false;
    }
    if (static_cast<uint64_t>(eFrame) > totalFrames) {
        eFrame = static_cast<int64_t>(totalFrames);
    }

    outStartFrame = static_cast<uint64_t>(sFrame);
    outEndFrame = static_cast<uint64_t>(eFrame);
    return true;
}

uint64_t WavExportPlan::CalculateHeaderSize(WavContainerFormat container, uint32_t channels, WavSampleFormat format) {
    uint64_t size = 0;

    // RIFF / RF64 ヘッダ
    size += 12; // "RIFF/RF64" (4) + size (4) + "WAVE" (4)

    if (container == WavContainerFormat::Rf64) {
        size += 8 + 28; // "ds64" (4) + chunk_size (4) + ds64_body (28)
    }

    // fmt チャンク
    if (channels <= 2 && format == WavSampleFormat::Pcm16) {
        size += 8 + 16; // "fmt " (4) + 16 (4) + PCMWAVEFORMAT (16)
    } else if (channels <= 2 && format == WavSampleFormat::IeeeFloat32) {
        size += 8 + 18; // "fmt " (4) + 18 (4) + WAVEFORMATEX (18)
    } else {
        size += 8 + 40; // "fmt " (4) + 40 (4) + WAVEFORMATEXTENSIBLE (40)
    }

    // fact チャンク (Float32形式では必須)
    if (format == WavSampleFormat::IeeeFloat32) {
        size += 8 + 4; // "fact" (4) + 4 (4) + sample_length (4)
    }

    // data チャンクヘッダ
    size += 8; // "data" (4) + size (4)

    return size;
}

WavExportEstimate WavExportPlan::AnalyzePlan(const DLogFileHeader& header, uint64_t totalFrames,
                                             const WavExportOptions& options) {
    WavExportEstimate est;
    est.nativeRate = header.nativeRate;
    est.decimationFactor = std::max(1u, header.decimationFactor);
    est.interpolationFactor = est.decimationFactor;
    est.outputRate = header.nativeRate;
    est.bytesPerSample = (options.sampleFormat == WavSampleFormat::IeeeFloat32) ? 4 : 2;

    if (header.nativeRate == 0 || totalFrames == 0 || header.channels == 0 || header.channels > 8) {
        est.isValid = false;
        est.validationError = "Invalid DLOG file header (nativeRate, channels, or frames is invalid).";
        est.errorInfo = MakeError(StringId::ErrWavInvalidHeader);
        return est;
    }

    est.effectiveInputRate = static_cast<double>(header.nativeRate) / static_cast<double>(est.decimationFactor);

    // チャネルの検証
    std::vector<int> chs = options.selectedChannels;
    if (chs.empty() && !options.explicitChannelSelection) {
        // デフォルトは全チャネル
        for (uint32_t c = 0; c < header.channels; ++c) {
            chs.push_back(static_cast<int>(c));
        }
    }

    // 重複除去しつつ範囲チェック・昇順整列
    std::vector<int> validChannels;
    for (int ch : chs) {
        if (ch < 0 || static_cast<uint32_t>(ch) >= header.channels) {
            est.isValid = false;
            est.validationError = "Invalid channel index selected: " + std::to_string(ch);
            est.errorInfo = MakeError(StringId::ErrWavInvalidChannelIndexFormat, std::to_string(ch));
            return est;
        }
        if (std::find(validChannels.begin(), validChannels.end(), ch) == validChannels.end()) {
            validChannels.push_back(ch);
        }
    }
    std::sort(validChannels.begin(), validChannels.end());

    if (validChannels.empty()) {
        est.isValid = false;
        est.validationError = "At least one output channel must be selected.";
        est.errorInfo = MakeError(StringId::ErrWavNoChannelsSelected);
        return est;
    }
    est.channelCount = static_cast<uint32_t>(validChannels.size());

    // 入力フレーム範囲の決定
    if (options.exportEntireFile) {
        est.inputFrames = totalFrames;
    } else {
        if (options.startFrame >= options.endFrame || options.endFrame > totalFrames) {
            est.isValid = false;
            est.validationError = "Invalid export frame range.";
            est.errorInfo = MakeError(StringId::ErrWavInvalidFrameRange);
            return est;
        }
        est.inputFrames = options.endFrame - options.startFrame;
    }

    if (est.inputFrames == 0) {
        est.isValid = false;
        est.validationError = "Selected export range contains 0 frames.";
        est.errorInfo = MakeError(StringId::ErrWavInvalidFrameRange);
        return est;
    }

    // 出力フレーム数 M * N の計算とオーバーフロー検出
    uint64_t M = est.inputFrames;
    uint64_t N = est.interpolationFactor;
    if (N > 0 && M > (std::numeric_limits<uint64_t>::max() / N)) {
        est.sizeCalculationOverflow = true;
        est.isValid = false;
        est.validationError = "Output frame count exceeds 64-bit integer range.";
        est.errorInfo = MakeError(StringId::ErrWavGeneral);
        return est;
    }
    est.outputFrames = M * N;
    est.outputDurationSec = static_cast<double>(est.outputFrames) / static_cast<double>(est.outputRate);

    // dataバイト数の計算とオーバーフロー検出
    uint64_t frameBytes = static_cast<uint64_t>(est.channelCount) * static_cast<uint64_t>(est.bytesPerSample);
    if (frameBytes > 0 && est.outputFrames > (std::numeric_limits<uint64_t>::max() / frameBytes)) {
        est.sizeCalculationOverflow = true;
        est.isValid = false;
        est.validationError = "Output data size exceeds 64-bit integer range.";
        est.errorInfo = MakeError(StringId::ErrWavGeneral);
        return est;
    }
    est.estimatedDataBytes = est.outputFrames * frameBytes;

    // パディングバイト (dataチャンクサイズが奇数なら1バイト追加)
    uint64_t pad = (est.estimatedDataBytes % 2 != 0) ? 1 : 0;

    // 4 GiB境界の判定
    // 標準WAVのヘッダサイズを基に暫定計算
    uint64_t stdHdr = CalculateHeaderSize(WavContainerFormat::StandardWav, est.channelCount, options.sampleFormat);
    uint64_t totalStd = stdHdr + est.estimatedDataBytes + pad;

    bool exceedsLimit = (totalStd >= STANDARD_WAV_LIMIT_BYTES || est.estimatedDataBytes >= STANDARD_WAV_LIMIT_BYTES);
    est.exceeds4GiBLimit = exceedsLimit;

    if (options.limitTo4GiB) {
        if (exceedsLimit) {
            est.containerFormat = WavContainerFormat::StandardWav;
            est.estimatedTotalBytes = totalStd;
            est.isValid = false;
            est.validationError = "Output size exceeds 4 GiB standard WAV limit. Enable RF64 by unchecking 4 GiB limit, or narrow the time range.";
            est.errorInfo = MakeError(StringId::ErrWavOutputSizeExceeds4GiB);
            return est;
        }
        est.containerFormat = WavContainerFormat::StandardWav;
        est.estimatedTotalBytes = totalStd;
    } else {
        if (exceedsLimit) {
            est.containerFormat = WavContainerFormat::Rf64;
            uint64_t rfHdr = CalculateHeaderSize(WavContainerFormat::Rf64, est.channelCount, options.sampleFormat);
            est.estimatedTotalBytes = rfHdr + est.estimatedDataBytes + pad;
        } else {
            est.containerFormat = WavContainerFormat::StandardWav;
            est.estimatedTotalBytes = totalStd;
        }
    }

    est.isValid = true;
    return est;
}

bool WavExportPlan::ValidatePaths(const std::string& sourceDlogPath, const std::string& destinationWavPath,
                                  std::string& outError, LocalizedError* outLocError) {
    if (outLocError) outLocError->Clear();

    if (destinationWavPath.empty()) {
        outError = "Destination path is empty.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavDestEmpty);
        return false;
    }

    std::filesystem::path src = std::filesystem::u8path(sourceDlogPath);
    std::filesystem::path dst = std::filesystem::u8path(destinationWavPath);

    // 拡張子のチェック (.wav)
    std::string ext = dst.extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext != ".wav") {
        outError = "Destination file must have .wav extension.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavExtMustBeWav);
        return false;
    }

    std::error_code ec;
    // 同一ファイル判定 (正規化・実パス比較)
    if (std::filesystem::exists(src, ec) && std::filesystem::exists(dst, ec)) {
        if (std::filesystem::equivalent(src, dst, ec)) {
            outError = "Destination cannot be identical to source DLOG file.";
            if (outLocError) *outLocError = MakeError(StringId::ErrWavSameAsSource);
            return false;
        }
    }

    // 入力ファイルの付随メタデータ (.meta) との同一性判定
    std::filesystem::path srcMeta = src;
    srcMeta += ".meta";
    if (std::filesystem::exists(srcMeta, ec) && std::filesystem::exists(dst, ec)) {
        if (std::filesystem::equivalent(srcMeta, dst, ec)) {
            outError = "Destination cannot be identical to companion .meta file.";
            if (outLocError) *outLocError = MakeError(StringId::ErrWavOverwriteMeta);
            return false;
        }
    }

    // パス文字列自体の単純一致チェック (非存在時も保護)
    auto normSrc = std::filesystem::weakly_canonical(src, ec);
    auto normDst = std::filesystem::weakly_canonical(dst, ec);
    if (!ec && normSrc == normDst) {
        outError = "Destination cannot be identical to source DLOG file.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavSameAsSource);
        return false;
    }

    auto normMeta = std::filesystem::weakly_canonical(srcMeta, ec);
    if (!ec && normDst == normMeta) {
        outError = "Destination cannot be identical to companion .meta file.";
        if (outLocError) *outLocError = MakeError(StringId::ErrWavOverwriteMeta);
        return false;
    }

    // ディレクトリ存在チェック
    std::filesystem::path parentDir = dst.parent_path();
    if (!parentDir.empty() && !std::filesystem::exists(parentDir, ec)) {
        outError = "Destination directory does not exist.";
        if (outLocError) *outLocError = MakeError(StringId::OutputDirNotFound);
        return false;
    }

    return true;
}

} // namespace dlog
