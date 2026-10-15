#include "app.h"
#include "ui/main_frame.h"

#include <wx/cmdline.h>
#include <cstdlib>

namespace dlog {

void DataLoggerApp::OnInitCmdLine(wxCmdLineParser& parser) {
    parser.AddSwitch(wxEmptyString, wxT("smoke-test"), wxT("Run startup smoke test"));
    wxApp::OnInitCmdLine(parser);
}

bool DataLoggerApp::OnCmdLineParsed(wxCmdLineParser& parser) {
    m_smokeTest = parser.Found(wxT("smoke-test"));
    return wxApp::OnCmdLineParsed(parser);
}

bool DataLoggerApp::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }

    auto* frame = new MainFrame(
        wxT("シグナル・ロガー"),
        wxDefaultPosition,
        wxSize(960, 680)
    );
    SetTopWindow(frame);
    frame->Centre();
    frame->Show(true);

    if (m_smokeTest) {
        CallAfter([this, frame]() {
            bool ok = frame->PerformSmokeTest();
            m_smokeTestExitCode = ok ? 0 : 1;
            frame->Close(true);
        });
        return true;
    }

    return true;
}

int DataLoggerApp::OnRun() {
    int ret = wxApp::OnRun();
    if (m_smokeTest) {
        return m_smokeTestExitCode;
    }
    return ret;
}


} // namespace dlog
