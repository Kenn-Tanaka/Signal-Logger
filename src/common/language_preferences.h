#pragma once

#include "common/ui_language.h"
#include <string>

namespace dlog {

class LanguagePreferences {
public:
    /**
     * @brief 初期化（CLI引数があれば反映、無ければ設定ファイルまたはOS言語から取得）
     * @param cliLanguageOverride コマンドラインで指定された言語コード（空文字列なら指定なし）
     */
    static void Initialize(const std::string& cliLanguageOverride = "");

    /**
     * @brief 現在選択されている言語を取得
     */
    static Language GetCurrentLanguage();

    /**
     * @brief 言語を変更（必要に応じてディスクへ即時永続化）
     * @param lang 切り替える言語
     * @param persistToDisk ディスクに保存するかどうか (CLI指定時は無視され保存されない)
     * @param outError 保存失敗時のエラーメッセージ格納先 (任意)
     * @return 正常に設定されたか（ディスク保存失敗時は false だがメモリ上は切り替わる）
     */
    static bool SetCurrentLanguage(Language lang, bool persistToDisk = true, std::string* outError = nullptr);

    static bool SaveLanguage(Language lang, std::string* outError = nullptr) {
        return SetCurrentLanguage(lang, true, outError);
    }

    /**
     * @brief コマンドライン引数による言語固定指定があるか
     */
    static bool HasCliOverride();

    /**
     * @brief 設定ファイル (settings.ini) の保存先パスを取得
     */
    static std::string GetSettingsFilePath();

    /**
     * @brief 設定ファイルから言語を読み込み
     * @param outLang 読み出された言語
     * @param filePath 読み込み対象パス（空なら既定パス）
     * @return 正常に読めたか
     */
    static bool LoadFromDisk(Language& outLang, const std::string& filePath = "");

    /**
     * @brief 設定ファイルへ言語をアトミック保存
     * @param lang 保存する言語
     * @param outError エラーメッセージ格納先 (任意)
     * @param filePath 保存対象パス（空なら既定パス）
     * @return 正常に保存できたか
     */
    static bool SaveToDisk(Language lang, std::string* outError = nullptr, const std::string& filePath = "");

    /**
     * @brief テスト用: 設定ファイルパスの強制指定（空文字列でリセット）
     */
    static void SetCustomSettingsPathForTesting(const std::string& customPath);

    /**
     * @brief テスト用: 内部状態のリセット
     */
    static void ResetForTesting();

private:
    static Language s_currentLanguage;
    static bool s_hasCliOverride;
    static bool s_initialized;
    static std::string s_customSettingsPath;
};

} // namespace dlog
