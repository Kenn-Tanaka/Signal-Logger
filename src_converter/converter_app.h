#pragma once

#include <wx/app.h>

namespace dlog {

class ConverterApp : public wxApp {
public:
    ConverterApp() = default;
    ~ConverterApp() override = default;

    void OnInitCmdLine(wxCmdLineParser& parser) override;
    bool OnCmdLineParsed(wxCmdLineParser& parser) override;
    bool OnInit() override;
    int OnRun() override;

private:
    bool m_smokeTest = false;
    int m_smokeTestExitCode = 0;
    wxArrayString m_inputFiles;
};

} // namespace dlog
