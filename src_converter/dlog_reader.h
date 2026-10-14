#pragma once

#include "converter_types.h"
#include <fstream>
#include <string>
#include <vector>

namespace dlog {

class DlogReader {
public:
    DlogReader();
    ~DlogReader();

    /**
     * @brief DLOG ファイルを開き、ヘッダを検証
     * @param filePath 入力ファイルパス
     * @return 正常にヘッダを読めたか
     */
    bool Open(const std::string& filePath);

    /**
     * @brief ファイルを閉じる
     */
    void Close();

    /**
     * @brief 開いているか
     */
    bool IsOpen() const { return m_fileStream.is_open(); }

    /**
     * @brief ヘッダ情報を取得
     */
    const DLogFileHeader& GetHeader() const { return m_header; }

    /**
     * @brief チャネルメタデータを取得
     */
    const std::vector<ChannelMetadata>& GetChannelMetadata() const { return m_channelMetadata; }

    /**
     * @brief 総フレーム数を取得
     */
    uint64_t GetTotalFrames() const { return m_totalFrames; }

    /**
     * @brief ファイルサイズ（バイト）を取得
     */
    uint64_t GetFileSize() const { return m_fileSizeBytes; }

    /**
     * @brief 記録時間（秒）を取得
     */
    double GetDurationSec() const { return m_durationSec; }

    /**
     * @brief 直近のオープン・読込失敗理由
     */
    const std::string& GetLastError() const { return m_lastError; }

    /**
     * @brief 指定フレーム位置へシーク
     */
    bool SeekToFrame(uint64_t frameIndex);

    /**
     * @brief 次のチャンクを読み出し
     * @param buffer インターリーブ形式の float 出力先バッファ
     * @param maxFrames 読み出す最大フレーム数
     * @param framesRead 実際に読み出されたフレーム数
     * @return 読み出しに成功したか (EOF時も framesRead > 0 なら true)
     */
    bool ReadNextChunk(std::vector<float>& buffer, size_t maxFrames, size_t& framesRead);

    /**
     * @brief 指定フレーム位置から指定フレーム数を読み出し
     * @param startFrame 読み出し開始フレーム番号
     * @param frameCount 読み出すフレーム数
     * @param buffer インターリーブ形式の float 出力先バッファ
     * @param framesRead 実際に読み出されたフレーム数
     * @return 読み出しに成功したか (EOF時も framesRead > 0 なら true)
     */
    bool ReadFrames(uint64_t startFrame, size_t frameCount, std::vector<float>& buffer, size_t& framesRead);

    /**
     * @brief 静的ヘッダ検証ユーティリティ
     */
    static bool InspectFile(const std::string& filePath, BatchItem& item);

    /**
     * @brief 異常終了等で残された .partial 一時ファイルの回収・修復 (FUNC-001)
     * @param partialPath 回収対象の .partial ファイルパス
     * @param outRecoveredPath 回収・確定後のファイルパス
     * @param errorMsg 失敗時のエラーメッセージ
     * @return 正常に回収・修復できたか
     */
    static bool RecoverPartialFile(const std::string& partialPath,
                                  std::string& outRecoveredPath,
                                  std::string& errorMsg);

    /**
     * @brief コンパニオンメタデータ (.meta) の読込と整合性検証 (DATA-001)
     */
    static std::vector<ChannelMetadata> LoadCompanionMeta(const std::string& filePath,
                                                          const DLogFileHeader& header,
                                                          std::string* outWarning = nullptr);

    /**
     * @brief メタデータ読込時の警告（不整合等）を取得
     */
    const std::string& GetMetaWarning() const { return m_metaWarning; }

private:
    std::ifstream m_fileStream;
    DLogFileHeader m_header{};
    uint64_t m_fileSizeBytes = 0;
    uint64_t m_totalFrames = 0;
    double m_durationSec = 0.0;
    uint64_t m_currentFrameIndex = 0;
    std::vector<ChannelMetadata> m_channelMetadata;
    std::string m_lastError;
    std::string m_metaWarning;
};

} // namespace dlog

