#pragma once

#include <wx/app.h>

namespace dlog {

class ConverterApp : public wxApp {
public:
    ConverterApp() = default;
    ~ConverterApp() override = default;

    bool OnInit() override;
};

} // namespace dlog
