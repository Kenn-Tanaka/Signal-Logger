#include "converter_app.h"
#include "ui/converter_frame.h"
#include "common/language_preferences.h"
#include "common/localized_strings.h"

#include <wx/cmdline.h>

namespace dlog {

void ConverterApp::OnInitCmdLine(wxCmdLineParser& parser) {
    parser.AddSwitch(wxEmptyString, wxT("smoke-test"), wxT("Run startup smoke test"));
    parser.AddOption(wxEmptyString, wxT("language"), wxT("Override UI language (ja|en)"), wxCMD_LINE_VAL_STRING);
    parser.AddParam(wxT("input file"), wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL | wxCMD_LINE_PARAM_MULTIPLE);
    wxApp::OnInitCmdLine(parser);
}

bool ConverterApp::OnCmdLineParsed(wxCmdLineParser& parser) {
    m_smokeTest = parser.Found(wxT("smoke-test"));
    wxString langVal;
    if (parser.Found(wxT("language"), &langVal)) {
        std::string l = std::string(langVal.utf8_str());
        LanguagePreferences::Initialize(l.c_str());
    } else {
        LanguagePreferences::Initialize();
    }

    m_inputFiles.clear();
    for (size_t i = 0; i < parser.GetParamCount(); ++i) {
        m_inputFiles.Add(parser.GetParam(i));
    }

    return wxApp::OnCmdLineParsed(parser);
}

bool ConverterApp::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }

    auto* frame = new ConverterFrame(
        TrWx(StringId::AppTitleConverter),
        wxDefaultPosition,
        wxSize(920, 720)
    );
    SetTopWindow(frame);
    frame->Centre();

    // パーサが返した位置引数のみを入力ファイルとして追加 (CLI-001)
    if (!m_inputFiles.empty()) {
        frame->AddFiles(m_inputFiles);
    }

    frame->Show(true);

    if (m_smokeTest) {
        CallAfter([this, frame]() {
            m_smokeTestExitCode = 0;
            frame->Close(true);
        });
        return true;
    }

    return true;
}

int ConverterApp::OnRun() {
    int ret = wxApp::OnRun();
    if (m_smokeTest) {
        return m_smokeTestExitCode;
    }
    return ret;
}

} // namespace dlog
