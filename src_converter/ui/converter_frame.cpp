#include "ui/converter_frame.h"
#include "dlog_reader.h"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/filedlg.h>
#include <wx/dirdlg.h>
#include <wx/msgdlg.h>
#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/utils.h>
#include <wx/progdlg.h>
#include <wx/app.h>
#include <iomanip>
#include <sstream>
#include <cmath>
#include <thread>
#include <atomic>

namespace dlog {

// チャネル名称・単位設定モーダルダイアログ (FUNC-003)
class ChannelConfigDialog : public wxDialog {
public:
    ChannelConfigDialog(wxWindow* parent, const std::vector<ChannelMetadata>& meta, int channelCount)
        : wxDialog(parent, wxID_ANY, wxT("チャネル名称・単位設定"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          m_meta(meta), m_channelCount(channelCount) {

        auto* mainSizer = new wxBoxSizer(wxVERTICAL);
        auto* descText = new wxStaticText(this, wxID_ANY,
            wxT("各チャネルの計測信号名（例: V_bat, 歪みゲージ, 負荷電流）および物理単位（例: V, mV, A, kPa）を設定します。\n設定内容はエクスポートされるCSVの列名（例: V_bat[V]）に反映されます。"));
        descText->Wrap(420);
        mainSizer->Add(descText, 0, wxALL, 10);

        auto* grid = new wxFlexGridSizer(m_channelCount + 1, 3, 6, 10);
        grid->AddGrowableCol(1, 1);

        grid->Add(new wxStaticText(this, wxID_ANY, wxT("チャネル")), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(new wxStaticText(this, wxID_ANY, wxT("信号名称 (Label)")), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(new wxStaticText(this, wxID_ANY, wxT("物理単位 (Unit)")), 0, wxALIGN_CENTER_VERTICAL);

        if (m_meta.size() < static_cast<size_t>(m_channelCount)) {
            m_meta.resize(m_channelCount);
            for (int i = 0; i < m_channelCount; ++i) {
                if (m_meta[i].name.empty()) m_meta[i].name = "CH" + std::to_string(i + 1);
                if (m_meta[i].unit.empty()) m_meta[i].unit = "V";
            }
        }

        m_txtNames.resize(m_channelCount);
        m_txtUnits.resize(m_channelCount);

        for (int i = 0; i < m_channelCount; ++i) {
            grid->Add(new wxStaticText(this, wxID_ANY, wxString::Format(wxT("CH %d:"), i + 1)), 0, wxALIGN_CENTER_VERTICAL);

            m_txtNames[i] = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(m_meta[i].name.c_str()), wxDefaultPosition, wxSize(180, -1));
            grid->Add(m_txtNames[i], 1, wxEXPAND);

            m_txtUnits[i] = new wxTextCtrl(this, wxID_ANY, wxString::FromUTF8(m_meta[i].unit.c_str()), wxDefaultPosition, wxSize(80, -1));
            grid->Add(m_txtUnits[i], 0, wxALIGN_CENTER_VERTICAL);
        }

        mainSizer->Add(grid, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

        auto* btnSizer = CreateButtonSizer(wxOK | wxCANCEL);
        if (btnSizer) {
            mainSizer->Add(btnSizer, 0, wxALIGN_RIGHT | wxALL, 10);
        }

        SetSizerAndFit(mainSizer);
        CenterOnParent();
    }

    std::vector<ChannelMetadata> GetMetadata() const {
        std::vector<ChannelMetadata> result(m_channelCount);
        for (int i = 0; i < m_channelCount; ++i) {
            result[i].name = std::string(m_txtNames[i]->GetValue().utf8_str());
            if (result[i].name.empty()) {
                result[i].name = "CH" + std::to_string(i + 1);
            }
            result[i].unit = std::string(m_txtUnits[i]->GetValue().utf8_str());
            if (result[i].unit.empty()) {
                result[i].unit = "V";
            }
        }
        return result;
    }

private:
    std::vector<ChannelMetadata> m_meta;
    int m_channelCount = 2;
    std::vector<wxTextCtrl*> m_txtNames;
    std::vector<wxTextCtrl*> m_txtUnits;
};

wxBEGIN_EVENT_TABLE(ConverterFrame, wxFrame)
    EVT_BUTTON(ID_ADD_FILES, ConverterFrame::OnAddFiles)
    EVT_BUTTON(ID_ADD_FOLDER, ConverterFrame::OnAddFolder)
    EVT_BUTTON(ID_REMOVE_SEL, ConverterFrame::OnRemoveSelected)
    EVT_BUTTON(ID_CLEAR_LIST, ConverterFrame::OnClearList)
    EVT_BUTTON(ID_START_CONV, ConverterFrame::OnStartConversion)
    EVT_BUTTON(ID_CANCEL_CONV, ConverterFrame::OnCancelConversion)
    EVT_BUTTON(ID_OPEN_OUT_DIR, ConverterFrame::OnOpenOutputDir)
    EVT_BUTTON(ID_BROWSE_OUT_DIR, ConverterFrame::OnBrowseOutputDir)
    EVT_RADIOBUTTON(ID_RADIO_OUT_SAME, ConverterFrame::OnOutputDirRadio)
    EVT_RADIOBUTTON(ID_RADIO_OUT_CUSTOM, ConverterFrame::OnOutputDirRadio)
    EVT_CHECKBOX(ID_CHK_SPLIT, ConverterFrame::OnSplitCheck)
    EVT_BUTTON(ID_BTN_EXCEL_LIMIT, ConverterFrame::OnSetExcelLimit)
    EVT_CHECKBOX(ID_CHK_ALL_CH, ConverterFrame::OnAllChannelsCheck)
    EVT_CHECKBOX(ID_CHK_TIME_RANGE, ConverterFrame::OnTimeRangeCheck)
    EVT_BUTTON(ID_CH_CONFIG, ConverterFrame::OnChannelConfig)
    EVT_BUTTON(ID_RECOVER_PARTIAL, ConverterFrame::OnRecoverPartial)
    EVT_TIMER(ID_CONV_TIMER, ConverterFrame::OnTimer)

    EVT_CLOSE(ConverterFrame::OnClose)
wxEND_EVENT_TABLE()

ConverterFrame::ConverterFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(nullptr, wxID_ANY, title, pos, size),
      m_timer(this, ID_CONV_TIMER) {

    m_worker = std::make_unique<BatchConverterWorker>();

    InitUI();
    SetDropTarget(new ConverterFileDropTarget(this));
}

ConverterFrame::~ConverterFrame() {
    if (m_timer.IsRunning()) {
        m_timer.Stop();
    }
}

void ConverterFrame::InitUI() {
    SetMinSize(wxSize(850, 680));

    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // --- 上部: ファイル一覧リスト ---
    auto* listBox = new wxStaticBox(this, wxID_ANY, wxT(" 変換対象ファイル一覧 "));
    auto* listSizer = new wxStaticBoxSizer(listBox, wxVERTICAL);

    m_listCtrl = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 200),
                                wxLC_REPORT | wxLC_HRULES | wxLC_VRULES);
    m_listCtrl->InsertColumn(0, wxT("状態"), wxLIST_FORMAT_LEFT, 75);
    m_listCtrl->InsertColumn(1, wxT("ファイル名"), wxLIST_FORMAT_LEFT, 200);
    m_listCtrl->InsertColumn(2, wxT("サンプリング"), wxLIST_FORMAT_RIGHT, 95);
    m_listCtrl->InsertColumn(3, wxT("チャネル"), wxLIST_FORMAT_RIGHT, 65);
    m_listCtrl->InsertColumn(4, wxT("記録時間"), wxLIST_FORMAT_RIGHT, 90);
    m_listCtrl->InsertColumn(5, wxT("サイズ"), wxLIST_FORMAT_RIGHT, 80);
    m_listCtrl->InsertColumn(6, wxT("パス"), wxLIST_FORMAT_LEFT, 220);

    listSizer->Add(m_listCtrl, 1, wxEXPAND | wxALL, 5);

    auto* listBtnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_btnAddFiles = new wxButton(this, ID_ADD_FILES, wxT("ファイル追加..."));
    m_btnAddFolder = new wxButton(this, ID_ADD_FOLDER, wxT("フォルダ追加..."));
    m_btnRemove = new wxButton(this, ID_REMOVE_SEL, wxT("選択項目を削除"));
    m_btnClear = new wxButton(this, ID_CLEAR_LIST, wxT("一覧をクリア"));
    m_btnRecoverPartial = new wxButton(this, ID_RECOVER_PARTIAL, wxT("不完全ファイル復旧..."));

    listBtnSizer->Add(m_btnAddFiles, 0, wxRIGHT, 5);
    listBtnSizer->Add(m_btnAddFolder, 0, wxRIGHT, 5);
    listBtnSizer->Add(m_btnRemove, 0, wxRIGHT, 5);
    listBtnSizer->Add(m_btnClear, 0, wxRIGHT, 5);
    listBtnSizer->AddStretchSpacer();
    listBtnSizer->Add(m_btnRecoverPartial, 0, wxRIGHT, 5);


    listSizer->Add(listBtnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
    mainSizer->Add(listSizer, 1, wxEXPAND | wxALL, 6);

    // --- 中部: 出力・変換オプション ---
    auto* optBox = new wxStaticBox(this, wxID_ANY, wxT(" 変換オプション "));
    auto* optSizer = new wxStaticBoxSizer(optBox, wxVERTICAL);

    // 出力先フォルダ
    auto* outDirSizer = new wxBoxSizer(wxHORIZONTAL);
    outDirSizer->Add(new wxStaticText(this, wxID_ANY, wxT("出力先:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    m_radioOutSameDir = new wxRadioButton(this, ID_RADIO_OUT_SAME, wxT("入力ファイルと同じフォルダ"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    m_radioOutCustomDir = new wxRadioButton(this, ID_RADIO_OUT_CUSTOM, wxT("指定フォルダ:"));
    m_txtCustomOutputDir = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(250, -1));
    m_btnBrowseOutputDir = new wxButton(this, ID_BROWSE_OUT_DIR, wxT("参照..."), wxDefaultPosition, wxSize(65, -1));

    m_txtCustomOutputDir->Disable();
    m_btnBrowseOutputDir->Disable();

    outDirSizer->Add(m_radioOutSameDir, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);
    outDirSizer->Add(m_radioOutCustomDir, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    outDirSizer->Add(m_txtCustomOutputDir, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    outDirSizer->Add(m_btnBrowseOutputDir, 0, wxALIGN_CENTER_VERTICAL, 0);
    optSizer->Add(outDirSizer, 0, wxEXPAND | wxALL, 5);

    // 時間列形式 & 間引き & 区切り文字
    auto* formatRowSizer = new wxBoxSizer(wxHORIZONTAL);
    formatRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("時間列形式:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    m_radioTimeRelSec = new wxRadioButton(this, wxID_ANY, wxT("相対時間(秒)"), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    m_radioTimeAbs = new wxRadioButton(this, wxID_ANY, wxT("絶対日時 (UTC)"));
    m_radioTimeIndex = new wxRadioButton(this, wxID_ANY, wxT("サンプル番号"));
    m_radioTimeNone = new wxRadioButton(this, wxID_ANY, wxT("なし"));

    formatRowSizer->Add(m_radioTimeRelSec, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    formatRowSizer->Add(m_radioTimeAbs, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    formatRowSizer->Add(m_radioTimeIndex, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    formatRowSizer->Add(m_radioTimeNone, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    formatRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("間引き:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    wxArrayString decChoices;
    decChoices.Add(wxT("1/1 (全サンプル)"));
    decChoices.Add(wxT("1/2 (2-to-1)"));
    decChoices.Add(wxT("1/4 (4-to-1)"));
    decChoices.Add(wxT("1/5 (5-to-1)"));
    decChoices.Add(wxT("1/10 (10-to-1)"));
    decChoices.Add(wxT("1/20 (20-to-1)"));
    decChoices.Add(wxT("1/50 (50-to-1)"));
    decChoices.Add(wxT("1/100 (100-to-1)"));
    m_choiceDecimation = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(120, -1), decChoices);
    m_choiceDecimation->SetSelection(0);
    formatRowSizer->Add(m_choiceDecimation, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    formatRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("区切り:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    wxArrayString delimChoices;
    delimChoices.Add(wxT("カンマ (CSV)"));
    delimChoices.Add(wxT("タブ (TSV)"));
    m_choiceDelimiter = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(100, -1), delimChoices);
    m_choiceDelimiter->SetSelection(0);
    formatRowSizer->Add(m_choiceDelimiter, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    formatRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("数値精度:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    wxArrayString precChoices;
    precChoices.Add(wxT("4 桁"));
    precChoices.Add(wxT("6 桁 (推奨)"));
    precChoices.Add(wxT("8 桁"));
    m_choicePrecision = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxSize(90, -1), precChoices);
    m_choicePrecision->SetSelection(1); // 6桁
    formatRowSizer->Add(m_choicePrecision, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    m_chkApplyVoltageScale = new wxCheckBox(this, wxID_ANY, wxT("電圧値[V]に換算して出力"));
    m_chkApplyVoltageScale->SetValue(true);
    m_chkApplyVoltageScale->SetToolTip(wxT("ファイルヘッダに記録されたフルスケール電圧を適用して、デジタル値(-1.0〜+1.0)を実電圧[V]に換算します"));
    formatRowSizer->Add(m_chkApplyVoltageScale, 0, wxALIGN_CENTER_VERTICAL, 0);

    optSizer->Add(formatRowSizer, 0, wxEXPAND | wxALL, 5);

    // ★ 行数自動分割（ユーザー指定行数） ★
    auto* splitRowSizer = new wxBoxSizer(wxHORIZONTAL);
    m_chkSplitLines = new wxCheckBox(this, ID_CHK_SPLIT, wxT("指定行数でファイルを自動分割"));
    m_chkSplitLines->SetValue(true);
    splitRowSizer->Add(m_chkSplitLines, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    splitRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("1ファイルあたりの最大行数:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    // デフォルト 1,000,000 行 (1000〜100,000,000)
    m_spinMaxLines = new wxSpinCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(120, -1),
                                    wxSP_ARROW_KEYS, 1000, 100000000, 1000000);
    splitRowSizer->Add(m_spinMaxLines, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    m_btnExcelLimit = new wxButton(this, ID_BTN_EXCEL_LIMIT, wxT("Excel上限値に設定"),
                                   wxDefaultPosition, wxDefaultSize);
    splitRowSizer->Add(m_btnExcelLimit, 0, wxALIGN_CENTER_VERTICAL, 0);

    optSizer->Add(splitRowSizer, 0, wxEXPAND | wxALL, 5);

    // 出力チャネル個別選択 (MED-02)
    auto* channelRowSizer = new wxBoxSizer(wxHORIZONTAL);
    channelRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("出力チャネル:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
    m_chkAllChannels = new wxCheckBox(this, ID_CHK_ALL_CH, wxT("全チャネル"));
    m_chkAllChannels->SetValue(true);
    channelRowSizer->Add(m_chkAllChannels, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_chkChannels.resize(8);
    for (int ch = 0; ch < 8; ++ch) {
        m_chkChannels[ch] = new wxCheckBox(this, wxID_ANY, wxString::Format(wxT("CH%d"), ch + 1));
        m_chkChannels[ch]->SetValue(true);
        m_chkChannels[ch]->Disable(); // 全チャネル選択時は個別チェックボックス無効
        channelRowSizer->Add(m_chkChannels[ch], 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    }
    m_btnChannelConfig = new wxButton(this, ID_CH_CONFIG, wxT("チャネル名・単位設定..."));
    channelRowSizer->Add(m_btnChannelConfig, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 10);
    optSizer->Add(channelRowSizer, 0, wxEXPAND | wxALL, 5);

    // 時間範囲トリミング (MED-03)
    auto* timeRangeRowSizer = new wxBoxSizer(wxHORIZONTAL);
    m_chkTimeRange = new wxCheckBox(this, ID_CHK_TIME_RANGE, wxT("時間範囲でトリミング"));
    m_chkTimeRange->SetValue(false);
    timeRangeRowSizer->Add(m_chkTimeRange, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    timeRangeRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("開始時間[秒]:")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_txtStartTime = new wxTextCtrl(this, wxID_ANY, wxT("0.0"), wxDefaultPosition, wxSize(70, -1));
    m_txtStartTime->Disable();
    timeRangeRowSizer->Add(m_txtStartTime, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    timeRangeRowSizer->Add(new wxStaticText(this, wxID_ANY, wxT("終了時間[秒] (0=末尾まで):")), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_txtEndTime = new wxTextCtrl(this, wxID_ANY, wxT("0.0"), wxDefaultPosition, wxSize(70, -1));
    m_txtEndTime->Disable();
    timeRangeRowSizer->Add(m_txtEndTime, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    optSizer->Add(timeRangeRowSizer, 0, wxEXPAND | wxALL, 5);
    mainSizer->Add(optSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // --- 下部: 進捗表示 & アクションボタン ---
    auto* progBox = new wxStaticBox(this, wxID_ANY, wxT(" 進捗・実行 "));
    auto* progSizer = new wxStaticBoxSizer(progBox, wxVERTICAL);

    auto* gaugeGrid = new wxFlexGridSizer(2, 2, 4, 8);
    gaugeGrid->AddGrowableCol(1, 1);

    m_lblCurrentFile = new wxStaticText(this, wxID_ANY, wxT("ファイル進捗 (0%):"));
    m_gaugeCurrent = new wxGauge(this, wxID_ANY, 1000, wxDefaultPosition, wxSize(-1, 18));
    gaugeGrid->Add(m_lblCurrentFile, 0, wxALIGN_CENTER_VERTICAL);
    gaugeGrid->Add(m_gaugeCurrent, 1, wxEXPAND);

    m_lblOverallStatus = new wxStaticText(this, wxID_ANY, wxT("全体進捗 (0/0):"));
    m_gaugeOverall = new wxGauge(this, wxID_ANY, 1000, wxDefaultPosition, wxSize(-1, 18));
    gaugeGrid->Add(m_lblOverallStatus, 0, wxALIGN_CENTER_VERTICAL);
    gaugeGrid->Add(m_gaugeOverall, 1, wxEXPAND);

    progSizer->Add(gaugeGrid, 0, wxEXPAND | wxALL, 5);

    auto* actionRow = new wxBoxSizer(wxHORIZONTAL);
    m_lblSpeedAndEta = new wxStaticText(this, wxID_ANY, wxT("待機中"));
    m_lblSpeedAndEta->SetForegroundColour(wxColour(100, 100, 100));
    actionRow->Add(m_lblSpeedAndEta, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_btnOpenOutputDir = new wxButton(this, ID_OPEN_OUT_DIR, wxT("出力フォルダを開く"));
    actionRow->Add(m_btnOpenOutputDir, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    m_btnStart = new wxButton(this, ID_START_CONV, wxT("変換開始"), wxDefaultPosition, wxSize(130, 32));
    m_btnStart->SetBackgroundColour(wxColour(30, 160, 60));
    m_btnStart->SetForegroundColour(*wxWHITE);
    wxFont btnFont = m_btnStart->GetFont();
    btnFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_btnStart->SetFont(btnFont);
    actionRow->Add(m_btnStart, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_btnCancel = new wxButton(this, ID_CANCEL_CONV, wxT("中断"), wxDefaultPosition, wxSize(80, 32));
    m_btnCancel->Disable();
    actionRow->Add(m_btnCancel, 0, wxALIGN_CENTER_VERTICAL, 0);

    progSizer->Add(actionRow, 0, wxEXPAND | wxALL, 5);
    mainSizer->Add(progSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(mainSizer);
    Layout();
}

void ConverterFrame::AddFiles(const wxArrayString& filePaths) {
    for (const auto& path : filePaths) {
        std::string stdPath = std::string(path.utf8_str());

        // 重複チェック
        bool alreadyAdded = false;
        for (const auto& existing : m_items) {
            if (existing.inputFilePath == stdPath) {
                alreadyAdded = true;
                break;
            }
        }
        if (alreadyAdded) continue;

        BatchItem item;
        item.id = static_cast<int>(m_items.size() + 1);
        if (DlogReader::InspectFile(stdPath, item)) {
            if (m_channelMetadata.empty() && !item.channelMetadata.empty()) {
                m_channelMetadata = item.channelMetadata;
                for (size_t ch = 0; ch < m_chkChannels.size() && ch < m_channelMetadata.size(); ++ch) {
                    m_chkChannels[ch]->SetLabel(wxString::Format(wxT("%s [%s]"),
                        wxString::FromUTF8(m_channelMetadata[ch].name.c_str()),
                        wxString::FromUTF8(m_channelMetadata[ch].unit.c_str())));
                }
            }
            m_items.push_back(item);
        } else {
            // ヘッダ不正でもエラー表示用に追加
            item.inputFilePath = stdPath;
            item.status = BatchStatus::Error;
            m_items.push_back(item);
        }
    }
    UpdateListDisplay();
}

void ConverterFrame::UpdateListDisplay() {
    m_listCtrl->DeleteAllItems();

    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto& it = m_items[i];

        wxString statusStr;
        switch (it.status) {
            case BatchStatus::Pending:    statusStr = wxT("待機中"); break;
            case BatchStatus::Converting: statusStr = wxString::Format(wxT("変換中 (%.0f%%)"), it.progress * 100.0); break;
            case BatchStatus::Completed:  statusStr = wxString::Format(wxT("完了 (%zu個)"), it.outputFiles.size()); break;
            case BatchStatus::Canceled:   statusStr = wxT("中断"); break;
            case BatchStatus::Error:
                statusStr = it.errorMessage.empty()
                    ? wxT("エラー")
                    : wxString::Format(wxT("エラー: %s"), wxString::FromUTF8(it.errorMessage.c_str()));
                break;
        }

        long index = m_listCtrl->InsertItem(static_cast<long>(i), statusStr);

        m_listCtrl->SetItem(index, 1, wxString::FromUTF8(it.fileNameOnly.c_str()));

        if (it.isValidHeader) {
            m_listCtrl->SetItem(index, 2, wxString::Format(wxT("%.6g Hz"), GetDLogEffectiveSampleRate(it.header)));
            m_listCtrl->SetItem(index, 3, wxString::Format(wxT("%u ch"), it.header.channels));

            int totalSec = static_cast<int>(it.durationSec);
            int h = totalSec / 3600;
            int m = (totalSec % 3600) / 60;
            int s = totalSec % 60;
            m_listCtrl->SetItem(index, 4, wxString::Format(wxT("%02d:%02d:%02d"), h, m, s));

            double mb = static_cast<double>(it.fileSizeBytes) / (1024.0 * 1024.0);
            if (mb >= 1024.0) {
                m_listCtrl->SetItem(index, 5, wxString::Format(wxT("%.2f GB"), mb / 1024.0));
            } else {
                m_listCtrl->SetItem(index, 5, wxString::Format(wxT("%.1f MB"), mb));
            }
        } else {
            m_listCtrl->SetItem(index, 2, wxT("-"));
            m_listCtrl->SetItem(index, 3, wxT("-"));
            m_listCtrl->SetItem(index, 4, wxT("-"));
            m_listCtrl->SetItem(index, 5, wxT("-"));
        }

        m_listCtrl->SetItem(index, 6, wxString::FromUTF8(it.inputFilePath.c_str()));
    }
}

void ConverterFrame::OnAddFiles(wxCommandEvent& /*event*/) {
    wxFileDialog openFileDialog(this, wxT("変換する DLOG ファイルを選択（複数選択可）"),
                                wxEmptyString, wxEmptyString,
                                wxT("DLOGファイル (*.bin;*.dat)|*.bin;*.dat|すべてのファイル (*.*)|*.*"),
                                wxFD_OPEN | wxFD_FILE_MUST_EXIST | wxFD_MULTIPLE);

    if (openFileDialog.ShowModal() == wxID_CANCEL) return;

    wxArrayString paths;
    openFileDialog.GetPaths(paths);
    AddFiles(paths);
}

void ConverterFrame::OnAddFolder(wxCommandEvent& /*event*/) {
    wxDirDialog dirDialog(this, wxT("DLOGファイルが含まれるフォルダを選択"), wxEmptyString,
                          wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

    if (dirDialog.ShowModal() == wxID_CANCEL) return;

    wxArrayString files;
    wxDir::GetAllFiles(dirDialog.GetPath(), &files, wxT("*.bin"), wxDIR_FILES);
    wxArrayString datFiles;
    wxDir::GetAllFiles(dirDialog.GetPath(), &datFiles, wxT("*.dat"), wxDIR_FILES);
    for (const auto& d : datFiles) files.Add(d);

    if (files.empty()) {
        wxMessageBox(wxT("指定フォルダ内に .bin または .dat ファイルが見つかりませんでした。"),
                     wxT("情報"), wxOK | wxICON_INFORMATION, this);
        return;
    }
    AddFiles(files);
}

void ConverterFrame::OnRemoveSelected(wxCommandEvent& /*event*/) {
    long item = m_listCtrl->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    std::vector<long> toRemove;
    while (item != -1) {
        toRemove.push_back(item);
        item = m_listCtrl->GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    }

    for (int i = static_cast<int>(toRemove.size()) - 1; i >= 0; --i) {
        size_t idx = static_cast<size_t>(toRemove[i]);
        if (idx < m_items.size()) {
            m_items.erase(m_items.begin() + idx);
        }
    }
    UpdateListDisplay();
}

void ConverterFrame::OnClearList(wxCommandEvent& /*event*/) {
    m_items.clear();
    m_channelMetadata.clear();
    m_hasCustomChannelMetadata = false;
    for (size_t ch = 0; ch < m_chkChannels.size(); ++ch) {
        m_chkChannels[ch]->SetLabel(wxString::Format(wxT("CH%d"), static_cast<int>(ch + 1)));
    }
    UpdateListDisplay();
}

void ConverterFrame::OnOutputDirRadio(wxCommandEvent& /*event*/) {
    bool isCustom = m_radioOutCustomDir->GetValue();
    m_txtCustomOutputDir->Enable(isCustom);
    m_btnBrowseOutputDir->Enable(isCustom);
}

void ConverterFrame::OnBrowseOutputDir(wxCommandEvent& /*event*/) {
    wxDirDialog dirDialog(this, wxT("出力先フォルダを選択"), wxEmptyString, wxDD_DEFAULT_STYLE);
    if (dirDialog.ShowModal() == wxID_CANCEL) return;
    m_txtCustomOutputDir->SetValue(dirDialog.GetPath());
}

void ConverterFrame::OnSplitCheck(wxCommandEvent& /*event*/) {
    bool checked = m_chkSplitLines->GetValue();
    m_spinMaxLines->Enable(checked);
    m_btnExcelLimit->Enable(checked);
}

void ConverterFrame::OnSetExcelLimit(wxCommandEvent& /*event*/) {
    // 1行目のヘッダを含めてExcel上限1,048,576行に収める。
    m_spinMaxLines->SetValue(1048575);
}

void ConverterFrame::OnAllChannelsCheck(wxCommandEvent& /*event*/) {
    bool all = m_chkAllChannels->GetValue();
    for (int ch = 0; ch < 8; ++ch) {
        m_chkChannels[ch]->Enable(!all);
        if (all) {
            m_chkChannels[ch]->SetValue(true);
        }
    }
}

void ConverterFrame::OnTimeRangeCheck(wxCommandEvent& /*event*/) {
    bool range = m_chkTimeRange->GetValue();
    m_txtStartTime->Enable(range);
    m_txtEndTime->Enable(range);
}

void ConverterFrame::OnChannelConfig(wxCommandEvent& /*event*/) {
    int maxChannels = 2;
    for (const auto& item : m_items) {
        if (item.isValidHeader && static_cast<int>(item.header.channels) > maxChannels) {
            maxChannels = static_cast<int>(item.header.channels);
        }
    }
    maxChannels = std::min(maxChannels, 8);

    if (m_channelMetadata.empty()) {
        m_channelMetadata.resize(maxChannels);
        for (int i = 0; i < maxChannels; ++i) {
            m_channelMetadata[i].name = "CH" + std::to_string(i + 1);
            m_channelMetadata[i].unit = "V";
        }
    }

    ChannelConfigDialog dlg(this, m_channelMetadata, maxChannels);
    if (dlg.ShowModal() == wxID_OK) {
        m_channelMetadata = dlg.GetMetadata();
        m_hasCustomChannelMetadata = true;
        for (size_t ch = 0; ch < m_chkChannels.size() && ch < m_channelMetadata.size(); ++ch) {
            m_chkChannels[ch]->SetLabel(wxString::Format(wxT("%s [%s]"),
                wxString::FromUTF8(m_channelMetadata[ch].name.c_str()),
                wxString::FromUTF8(m_channelMetadata[ch].unit.c_str())));
        }
    }
}

ConversionOptions ConverterFrame::GatherOptions() {
    ConversionOptions opt;

    if (m_radioTimeRelSec->GetValue()) opt.timeFormat = TimeFormat::RelativeSec;
    else if (m_radioTimeAbs->GetValue()) opt.timeFormat = TimeFormat::AbsoluteTime;
    else if (m_radioTimeIndex->GetValue()) opt.timeFormat = TimeFormat::SampleIndex;
    else opt.timeFormat = TimeFormat::None;

    int decSel = m_choiceDecimation->GetSelection();
    int decSteps[] = {1, 2, 4, 5, 10, 20, 50, 100};
    opt.decimationStep = (decSel >= 0 && decSel < 8) ? decSteps[decSel] : 1;

    opt.delimiter = (m_choiceDelimiter->GetSelection() == 1) ? '\t' : ',';

    int precSel = m_choicePrecision->GetSelection();
    int precs[] = {4, 6, 8};
    opt.floatPrecision = (precSel >= 0 && precSel < 3) ? precs[precSel] : 6;

    if (m_chkApplyVoltageScale) {
        opt.applyVoltageScale = m_chkApplyVoltageScale->GetValue();
    }

    // 行数自動分割
    opt.enableSplitLines = m_chkSplitLines->GetValue();
    opt.maxLinesPerFile = static_cast<uint64_t>(m_spinMaxLines->GetValue());

    // 出力チャネル選択 (MED-02)
    if (!m_chkAllChannels->GetValue()) {
        opt.selectedChannels.clear();
        for (int ch = 0; ch < 8; ++ch) {
            if (m_chkChannels[ch]->GetValue()) {
                opt.selectedChannels.push_back(ch);
            }
        }
        if (opt.selectedChannels.empty()) {
            for (int ch = 0; ch < 8; ++ch) {
                opt.selectedChannels.push_back(ch);
            }
        }
    } else {
        opt.selectedChannels.clear();
    }

    // 時間範囲トリミング (MED-03)
    opt.enableTimeRange = m_chkTimeRange->GetValue();
    if (opt.enableTimeRange) {
        double startSec = 0.0;
        double endSec = 0.0;
        m_txtStartTime->GetValue().ToDouble(&startSec);
        m_txtEndTime->GetValue().ToDouble(&endSec);
        opt.startTimeSec = std::max(0.0, startSec);
        opt.endTimeSec = std::max(0.0, endSec);
    }

    // 出力先
    opt.useSameDirAsInput = m_radioOutSameDir->GetValue();
    opt.customOutputDir = std::string(m_txtCustomOutputDir->GetValue().utf8_str());

    // チャネル名称・物理単位設定 (FUNC-003)
    if (m_hasCustomChannelMetadata) {
        opt.channelMetadata = m_channelMetadata;
    }

    return opt;
}

void ConverterFrame::SetControlsEnabled(bool enabled) {
    m_btnAddFiles->Enable(enabled);
    m_btnAddFolder->Enable(enabled);
    m_btnRemove->Enable(enabled);
    m_btnClear->Enable(enabled);
    if (m_btnRecoverPartial) m_btnRecoverPartial->Enable(enabled);


    m_radioOutSameDir->Enable(enabled);
    m_radioOutCustomDir->Enable(enabled);
    if (enabled && m_radioOutCustomDir->GetValue()) {
        m_txtCustomOutputDir->Enable(true);
        m_btnBrowseOutputDir->Enable(true);
    } else {
        m_txtCustomOutputDir->Enable(false);
        m_btnBrowseOutputDir->Enable(false);
    }

    m_radioTimeRelSec->Enable(enabled);
    m_radioTimeAbs->Enable(enabled);
    m_radioTimeIndex->Enable(enabled);
    m_radioTimeNone->Enable(enabled);
    m_choiceDecimation->Enable(enabled);
    m_choiceDelimiter->Enable(enabled);
    m_choicePrecision->Enable(enabled);
    if (m_chkApplyVoltageScale) m_chkApplyVoltageScale->Enable(enabled);
    m_chkSplitLines->Enable(enabled);
    if (enabled && m_chkSplitLines->GetValue()) {
        m_spinMaxLines->Enable(true);
        m_btnExcelLimit->Enable(true);
    } else {
        m_spinMaxLines->Enable(false);
        m_btnExcelLimit->Enable(false);
    }

    // 出力チャネル選択コントロール (MED-02, FUNC-003)
    m_chkAllChannels->Enable(enabled);
    bool all = m_chkAllChannels->GetValue();
    for (int ch = 0; ch < 8; ++ch) {
        m_chkChannels[ch]->Enable(enabled && !all);
    }
    if (m_btnChannelConfig) m_btnChannelConfig->Enable(enabled);

    // 時間範囲トリミングコントロール (MED-03)
    m_chkTimeRange->Enable(enabled);
    bool range = m_chkTimeRange->GetValue();
    m_txtStartTime->Enable(enabled && range);
    m_txtEndTime->Enable(enabled && range);

    m_btnStart->Enable(enabled);
    m_btnCancel->Enable(!enabled);
}

void ConverterFrame::OnStartConversion(wxCommandEvent& /*event*/) {
    if (m_items.empty()) {
        wxMessageBox(wxT("変換するファイルを追加してください。"), wxT("情報"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (m_chkTimeRange->GetValue()) {
        double startSec = 0.0;
        double endSec = 0.0;
        if (!m_txtStartTime->GetValue().ToDouble(&startSec) ||
            !m_txtEndTime->GetValue().ToDouble(&endSec) ||
            !std::isfinite(startSec) || !std::isfinite(endSec) ||
            startSec < 0.0 || endSec < 0.0) {
            wxMessageBox(wxT("開始時間と終了時間には0以上の数値を入力してください。"),
                         wxT("警告"), wxOK | wxICON_WARNING, this);
            return;
        }
    }

    ConversionOptions options = GatherOptions();
    if (options.enableTimeRange && options.endTimeSec > 0.0 && options.startTimeSec >= options.endTimeSec) {
        wxMessageBox(wxT("時間範囲の指定が不正です。開始時間は終了時間より小さく設定してください。"),
                     wxT("警告"), wxOK | wxICON_WARNING, this);
        return;
    }
    if (!options.useSameDirAsInput && options.customOutputDir.empty()) {
        wxMessageBox(wxT("指定フォルダのパスを入力してください。"), wxT("警告"), wxOK | wxICON_WARNING, this);
        return;
    }

    std::string collisionErr;
    if (!BatchConverterWorker::CheckCollisions(m_items, options, collisionErr)) {
        wxMessageBox(wxString::FromUTF8(collisionErr.c_str()), wxT("出力ファイル名の衝突"), wxOK | wxICON_WARNING, this);
        return;
    }

    SetControlsEnabled(false);
    m_isConverting = true;

    m_gaugeCurrent->SetValue(0);
    m_gaugeOverall->SetValue(0);
    m_lblSpeedAndEta->SetLabel(wxT("変換を開始中..."));

    if (!m_worker->Start(m_items, options)) {
        m_isConverting = false;
        SetControlsEnabled(true);
        wxMessageBox(wxT("変換ワーカースレッドを開始できませんでした。"),
                     wxT("エラー"), wxOK | wxICON_ERROR, this);
        return;
    }
    m_timer.Start(50); // 変換実行中のみタイマーを起動
}

void ConverterFrame::OnCancelConversion(wxCommandEvent& /*event*/) {
    if (m_worker->IsRunning()) {
        m_lblSpeedAndEta->SetLabel(wxT("中断中..."));
        m_worker->Cancel();
    }
}

void ConverterFrame::OnOpenOutputDir(wxCommandEvent& /*event*/) {
    wxString dir;
    if (m_radioOutCustomDir->GetValue() && !m_txtCustomOutputDir->GetValue().IsEmpty()) {
        dir = m_txtCustomOutputDir->GetValue();
    } else if (!m_items.empty()) {
        wxFileName fn(wxString::FromUTF8(m_items[0].inputFilePath.c_str()));
        dir = fn.GetPath();
    }

    if (!dir.IsEmpty() && wxDirExists(dir)) {
#ifdef _WIN32
        wxExecute(wxString::Format(wxT("explorer.exe \"%s\""), dir));
#endif
    } else {
        wxMessageBox(wxT("出力先フォルダが見つかりません。"), wxT("エラー"), wxOK | wxICON_WARNING, this);
    }
}

void ConverterFrame::OnTimer(wxTimerEvent& /*event*/) {
    if (!m_isConverting) {
        return;
    }

    if (m_worker->IsRunning()) {
        BatchStats stats = m_worker->GetStats();

        // ゲージ更新
        m_gaugeCurrent->SetValue(static_cast<int>(stats.currentFileProgress * 1000.0));
        m_gaugeOverall->SetValue(static_cast<int>(stats.overallProgress * 1000.0));

        // ラベル更新
        m_lblCurrentFile->SetLabel(wxString::Format(wxT("ファイル進捗 (%d%%):"),
                                                    static_cast<int>(stats.currentFileProgress * 100.0)));
        m_lblOverallStatus->SetLabel(wxString::Format(wxT("全体進捗 (%zu/%zu - %d%%):"),
                                                      stats.completedFiles, stats.totalFiles,
                                                      static_cast<int>(stats.overallProgress * 100.0)));

        double speedMB = stats.bytesPerSec / (1024.0 * 1024.0);
        int etaSec = static_cast<int>(stats.estimatedRemainingSec);
        int etaM = etaSec / 60;
        int etaS = etaSec % 60;

        m_lblSpeedAndEta->SetLabel(wxString::Format(wxT("速度: %.1f MB/s | 残り推定時間: %02d:%02d"),
                                                    speedMB, etaM, etaS));

        // リストの状態を更新
        auto updatedItems = m_worker->GetItems();
        if (updatedItems.size() == m_items.size()) {
            m_items = updatedItems;
            for (size_t i = 0; i < m_items.size(); ++i) {
                wxString stStr;
                switch (m_items[i].status) {
                    case BatchStatus::Pending:    stStr = wxT("待機中"); break;
                    case BatchStatus::Converting: stStr = wxT("● 変換中"); break;
                    case BatchStatus::Completed:  stStr = wxString::Format(wxT("完了 (%zu個)"), m_items[i].outputFiles.size()); break;
                    case BatchStatus::Canceled:   stStr = wxT("中断"); break;
                    case BatchStatus::Error:
                        stStr = m_items[i].errorMessage.empty()
                            ? wxT("エラー")
                            : wxString::Format(wxT("エラー: %s"), wxString::FromUTF8(m_items[i].errorMessage.c_str()));
                        break;
                }
                m_listCtrl->SetItem(static_cast<long>(i), 0, stStr);
            }
        }
    } else {
        // 変換終了時（完了、中断、またはエラー停止）
        // 最優先でフラグをクリアしタイマーを停止して再入を確実に防止
        m_isConverting = false;
        m_timer.Stop();

        m_items = m_worker->GetItems();
        UpdateListDisplay();

        double progressSum = 0.0;
        for (const auto& item : m_items) {
            progressSum += std::max(0.0, std::min(1.0, item.progress));
        }
        const double finalOverall = m_items.empty() ? 0.0 :
            progressSum / static_cast<double>(m_items.size());
        const double finalCurrent = m_items.empty() ? 0.0 :
            std::max(0.0, std::min(1.0, m_items.back().progress));
        m_gaugeCurrent->SetValue(static_cast<int>(finalCurrent * 1000.0));
        m_gaugeOverall->SetValue(static_cast<int>(finalOverall * 1000.0));

        SetControlsEnabled(true);

        // バッチ処理結果の集計
        size_t completedCount = 0;
        size_t errorCount = 0;
        size_t canceledCount = 0;
        for (const auto& item : m_items) {
            if (item.status == BatchStatus::Completed) {
                completedCount++;
            } else if (item.status == BatchStatus::Error) {
                errorCount++;
            } else if (item.status == BatchStatus::Canceled) {
                canceledCount++;
            }
        }

        bool wasCanceled = (canceledCount > 0) || (m_worker->IsCanceled());
        wxString msg;
        wxString title;
        long iconStyle = wxOK;

        if (wasCanceled) {
            title = wxT("処理中断");
            iconStyle |= wxICON_INFORMATION;
            msg = wxString::Format(wxT("変換処理を中断しました。\n\n変換完了: %zu 件\n未完了: %zu 件"),
                                   completedCount, m_items.size() - completedCount);
            m_lblSpeedAndEta->SetLabel(wxT("変換中断"));
        } else if (errorCount > 0) {
            title = wxT("変換完了（エラーあり）");
            iconStyle |= wxICON_WARNING;
            msg = wxString::Format(wxT("変換処理が終了しましたが、エラーが発生したファイルがあります。\n\n成功: %zu 件\nエラー: %zu 件"),
                                   completedCount, errorCount);
            m_lblSpeedAndEta->SetLabel(wxT("エラーありで完了"));
        } else {
            title = wxT("変換完了");
            iconStyle |= wxICON_INFORMATION;
            msg = wxString::Format(wxT("変換が正常に完了しました。\n\n成功: %zu 件 / 全 %zu 件"),
                                   completedCount, m_items.size());
            m_lblSpeedAndEta->SetLabel(wxT("全変換完了"));
        }

        // wxCallAfter で安全にダイアログを表示（タイマー処理を抜けた後）
        CallAfter([this, msg, title, iconStyle]() {
            wxMessageBox(msg, title, iconStyle, this);
        });
    }
}

void ConverterFrame::OnClose(wxCloseEvent& event) {
    if (m_worker->IsRunning()) {
        int res = wxMessageBox(wxT("変換が実行中です。中断して終了しますか？"),
                               wxT("確認"), wxYES_NO | wxICON_QUESTION, this);
        if (res != wxYES) {
            event.Veto();
            return;
        }
        m_worker->Cancel();
    }
    if (m_timer.IsRunning()) {
        m_timer.Stop();
    }
    Destroy();
}

void ConverterFrame::OnRecoverPartial(wxCommandEvent& /*event*/) {
    wxFileDialog openDlg(this, wxT("復旧する .partial ファイルを選択"), wxEmptyString, wxEmptyString,
                         wxT("DLOG一時ファイル (*.partial;*.partial.*)|*.partial;*.partial.*|すべてのファイル (*.*)|*.*"),
                         wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openDlg.ShowModal() == wxID_CANCEL) return;

    wxString inputPath = openDlg.GetPath();
    std::string recoveredPath;
    std::string errMsg;

    std::atomic<bool> cancelled{false};
    std::atomic<int> progressPermille{0};
    std::atomic<bool> done{false};
    bool success = false;

    wxProgressDialog progressDlg(
        wxT("ファイル復旧中"),
        wxT("不完全な一時ファイルから有効データを復旧しています..."),
        1000,
        this,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME
    );

    std::thread workerThread([&]() {
        success = DlogReader::RecoverPartialFile(
            std::string(inputPath.utf8_str()),
            recoveredPath,
            errMsg,
            [&](double ratio) -> bool {
                if (cancelled.load()) {
                    return false;
                }
                progressPermille.store(static_cast<int>(ratio * 1000.0));
                return true;
            }
        );
        done.store(true);
    });

    while (!done.load()) {
        wxMilliSleep(50);
        wxTheApp->Yield();
        int permille = progressPermille.load();
        if (!progressDlg.Update(std::min(permille, 999))) {
            cancelled.store(true);
        }
    }

    if (workerThread.joinable()) {
        workerThread.join();
    }

    if (cancelled.load()) {
        wxMessageBox(wxT("ファイルの復旧処理がキャンセルされました。"),
                     wxT("キャンセル"), wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (!success) {
        wxMessageBox(wxString::Format(wxT("不完全ファイルの復旧に失敗しました:\n%s"),
                                      wxString::FromUTF8(errMsg.c_str())),
                     wxT("復旧エラー"), wxOK | wxICON_ERROR, this);
        return;
    }

    wxArrayString recoveredList;
    recoveredList.Add(wxString::FromUTF8(recoveredPath.c_str()));
    AddFiles(recoveredList);

    wxMessageBox(wxString::Format(wxT("不完全ファイルを正常に復旧し、変換一覧に追加しました:\n%s\n\n※元の .partial ファイルは安全に保持されています。"),
                                  wxString::FromUTF8(recoveredPath.c_str())),
                 wxT("復旧完了"), wxOK | wxICON_INFORMATION, this);
}

} // namespace dlog

