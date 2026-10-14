#pragma once

#include "converter_types.h"
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace dlog {

class CsvWriter {
public:
    CsvWriter();
    ~CsvWriter();

    /**
     * @brief CSV 出力の初期化
     * @param baseOutputDir 出力先ディレクトリ
     * @param baseFileName 拡張子なしのベースファイル名
     * @param options 変換オプション
     * @param header DLOG ヘッダ情報
     * @param willSplit 事前計算で複数ファイルに分割されるか
     * @param totalParts 事前計算された分割数（1以下の場合は分割なし）
     * @return 成功したか
     */
    bool Initialize(const std::string& baseOutputDir,
                    const std::string& baseFileName,
                    const ConversionOptions& options,
                    const DLogFileHeader& header,
                    bool willSplit = false,
                    uint64_t totalParts = 1,
                    int branchDigits = 2);

    /**
     * @brief 1行書き込み
     * @param frameIndex サンプルフレームインデックス
     * @param samples インターリーブ形式の全チャネルサンプル配列
     * @param channelCount 全チャネル数
     * @return 書き込み成功したか
     */
    bool WriteRow(uint64_t frameIndex, const float* samples, int channelCount);

    /**
     * @brief 全書き込み完了・ファイルクローズ
     */
    bool Finish();

    /**
     * @brief 現在書込中の未確定ファイルを破棄（キャンセル用）
     */
    void Abort();

    const std::string& GetLastError() const { return m_lastError; }

    /**
     * @brief 生成された全ファイルパス一覧を取得
     */
    const std::vector<std::string>& GetGeneratedFiles() const { return m_generatedFiles; }

    /**
     * @brief 総書き込み行数を取得
     */
    uint64_t GetTotalRowsWritten() const { return m_totalRowsWritten; }

private:
    bool OpenNextFile();
    bool FinalizeCurrentFile();
    bool WriteHeaderLine();
    std::string FormatTimestamp(uint64_t frameIndex);

    ConversionOptions m_options;
    DLogFileHeader m_header;
    std::string m_baseOutputDir;
    std::string m_baseFileName;
    bool m_willSplit = false;
    int m_branchDigits = 2;

    std::ofstream m_fileStream;
    std::filesystem::path m_currentFinalPath;
    std::filesystem::path m_currentTemporaryPath;
    std::vector<char> m_streamBuffer; // 256KB 高速I/Oバッファ

    int m_currentPart = 0;
    uint64_t m_currentFileLines = 0;
    uint64_t m_totalRowsWritten = 0;
    std::vector<std::string> m_generatedFiles;

    // 出力対象チャネルのインデックスリスト
    std::vector<int> m_channelIndices;

    char m_delim = ',';
    std::string m_floatFormat;
    std::string m_lastError;
    bool m_hasError = false;

    // UTC絶対日時フォーマットの秒単位キャッシュ
    time_t m_cachedSec = static_cast<time_t>(-1);
    char m_cachedSecStr[32] = {0};
};

} // namespace dlog
