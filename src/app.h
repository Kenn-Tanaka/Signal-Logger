#pragma once

#include <wx/app.h>

namespace dlog {

class SignalLoggerApp : public wxApp {
public:
    SignalLoggerApp() = default;
    ~SignalLoggerApp() override = default;

    bool OnInit() override;
    int OnRun() override;
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

private:
    bool m_smokeTest = false;
    int m_smokeTestExitCode = 0;
};

} // namespace dlog
