#pragma once

#include "wav_export_types.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <cstddef>

namespace dlog {

class WavWriter {
public:
    WavWriter();
    ~WavWriter();

    /**
     * @brief WAVまたはRF64出力ファイルを開く (非破壊一時ファイル使用)
     * @param destinationPath 最終出力先ファイルパス
     * @param container コンテナ形式 (StandardWav または Rf64)
     * @param channels 出力チャネル数
     * @param sampleRate 出力サンプリング周波数 (nativeRate)
     * @param format サンプル形式 (IeeeFloat32 または Pcm16)
     * @param outError エラー理由格納先
     * @return 成功なら true
     */
    bool Open(const std::string& destinationPath,
              WavContainerFormat container,
              uint32_t channels,
              uint32_t sampleRate,
              WavSampleFormat format,
              std::string& outError);

    /**
     * @brief インターリーブ浮動小数点サンプルチャンクを書き込み
     * (PCM16形式指定時は内部で丸め・飽和・NaN検証を行って変換)
     * @param samples インターリーブ形式の float サンプル配列 (要素数 = frameCount * channels)
     * @param frameCount フレーム数
     * @param outError エラー理由格納先 (NaN/Inf検出時や書込失敗時)
     * @return 成功なら true
     */
    bool WriteFrames(const float* samples, size_t frameCount, std::string& outError);

    /**
     * @brief 書き込みを確定し、ヘッダサイズを更新して一時ファイルを最終パスへ置換
     * @param outError エラー理由格納先
     * @return 成功なら true
     */
    bool Commit(std::string& outError);

    /**
     * @brief 処理中断・異常終了時に一時ファイルを安全に削除し、既存出力を保護
     * @param outWarning 一時ファイル削除失敗時の警告格納先
     */
    void Abort(std::string* outWarning = nullptr);

    uint64_t GetFramesWritten() const { return m_framesWritten; }
    uint64_t GetDataBytesWritten() const { return m_dataBytesWritten; }
    uint64_t GetTotalFileBytes() const { return m_totalFileBytes; }
    uint64_t GetPcm16ClippedSamples() const { return m_pcm16ClippedSamples; }
    const std::string& GetTempPath() const { return m_tempPath; }
    const std::string& GetDestinationPath() const { return m_destinationPath; }

private:
    bool WriteHeaderPlaceholder(std::string& outError);
    bool UpdateHeaderSizes(std::string& outError);

    std::string m_destinationPath;
    std::string m_tempPath;
    std::ofstream m_fileStream;

    WavContainerFormat m_container = WavContainerFormat::StandardWav;
    uint32_t m_channels = 1;
    uint32_t m_sampleRate = 44100;
    WavSampleFormat m_format = WavSampleFormat::IeeeFloat32;

    uint64_t m_framesWritten = 0;
    uint64_t m_dataBytesWritten = 0;
    uint64_t m_totalFileBytes = 0;
    uint64_t m_pcm16ClippedSamples = 0;

    // ヘッダ位置記録用オフセット
    std::streampos m_ds64BodyOffset = 0;
    std::streampos m_riffSizeOffset = 0;
    std::streampos m_factLengthOffset = 0;
    std::streampos m_dataSizeOffset = 0;
    std::streampos m_dataStartOffset = 0;

    bool m_isOpen = false;
    bool m_isCommitted = false;
};

} // namespace dlog
