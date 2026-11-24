#include "app.h"
#include "ui/main_frame.h"
#include "common/language_preferences.h"
#include "common/localized_strings.h"

#include <wx/cmdline.h>
#ifdef __WXMSW__
#include <wx/iconbndl.h>
#include <windows.h>
#endif
#include <cstdlib>

namespace dlog {

void SignalLoggerApp::OnInitCmdLine(wxCmdLineParser& parser) {
    parser.AddSwitch(wxEmptyString, wxT("smoke-test"), wxT("Run startup smoke test"));
    parser.AddOption(wxEmptyString, wxT("language"), wxT("Override UI language (ja|en)"), wxCMD_LINE_VAL_STRING);
    wxApp::OnInitCmdLine(parser);
}

bool SignalLoggerApp::OnCmdLineParsed(wxCmdLineParser& parser) {
    m_smokeTest = parser.Found(wxT("smoke-test"));
    wxString langVal;
    if (parser.Found(wxT("language"), &langVal)) {
        std::string l = std::string(langVal.utf8_str());
        LanguagePreferences::Initialize(l.c_str());
    } else {
        LanguagePreferences::Initialize();
    }
    return wxApp::OnCmdLineParsed(parser);
}

bool SignalLoggerApp::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }

    auto* frame = new MainFrame(
        TrWx(StringId::AppTitleLogger),
        wxDefaultPosition,
        wxSize(960, 680)
    );
#ifdef __WXMSW__
    frame->SetIcons(wxIconBundle("APP_ICON", ::GetModuleHandleW(nullptr)));
#endif
    SetTopWindow(frame);
    frame->Centre();
    frame->Show(true);

    if (m_smokeTest) {
        CallAfter([this, frame]() {
            bool ok = frame->PerformSmokeTest();
#ifdef __WXMSW__
            const auto& icons = frame->GetIcons();
            ok = ok && icons.GetIconOfExactSize(16).IsOk() && icons.GetIconOfExactSize(32).IsOk()
                && icons.GetIconOfExactSize(256).IsOk();
#endif
            m_smokeTestExitCode = ok ? 0 : 1;
            frame->Close(true);
        });
        return true;
    }

    return true;
}

int SignalLoggerApp::OnRun() {
    int ret = wxApp::OnRun();
    if (m_smokeTest) {
        return m_smokeTestExitCode;
    }
    return ret;
}


} // namespace dlog
