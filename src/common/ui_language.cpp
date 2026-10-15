#include "common/ui_language.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace dlog {

Language DetectSystemLanguage() {
#ifdef _WIN32
    LANGID langId = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(langId);
    if (primary == LANG_JAPANESE) {
        return Language::Japanese;
    }
#endif
    return Language::English;
}

} // namespace dlog
