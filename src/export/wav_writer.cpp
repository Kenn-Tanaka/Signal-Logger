#include "wav_writer.h"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <windows.h>

namespace dlog {

namespace {

inline void WriteLE16(std::ostream& os, uint16_t v) {
    uint8_t b[2] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF)
    };
    os.write(reinterpret_cast<const char*>(b), 2);
}

inline void WriteLE32(std::ostream& os, uint32_t v) {
    uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF)
    };
    os.write(reinterpret_cast<const char*>(b), 4);
}

inline void WriteLE64(std::ostream& os, uint64_t v) {
    uint8_t b[8] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF),
        static_cast<uint8_t>((v >> 32) & 0xFF),
        static_cast<uint8_t>((v >> 40) & 0xFF),
        static_cast<uint8_t>((v >> 48) & 0xFF),
        static_cast<uint8_t>((v >> 56) & 0xFF)
    };
    os.write(reinterpret_cast<const char*>(b), 8);
}

inline void WriteFourCC(std::ostream& os, const char* fourcc) {
    os.write(fourcc, 4);
}

// GUID: KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
// 00000003-0000-0010-8000-00aa00389b71
const uint8_t SUBTYPE_IEEE_FLOAT[16] = {
    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

// GUID: KSDATAFORMAT_SUBTYPE_PCM
// 00000001-0000-0010-8000-00aa00389b71
const uint8_t SUBTYPE_PCM[16] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

} // namespace

WavWriter::WavWriter() = default;

WavWriter::~WavWriter() {
    if (m_isOpen && !m_isCommitted) {
        Abort();
    }
}

bool WavWriter::Open(const std::string& destinationPath,
                     WavContainerFormat container,
                     uint32_t channels,
                     uint32_t sampleRate,
                     WavSampleFormat format,
                     std::string& outError) {
    m_destinationPath = destinationPath;
    m_container = container;
    m_channels = channels;
    m_sampleRate = sampleRate;
    m_format = format;
    m_framesWritten = 0;
    m_dataBytesWritten = 0;
    m_totalFileBytes = 0;
    m_pcm16ClippedSamples = 0;
    m_isOpen = false;
    m_isCommitted = false;

    if (m_channels == 0 || m_sampleRate == 0) {
        outError = "Invalid channel count or sample rate.";
        return false;
    }

    std::filesystem::path dst = std::filesystem::u8path(destinationPath);
    std::filesystem::path parentDir = dst.parent_path();
    if (parentDir.empty()) parentDir = std::filesystem::u8path(".");

    // 衝突を防止するユニークな一時ファイル名を生成
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::string tempFileName = ".~" + dst.filename().u8string() + "." + std::to_string(now) + ".tmp";
    std::filesystem::path tempP = parentDir / std::filesystem::u8path(tempFileName);
    m_tempPath = tempP.u8string();

#ifdef _WIN32
    m_fileStream.open(tempP, std::ios::binary | std::ios::trunc);
#else
    m_fileStream.open(m_tempPath, std::ios::binary | std::ios::trunc);
#endif

    if (!m_fileStream.is_open()) {
        outError = "Failed to create temporary export file: " + m_tempPath;
        return false;
    }

    m_isOpen = true;

    if (!WriteHeaderPlaceholder(outError)) {
        Abort();
        return false;
    }

    return true;
}

bool WavWriter::WriteHeaderPlaceholder(std::string& outError) {
    if (!m_fileStream.good()) {
        outError = "File stream not writable.";
        return false;
    }

    uint16_t bitsPerSample = (m_format == WavSampleFormat::IeeeFloat32) ? 32 : 16;
    uint16_t bytesPerSample = bitsPerSample / 8;
    uint16_t blockAlign = static_cast<uint16_t>(m_channels * bytesPerSample);
    uint32_t avgBytesPerSec = m_sampleRate * static_cast<uint32_t>(blockAlign);

    // 1. RIFF / RF64 ヘッダ
    if (m_container == WavContainerFormat::Rf64) {
        WriteFourCC(m_fileStream, "RF64");
        WriteLE32(m_fileStream, 0xFFFFFFFF); // -1 placeholder
        WriteFourCC(m_fileStream, "WAVE");

        // 2. ds64 チャンク (RF64 では直後に配置必須)
        WriteFourCC(m_fileStream, "ds64");
        WriteLE32(m_fileStream, 28); // ds64 chunk size
        m_ds64BodyOffset = m_fileStream.tellp();
        WriteLE64(m_fileStream, 0);  // riffSize64 placeholder
        WriteLE64(m_fileStream, 0);  // dataSize64 placeholder
        WriteLE64(m_fileStream, 0);  // sampleCount64 placeholder
        WriteLE32(m_fileStream, 0);  // tableLength = 0
    } else {
        WriteFourCC(m_fileStream, "RIFF");
        m_riffSizeOffset = m_fileStream.tellp();
        WriteLE32(m_fileStream, 0);  // riffSize placeholder
        WriteFourCC(m_fileStream, "WAVE");
    }

    // 3. fmt チャンク
    WriteFourCC(m_fileStream, "fmt ");
    bool useExtensible = (m_channels > 2);

    if (!useExtensible && m_format == WavSampleFormat::Pcm16) {
        // 標準 PCMWAVEFORMAT (16 bytes)
        WriteLE32(m_fileStream, 16);
        WriteLE16(m_fileStream, 0x0001); // WAVE_FORMAT_PCM
        WriteLE16(m_fileStream, static_cast<uint16_t>(m_channels));
        WriteLE32(m_fileStream, m_sampleRate);
        WriteLE32(m_fileStream, avgBytesPerSec);
        WriteLE16(m_fileStream, blockAlign);
        WriteLE16(m_fileStream, bitsPerSample);
    } else if (!useExtensible && m_format == WavSampleFormat::IeeeFloat32) {
        // 標準 WAVEFORMATEX (18 bytes, cbSize = 0)
        WriteLE32(m_fileStream, 18);
        WriteLE16(m_fileStream, 0x0003); // WAVE_FORMAT_IEEE_FLOAT
        WriteLE16(m_fileStream, static_cast<uint16_t>(m_channels));
        WriteLE32(m_fileStream, m_sampleRate);
        WriteLE32(m_fileStream, avgBytesPerSec);
        WriteLE16(m_fileStream, blockAlign);
        WriteLE16(m_fileStream, bitsPerSample);
        WriteLE16(m_fileStream, 0);      // cbSize = 0
    } else {
        // WAVEFORMATEXTENSIBLE (40 bytes, cbSize = 22)
        WriteLE32(m_fileStream, 40);
        WriteLE16(m_fileStream, 0xFFFE); // WAVE_FORMAT_EXTENSIBLE
        WriteLE16(m_fileStream, static_cast<uint16_t>(m_channels));
        WriteLE32(m_fileStream, m_sampleRate);
        WriteLE32(m_fileStream, avgBytesPerSec);
        WriteLE16(m_fileStream, blockAlign);
        WriteLE16(m_fileStream, bitsPerSample);
        WriteLE16(m_fileStream, 22);     // cbSize = 22
        WriteLE16(m_fileStream, bitsPerSample); // wValidBitsPerSample
        WriteLE32(m_fileStream, 0);      // dwChannelMask = 0 (unspecified / direct out)
        if (m_format == WavSampleFormat::IeeeFloat32) {
            m_fileStream.write(reinterpret_cast<const char*>(SUBTYPE_IEEE_FLOAT), 16);
        } else {
            m_fileStream.write(reinterpret_cast<const char*>(SUBTYPE_PCM), 16);
        }
    }

    // 4. fact チャンク (Float32形式では標準WAV・RF64ともに配置)
    if (m_format == WavSampleFormat::IeeeFloat32) {
        WriteFourCC(m_fileStream, "fact");
        WriteLE32(m_fileStream, 4);
        m_factLengthOffset = m_fileStream.tellp();
        WriteLE32(m_fileStream, (m_container == WavContainerFormat::Rf64) ? 0xFFFFFFFF : 0);
    }

    // 5. data チャンクヘッダ
    WriteFourCC(m_fileStream, "data");
    m_dataSizeOffset = m_fileStream.tellp();
    WriteLE32(m_fileStream, (m_container == WavContainerFormat::Rf64) ? 0xFFFFFFFF : 0);
    m_dataStartOffset = m_fileStream.tellp();

    if (!m_fileStream.good()) {
        outError = "Failed to write WAV header placeholders.";
        return false;
    }

    return true;
}

bool WavWriter::WriteFrames(const float* samples, size_t frameCount, std::string& outError) {
    if (!m_isOpen || !samples || frameCount == 0) return true;

    size_t totalSamples = frameCount * static_cast<size_t>(m_channels);

    if (m_format == WavSampleFormat::IeeeFloat32) {
        // Float32: バイト列を直接書き込み
        const char* rawBytes = reinterpret_cast<const char*>(samples);
        size_t bytesToWrite = totalSamples * sizeof(float);
        m_fileStream.write(rawBytes, bytesToWrite);

        if (!m_fileStream.good()) {
            outError = "Disk write failure while writing Float32 samples.";
            return false;
        }

        m_framesWritten += frameCount;
        m_dataBytesWritten += bytesToWrite;
    } else {
        // PCM16: 丸め・飽和・NaN/Inf検証
        std::vector<int16_t> pcmBuf(totalSamples);

        for (size_t i = 0; i < totalSamples; ++i) {
            float val = samples[i];
            if (!std::isfinite(val)) {
                outError = "Non-finite sample (NaN or Infinity) detected during PCM16 export.";
                return false;
            }

            int16_t s = 0;
            if (val > 1.0f) {
                m_pcm16ClippedSamples++;
                s = 32767;
            } else if (val < -1.0f) {
                m_pcm16ClippedSamples++;
                s = -32768;
            } else if (val >= 0.0f) {
                int32_t iv = static_cast<int32_t>(std::lround(val * 32767.0f));
                s = static_cast<int16_t>(std::clamp(iv, -32768, 32767));
            } else {
                int32_t iv = static_cast<int32_t>(std::lround(val * 32768.0f));
                s = static_cast<int16_t>(std::clamp(iv, -32768, 32767));
            }
            pcmBuf[i] = s;
        }

        const char* rawBytes = reinterpret_cast<const char*>(pcmBuf.data());
        size_t bytesToWrite = totalSamples * sizeof(int16_t);
        m_fileStream.write(rawBytes, bytesToWrite);

        if (!m_fileStream.good()) {
            outError = "Disk write failure while writing PCM16 samples.";
            return false;
        }

        m_framesWritten += frameCount;
        m_dataBytesWritten += bytesToWrite;
    }

    return true;
}

bool WavWriter::UpdateHeaderSizes(std::string& outError) {
    if (!m_fileStream.is_open()) return false;

    // パディングバイト (奇数サイズ時)
    if (m_dataBytesWritten % 2 != 0) {
        char pad = 0;
        m_fileStream.write(&pad, 1);
    }

    m_fileStream.flush();
    if (!m_fileStream.good()) {
        outError = "Failed to flush file stream before finalizing headers.";
        return false;
    }

    // ファイル総バイト数取得
    m_fileStream.seekp(0, std::ios::end);
    std::streampos endPos = m_fileStream.tellp();
    uint64_t totalFileSize = static_cast<uint64_t>(endPos);
    m_totalFileBytes = totalFileSize;
    uint64_t riffSize = (totalFileSize >= 8) ? (totalFileSize - 8) : 0;

    if (m_container == WavContainerFormat::StandardWav) {
        // 標準WAVのサイズ制限チェック
        if (totalFileSize > 0xFFFFFFFFULL || m_dataBytesWritten > 0xFFFFFFFFULL) {
            outError = "Output size exceeded standard WAV 32-bit limit upon finalization.";
            return false;
        }

        // RIFF チャンクサイズ更新
        m_fileStream.seekp(m_riffSizeOffset);
        WriteLE32(m_fileStream, static_cast<uint32_t>(riffSize));

        // fact チャンク更新 (Float32)
        if (m_format == WavSampleFormat::IeeeFloat32 && m_factLengthOffset != std::streampos(0)) {
            m_fileStream.seekp(m_factLengthOffset);
            WriteLE32(m_fileStream, static_cast<uint32_t>(m_framesWritten));
        }

        // data チャンクサイズ更新
        m_fileStream.seekp(m_dataSizeOffset);
        WriteLE32(m_fileStream, static_cast<uint32_t>(m_dataBytesWritten));
    } else {
        // RF64: ds64 チャンクの 64-bit サイズフィールド更新
        m_fileStream.seekp(m_ds64BodyOffset);
        WriteLE64(m_fileStream, riffSize);
        WriteLE64(m_fileStream, m_dataBytesWritten);
        WriteLE64(m_fileStream, m_framesWritten);
        WriteLE32(m_fileStream, 0); // tableLength = 0
    }

    m_fileStream.flush();
    if (!m_fileStream.good()) {
        outError = "Failed to update header size fields.";
        return false;
    }

    return true;
}

bool WavWriter::Commit(std::string& outError) {
    if (!m_isOpen) {
        outError = "Writer is not open.";
        return false;
    }

    if (!UpdateHeaderSizes(outError)) {
        Abort();
        return false;
    }

    m_fileStream.close();
    m_isOpen = false;

    // 非破壊アトミック置換
#ifdef _WIN32
    std::filesystem::path tempP = std::filesystem::u8path(m_tempPath);
    std::filesystem::path destP = std::filesystem::u8path(m_destinationPath);

    BOOL ok = MoveFileExW(tempP.wstring().c_str(), destP.wstring().c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        DWORD err = GetLastError();
        outError = "Failed to replace destination file atomically (Error code: " + std::to_string(err) + "). Temporary file remains at: " + m_tempPath;
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(std::filesystem::u8path(m_tempPath), std::filesystem::u8path(m_destinationPath), ec);
    if (ec) {
        outError = "Failed to rename temporary file to destination: " + ec.message() + " (Temporary file: " + m_tempPath + ")";
        return false;
    }
#endif

    m_isCommitted = true;
    return true;
}

void WavWriter::Abort(std::string* outWarning) {
    if (m_fileStream.is_open()) {
        m_fileStream.close();
    }
    m_isOpen = false;

    if (!m_isCommitted && !m_tempPath.empty()) {
        std::error_code ec;
        std::filesystem::path p = std::filesystem::u8path(m_tempPath);
        if (std::filesystem::exists(p, ec)) {
            std::filesystem::remove(p, ec);
            if (ec && outWarning) {
                *outWarning = "Warning: Failed to remove temporary export file: " + m_tempPath + " (" + ec.message() + ")";
            }
        }
    }
}

} // namespace dlog
