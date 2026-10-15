#include "common/language_preferences.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#endif

namespace dlog {

Language LanguagePreferences::s_currentLanguage = Language::English;
bool LanguagePreferences::s_hasCliOverride = false;
bool LanguagePreferences::s_initialized = false;
std::string LanguagePreferences::s_customSettingsPath;

void LanguagePreferences::Initialize(const std::string& cliLanguageOverride) {
    s_initialized = true;

    if (!cliLanguageOverride.empty()) {
        s_hasCliOverride = true;
        s_currentLanguage = LanguageFromCode(cliLanguageOverride, DetectSystemLanguage());
        return;
    }

    s_hasCliOverride = false;
    Language diskLang;
    if (LoadFromDisk(diskLang)) {
        s_currentLanguage = diskLang;
    } else {
        s_currentLanguage = DetectSystemLanguage();
    }
}

Language LanguagePreferences::GetCurrentLanguage() {
    if (!s_initialized) {
        Initialize();
    }
    return s_currentLanguage;
}

bool LanguagePreferences::SetCurrentLanguage(Language lang, bool persistToDisk, std::string* outError) {
    if (outError) outError->clear();
    s_currentLanguage = lang;
    s_initialized = true;

    if (s_hasCliOverride) {
        // CLI引数による固定指定時はディスク設定を変更しない
        return true;
    }

    if (!persistToDisk) {
        return true;
    }

    return SaveToDisk(lang, outError);
}

bool LanguagePreferences::HasCliOverride() {
    return s_hasCliOverride;
}

std::string LanguagePreferences::GetSettingsFilePath() {
    if (!s_customSettingsPath.empty()) {
        return s_customSettingsPath;
    }

#ifdef _WIN32
    wchar_t localAppData[MAX_PATH * 2];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, static_cast<DWORD>(sizeof(localAppData) / sizeof(localAppData[0])));
    if (len > 0 && len < (sizeof(localAppData) / sizeof(localAppData[0]))) {
        std::filesystem::path dir = std::filesystem::path(localAppData) / "DataLoggerSuite";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return (dir / "settings.ini").u8string();
    }
#endif

    // フォールバック: カレントディレクトリ近傍
    return "settings.ini";
}

bool LanguagePreferences::LoadFromDisk(Language& outLang, const std::string& filePath) {
    const std::string path = filePath.empty() ? GetSettingsFilePath() : filePath;
    std::ifstream in(std::filesystem::u8path(path));
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    int schemaVersion = 0;
    std::string langCode;

    auto trim = [](std::string s) -> std::string {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    };

    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') {
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));

        if (key == "schema_version") {
            try {
                schemaVersion = std::stoi(val);
            } catch (...) {
                schemaVersion = 0;
            }
        } else if (key == "language") {
            langCode = val;
        }
    }

    // スキーマバージョンが 1 で、言語コードが指定されているか検証
    if (schemaVersion != 1 || langCode.empty()) {
        return false;
    }

    if (langCode == "ja") {
        outLang = Language::Japanese;
        return true;
    } else if (langCode == "en") {
        outLang = Language::English;
        return true;
    }

    return false;
}

bool LanguagePreferences::SaveToDisk(Language lang, std::string* outError, const std::string& filePath) {
    if (outError) outError->clear();
    const std::string path = filePath.empty() ? GetSettingsFilePath() : filePath;

    std::filesystem::path finalPath = std::filesystem::u8path(path);
    std::filesystem::path dir = finalPath.parent_path();
    std::error_code ec;
    if (!dir.empty()) {
        std::filesystem::create_directories(dir, ec);
    }

    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
#ifdef _WIN32
    int pid = _getpid();
#else
    int pid = 0;
#endif

    std::string tmpFileName = finalPath.filename().u8string() + ".tmp." +
                              std::to_string(pid) + "_" + std::to_string(nowMs);
    std::filesystem::path tmpPath = dir.empty() ? std::filesystem::u8path(tmpFileName) : (dir / tmpFileName);

    {
        std::ofstream out(tmpPath, std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            if (outError) *outError = "Failed to open temporary settings file for writing: " + tmpPath.u8string();
            return false;
        }

        out << "[General]\n";
        out << "schema_version=1\n";
        out << "language=" << LanguageToCode(lang) << "\n";
        out.flush();
        if (!out.good()) {
            if (outError) *outError = "Failed to write settings content: " + tmpPath.u8string();
            out.close();
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

#ifdef _WIN32
    std::wstring finalW = finalPath.wstring();
    std::wstring tmpW = tmpPath.wstring();

    if (std::filesystem::exists(finalPath)) {
        if (!ReplaceFileW(finalW.c_str(), tmpW.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)) {
            DWORD repErr = GetLastError();
            // ReplaceFileW が失敗した場合は MoveFileExW で再試行
            if (!MoveFileExW(tmpW.c_str(), finalW.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                DWORD moveErr = GetLastError();
                if (outError) {
                    *outError = "Atomic replace failed (ReplaceFileW: " + std::to_string(repErr) +
                                ", MoveFileExW: " + std::to_string(moveErr) + ")";
                }
                // 置換失敗時に旧設定を削除しない (CFG-001)
                std::filesystem::remove(tmpPath, ec);
                return false;
            }
        }
    } else {
        if (!MoveFileExW(tmpW.c_str(), finalW.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DWORD moveErr = GetLastError();
            if (outError) {
                *outError = "MoveFileExW failed: " + std::to_string(moveErr);
            }
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }
#else
    std::filesystem::rename(tmpPath, finalPath, ec);
    if (ec) {
        if (outError) *outError = "Rename failed: " + ec.message();
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
#endif

    return true;
}

void LanguagePreferences::SetCustomSettingsPathForTesting(const std::string& customPath) {
    s_customSettingsPath = customPath;
}

void LanguagePreferences::ResetForTesting() {
    s_customSettingsPath.clear();
    s_hasCliOverride = false;
    s_initialized = false;
    s_currentLanguage = Language::English;
}

} // namespace dlog
