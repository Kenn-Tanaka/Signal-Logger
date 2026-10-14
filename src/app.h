#pragma once

#include <wx/app.h>

namespace dlog {

class DataLoggerApp : public wxApp {
public:
    DataLoggerApp() = default;
    ~DataLoggerApp() override = default;

    bool OnInit() override;
    int OnRun() override;
    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;

private:
    bool m_smokeTest = false;
    int m_smokeTestExitCode = 0;
};

} // namespace dlog
