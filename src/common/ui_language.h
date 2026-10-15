#pragma once

#include <string>

namespace dlog {

enum class Language {
    Japanese,
    English
};

inline const char* LanguageToCode(Language lang) {
    return (lang == Language::Japanese) ? "ja" : "en";
}

inline Language LanguageFromCode(const std::string& code, Language fallback = Language::English) {
    if (code == "ja" || code == "ja_JP" || code == "ja-JP" || code == "Japanese" || code == "japanese") {
        return Language::Japanese;
    }
    if (code == "en" || code == "en_US" || code == "en-US" || code == "English" || code == "english") {
        return Language::English;
    }
    return fallback;
}

Language DetectSystemLanguage();

} // namespace dlog
