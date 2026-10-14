#include "converter_app.h"
#include "ui/converter_frame.h"

namespace dlog {

bool ConverterApp::OnInit() {
    if (!wxApp::OnInit()) {
        return false;
    }

    auto* frame = new ConverterFrame(
        wxT("DLOG to CSV 一括変換ツール (DLOG Batch Converter)"),
        wxDefaultPosition,
        wxSize(920, 720)
    );
    frame->Centre();

    // コマンドライン引数でファイルが指定されている場合は追加
    if (argc > 1) {
        wxArrayString files;
        for (int i = 1; i < argc; ++i) {
            files.Add(argv[i]);
        }
        frame->AddFiles(files);
    }

    frame->Show(true);
    return true;
}

} // namespace dlog
