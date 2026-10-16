#include "ui/main_frame.h"
#include "ui/calibration_dialog.h"
#include "common/localized_strings.h"
#include "common/ui_language.h"
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/textdlg.h>
#include <wx/statbox.h>
#include <wx/datetime.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/menu.h>
#include <wx/progdlg.h>
#include <wx/app.h>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace dlog {

// チャネル名称・単位設定モーダルダイアログ (FUNC-003)
class ChannelConfigDialog : public wxDialog {
public:
    ChannelConfigDialog(wxWindow* parent, const std::vector<ChannelMetadata>& meta, int channelCount)
        : wxDialog(parent, wxID_ANY, TrWx(StringId::ChannelConfigTitle), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
          m_meta(meta), m_channelCount(channelCount) {

        auto* mainSizer = new wxBoxSizer(wxVERTICAL);
        auto* descText = new wxStaticText(this, wxID_ANY, TrWx(StringId::ChannelConfigDesc));
        descText->Wrap(420);
        mainSizer->Add(descText, 0, wxALL, 10);

        auto* grid = new wxFlexGridSizer(m_channelCount + 1, 3, 6, 10);
        grid->AddGrowableCol(1, 1);

        grid->Add(new wxStaticText(this, wxID_ANY, TrWx(StringId::ChannelColChannel)), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(new wxStaticText(this, wxID_ANY, TrWx(StringId::ChannelColLabel)), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(new wxStaticText(this, wxID_ANY, TrWx(StringId::ChannelColUnit)), 0, wxALIGN_CENTER_VERTICAL);

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

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_MENU(ID_LANG_JA, MainFrame::OnLanguageSelectJa)
    EVT_MENU(ID_LANG_EN, MainFrame::OnLanguageSelectEn)
    EVT_NOTEBOOK_PAGE_CHANGING(ID_MODE_NOTEBOOK, MainFrame::OnModeChanging)
    EVT_NOTEBOOK_PAGE_CHANGED(ID_MODE_NOTEBOOK, MainFrame::OnModeChanged)
    EVT_CHOICE(ID_DEVICE_CHOICE, MainFrame::OnDeviceSelect)
    EVT_BUTTON(ID_REFRESH_DEVICES, MainFrame::OnRefreshDevices)
    EVT_CHOICE(ID_CHANNEL_CHOICE, MainFrame::OnChannelSelect)
    EVT_BUTTON(ID_CH_CONFIG_BTN, MainFrame::OnChannelConfig)
    EVT_CHOICE(ID_RATE_CHOICE, MainFrame::OnRateSelect)
    EVT_CHOICE(ID_DECIMATION_CHOICE, MainFrame::OnDecimationSelect)
    EVT_BUTTON(ID_AC_GUIDE_BTN, MainFrame::OnAcGuide)
    EVT_TEXT(ID_VOLTAGE_TXT, MainFrame::OnVoltageChange)
    EVT_BUTTON(ID_CALIBRATE_BTN, MainFrame::OnCalibrate)
    EVT_CHOICE(ID_TIME_WINDOW_CHOICE, MainFrame::OnTimeWindowSelect)
    EVT_CHOICE(ID_GAIN_CHOICE, MainFrame::OnGainSelect)
    EVT_BUTTON(ID_HOLD_BTN, MainFrame::OnToggleHold)
    EVT_BUTTON(ID_BROWSE_FILE, MainFrame::OnBrowseFile)
    EVT_BUTTON(ID_START_STOP_BTN, MainFrame::OnStartStop)
    EVT_BUTTON(ID_PREVIEW_BTN, MainFrame::OnPreviewStartStop)
    EVT_BUTTON(ID_PLAY_OPEN_BTN, MainFrame::OnPlayOpenFile)
    EVT_BUTTON(ID_PLAY_FIRST_BTN, MainFrame::OnPlayFirst)
    EVT_BUTTON(ID_PLAY_PAUSE_BTN, MainFrame::OnPlayPause)
    EVT_BUTTON(ID_PLAY_LAST_BTN, MainFrame::OnPlayLast)
    EVT_CHOICE(ID_PLAY_SPEED_CHOICE, MainFrame::OnPlaySpeedSelect)
    EVT_TEXT_ENTER(ID_PLAY_TIME_TXT, MainFrame::OnPlayTimeEnter)
    EVT_BUTTON(ID_PLAY_SEEK_BTN, MainFrame::OnPlaySeek)
    EVT_BUTTON(ID_PLAY_EXPORT_WAV_BTN, MainFrame::OnPlayExportWav)
    EVT_SCROLL_THUMBTRACK(MainFrame::OnPlaySliderThumbTrack)
    EVT_SCROLL_THUMBRELEASE(MainFrame::OnPlaySliderThumbRelease)
    EVT_SCROLL_CHANGED(MainFrame::OnPlaySliderScroll)
    EVT_TIMER(ID_UPDATE_TIMER, MainFrame::OnTimer)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size)
    : wxFrame(nullptr, wxID_ANY, title, pos, size),
      m_timer(this, ID_UPDATE_TIMER) {

    m_audioMgr = std::make_unique<AudioDeviceManager>();
    m_loggerWorker = std::make_shared<DataLoggerWorker>();

    InitUI();
    RefreshDeviceList();

    // 全てのUIコントロール構築が完了した後に初期化完了とする
    m_isInitializing = false;

    // 初期モード（ライブ入力）のUI同期を明示的に実行
    if (m_waveformPanel) {
        m_waveformPanel->SetPlaybackMode(false);
    }
    UpdateControlStates();

    // 30ms (約 33fps) 周期でタイマー起動
    m_timer.Start(30);
}

bool MainFrame::PerformSmokeTest() {
    if (!m_modeNotebook || !m_waveformPanel || !m_txtFilePath) return false;
    if (!wxFileName(m_txtFilePath->GetValue()).IsAbsolute()) return false;

    // 1. 再生タブへの切替をシミュレート
    m_modeNotebook->SetSelection(1);
    wxBookCtrlEvent event1(wxEVT_NOTEBOOK_PAGE_CHANGED, ID_MODE_NOTEBOOK, 1, 0);
    OnModeChanged(event1);

    // 2. ライブ入力タブへの切替をシミュレート
    m_modeNotebook->SetSelection(0);
    wxBookCtrlEvent event2(wxEVT_NOTEBOOK_PAGE_CHANGED, ID_MODE_NOTEBOOK, 0, 1);
    OnModeChanged(event2);

    return true;
}

MainFrame::~MainFrame() {
    if (m_timer.IsRunning()) {
        m_timer.Stop();
    }
    m_playbackLoader.Stop();
}

void MainFrame::InitUI() {
    SetMinSize(wxSize(850, 620));

    // メニューバー: 言語 / Language
    auto* menuBar = new wxMenuBar();
    auto* langMenu = new wxMenu();
    m_menuLangJa = langMenu->AppendRadioItem(ID_LANG_JA, wxT("日本語"));
    m_menuLangEn = langMenu->AppendRadioItem(ID_LANG_EN, wxT("English"));
    menuBar->Append(langMenu, wxT("言語 / Language"));
    SetMenuBar(menuBar);

    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // --- 上部: モード切替ノートブック ---
    m_modeNotebook = new wxNotebook(this, ID_MODE_NOTEBOOK, wxDefaultPosition, wxDefaultSize);

    // ========================================================
    // タブ 1: ライブ入力 (録音・プレビュー)
    // ========================================================
    m_panelLive = new wxPanel(m_modeNotebook);
    auto* liveMainSizer = new wxBoxSizer(wxVERTICAL);
    m_ctrlBox = new wxStaticBox(m_panelLive, wxID_ANY, wxT(" 設定・コントロール "));
    auto* ctrlSizer = new wxStaticBoxSizer(m_ctrlBox, wxVERTICAL);

    // 1行目: デバイス設定 & チャネル & レート
    auto* row1Sizer = new wxBoxSizer(wxHORIZONTAL);

    m_lblInputDevice = new wxStaticText(m_panelLive, wxID_ANY, wxT("入力デバイス:"));
    row1Sizer->Add(m_lblInputDevice, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_choiceDevice = new wxChoice(m_panelLive, ID_DEVICE_CHOICE, wxDefaultPosition, wxSize(240, -1));
    row1Sizer->Add(m_choiceDevice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    m_btnRefreshDevices = new wxButton(m_panelLive, ID_REFRESH_DEVICES, wxT("更新"), wxDefaultPosition, wxSize(50, -1));
    row1Sizer->Add(m_btnRefreshDevices, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_lblChannels = new wxStaticText(m_panelLive, wxID_ANY, wxT("チャネル数:"));
    row1Sizer->Add(m_lblChannels, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_choiceChannels = new wxChoice(m_panelLive, ID_CHANNEL_CHOICE, wxDefaultPosition, wxSize(110, -1));
    row1Sizer->Add(m_choiceChannels, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);

    m_btnChannelConfig = new wxButton(m_panelLive, ID_CH_CONFIG_BTN, wxT("チャネル設定..."), wxDefaultPosition, wxDefaultSize);
    m_btnChannelConfig->SetToolTip(wxT("各チャネルの信号名称および物理単位を設定します"));
    row1Sizer->Add(m_btnChannelConfig, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_lblSampleRate = new wxStaticText(m_panelLive, wxID_ANY, wxT("入力レート:"));
    row1Sizer->Add(m_lblSampleRate, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_choiceSampleRate = new wxChoice(m_panelLive, ID_RATE_CHOICE, wxDefaultPosition, wxSize(120, -1));
    row1Sizer->Add(m_choiceSampleRate, 0, wxALIGN_CENTER_VERTICAL, 0);

    ctrlSizer->Add(row1Sizer, 0, wxEXPAND | wxALL, 4);

    // 2行目: ソフトウェアデシメーション & 目標レート & フルスケール電圧・CAL校正 (FUNC-001, FUNC-002)
    auto* row2Sizer = new wxBoxSizer(wxHORIZONTAL);

    m_lblDecimation = new wxStaticText(m_panelLive, wxID_ANY, wxT("デシメーション:"));
    row2Sizer->Add(m_lblDecimation, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_choiceDecimation = new wxChoice(m_panelLive, ID_DECIMATION_CHOICE, wxDefaultPosition, wxSize(155, -1));
    UpdateDecimationChoices();
    m_choiceDecimation->SetSelection(1); // 1/2 デフォルト
    row2Sizer->Add(m_choiceDecimation, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_chkAntiAliasing = new wxCheckBox(m_panelLive, ID_ANTIALIASING_CHK, wxT("アンチエイリアス"));
    m_chkAntiAliasing->SetValue(true);
    m_chkAntiAliasing->SetToolTip(wxT("デシメーション時のローパスフィルタ（FIR／移動平均）を適用します。オフにすると単純間引き（ポイントサンプリング）を行い、フィルタ遅延やリンギングを排除します。"));
    row2Sizer->Add(m_chkAntiAliasing, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);

    m_lblTargetRate = new wxStaticText(m_panelLive, wxID_ANY, wxT("目標レート: 22050 Hz"));
    m_lblTargetRate->SetForegroundColour(wxColour(0, 120, 215));
    wxFont boldFont = m_lblTargetRate->GetFont();
    boldFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_lblTargetRate->SetFont(boldFont);
    row2Sizer->Add(m_lblTargetRate, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 20);

    // 電圧スケール & CAL校正
    m_lblVoltageScale = new wxStaticText(m_panelLive, wxID_ANY, wxT("フルスケール電圧[V]:"));
    row2Sizer->Add(m_lblVoltageScale, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_txtVoltageScale = new wxTextCtrl(m_panelLive, ID_VOLTAGE_TXT, wxT("1.0"), wxDefaultPosition, wxSize(60, -1));
    row2Sizer->Add(m_txtVoltageScale, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    m_btnCalibrate = new wxButton(m_panelLive, ID_CALIBRATE_BTN, wxT("校正..."), wxDefaultPosition, wxSize(65, -1));
    row2Sizer->Add(m_btnCalibrate, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_btnAcGuide = new wxButton(m_panelLive, ID_AC_GUIDE_BTN, wxT("AC結合について..."), wxDefaultPosition, wxDefaultSize);
    m_btnAcGuide->SetToolTip(wxT("PCオーディオI/Fのハードウェア回路特性（直流遮断・AC結合）に関するガイダンスを表示します"));
    row2Sizer->Add(m_btnAcGuide, 0, wxALIGN_CENTER_VERTICAL, 0);

    ctrlSizer->Add(row2Sizer, 0, wxEXPAND | wxALL, 4);

    // 3行目: ファイル保存先 & 録音開始/停止ボタン
    auto* row3Sizer = new wxBoxSizer(wxHORIZONTAL);
    m_lblSavePath = new wxStaticText(m_panelLive, wxID_ANY, wxT("保存先:"));
    row3Sizer->Add(m_lblSavePath, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    // デフォルトファイル名 (フルパス: <保存先ディレクトリ>/dlog_YYYYMMDD_HHMMSS.bin)
    wxDateTime now = wxDateTime::Now();
    wxString defaultFile = wxString::Format(wxT("dlog_%s.bin"), now.Format(wxT("%Y%m%d_%H%M%S")));
    wxFileName defaultFn(GetDefaultSaveDirectory(), defaultFile);
    m_txtFilePath = new wxTextCtrl(m_panelLive, wxID_ANY, defaultFn.GetFullPath(), wxDefaultPosition, wxSize(240, -1));
    row3Sizer->Add(m_txtFilePath, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    m_btnBrowse = new wxButton(m_panelLive, ID_BROWSE_FILE, wxT("参照..."), wxDefaultPosition, wxSize(65, -1));
    row3Sizer->Add(m_btnBrowse, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    // プレビュー開始/停止ボタン
    m_btnPreview = new wxButton(m_panelLive, ID_PREVIEW_BTN, wxT("プレビュー開始"), wxDefaultPosition, wxSize(120, 32));
    m_btnPreview->SetBackgroundColour(wxColour(0, 120, 180));
    m_btnPreview->SetForegroundColour(*wxWHITE);
    wxFont btnFont = m_btnPreview->GetFont();
    btnFont.SetPointSize(btnFont.GetPointSize() + 1);
    btnFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_btnPreview->SetFont(btnFont);
    row3Sizer->Add(m_btnPreview, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);

    // 記録開始/停止ボタン
    m_btnStartStop = new wxButton(m_panelLive, ID_START_STOP_BTN, wxT("記録開始"), wxDefaultPosition, wxSize(120, 32));
    m_btnStartStop->SetBackgroundColour(wxColour(30, 160, 60));
    m_btnStartStop->SetForegroundColour(*wxWHITE);
    m_btnStartStop->SetFont(btnFont);
    row3Sizer->Add(m_btnStartStop, 0, wxALIGN_CENTER_VERTICAL, 0);

    ctrlSizer->Add(row3Sizer, 0, wxEXPAND | wxALL, 4);
    liveMainSizer->Add(ctrlSizer, 1, wxEXPAND | wxALL, 2);
    m_panelLive->SetSizer(liveMainSizer);
    m_modeNotebook->AddPage(m_panelLive, wxT("ライブ入力"), true);

    // ========================================================
    // タブ 2: 記録データ再生
    // ========================================================
    m_panelPlayback = new wxPanel(m_modeNotebook);
    auto* playMainSizer = new wxBoxSizer(wxVERTICAL);
    m_playBox = new wxStaticBox(m_panelPlayback, wxID_ANY, wxT(" 再生コントロール "));
    auto* playSizer = new wxStaticBoxSizer(m_playBox, wxVERTICAL);

    // 再生 1行目: ファイル選択
    auto* playRow1 = new wxBoxSizer(wxHORIZONTAL);
    m_lblPlaybackFile = new wxStaticText(m_panelPlayback, wxID_ANY, wxT("ファイル:"));
    playRow1->Add(m_lblPlaybackFile, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_txtPlaybackFile = new wxTextCtrl(m_panelPlayback, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
    playRow1->Add(m_txtPlaybackFile, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_btnOpenPlaybackFile = new wxButton(m_panelPlayback, ID_PLAY_OPEN_BTN, wxT("開く..."), wxDefaultPosition, wxSize(70, -1));
    playRow1->Add(m_btnOpenPlaybackFile, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);

    m_btnExportWav = new wxButton(m_panelPlayback, ID_PLAY_EXPORT_WAV_BTN, TrWx(StringId::ButtonExportWav), wxDefaultPosition, wxDefaultSize);
    m_btnExportWav->SetToolTip(TrWx(StringId::TipExportWav));
    m_btnExportWav->Enable(false);
    playRow1->Add(m_btnExportWav, 0, wxALIGN_CENTER_VERTICAL, 0);

    playSizer->Add(playRow1, 0, wxEXPAND | wxALL, 3);

    // 再生 2行目: トランスポート & 再生速度 & 再生位置
    auto* playRow2 = new wxBoxSizer(wxHORIZONTAL);
    m_btnPlayFirst = new wxButton(m_panelPlayback, ID_PLAY_FIRST_BTN, wxT("⏮ 先頭"), wxDefaultPosition, wxSize(68, 28));
    playRow2->Add(m_btnPlayFirst, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_btnPlayPause = new wxButton(m_panelPlayback, ID_PLAY_PAUSE_BTN, wxT("▶ 再生"), wxDefaultPosition, wxSize(90, 28));
    m_btnPlayPause->SetBackgroundColour(wxColour(30, 160, 60));
    m_btnPlayPause->SetForegroundColour(*wxWHITE);
    wxFont ppFont = m_btnPlayPause->GetFont();
    ppFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_btnPlayPause->SetFont(ppFont);
    playRow2->Add(m_btnPlayPause, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_btnPlayLast = new wxButton(m_panelPlayback, ID_PLAY_LAST_BTN, wxT("⏭ 末尾"), wxDefaultPosition, wxSize(68, 28));
    playRow2->Add(m_btnPlayLast, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_lblPlaySpeed = new wxStaticText(m_panelPlayback, wxID_ANY, wxT("再生速度:"));
    playRow2->Add(m_lblPlaySpeed, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    wxArrayString speedChoices;
    speedChoices.Add(wxT("0.25×"));
    speedChoices.Add(wxT("0.5×"));
    speedChoices.Add(wxT("1×"));
    speedChoices.Add(wxT("2×"));
    speedChoices.Add(wxT("4×"));
    speedChoices.Add(wxT("8×"));
    speedChoices.Add(wxT("16×"));
    m_choicePlaySpeed = new wxChoice(m_panelPlayback, ID_PLAY_SPEED_CHOICE, wxDefaultPosition, wxSize(75, -1), speedChoices);
    m_choicePlaySpeed->SetSelection(2); // 1× デフォルト
    playRow2->Add(m_choicePlaySpeed, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_lblPlayPosition = new wxStaticText(m_panelPlayback, wxID_ANY, wxT("再生位置:"));
    playRow2->Add(m_lblPlayPosition, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    m_txtPlayTime = new wxTextCtrl(m_panelPlayback, ID_PLAY_TIME_TXT, wxT("00:00:00.000"), wxDefaultPosition, wxSize(105, -1), wxTE_PROCESS_ENTER);
    m_txtPlayTime->SetToolTip(wxT("指定書式: HH:MM:SS.ffffff または 秒数。\n※元データのサンプリング周期以上の時間分解能を意味するものではありません（直近サンプル点へ自動調整されます）。"));
    playRow2->Add(m_txtPlayTime, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
    m_btnSeekTime = new wxButton(m_panelPlayback, ID_PLAY_SEEK_BTN, wxT("移動"), wxDefaultPosition, wxSize(48, -1));
    playRow2->Add(m_btnSeekTime, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
    m_lblTotalDuration = new wxStaticText(m_panelPlayback, wxID_ANY, wxT("/ 00:00:00.000"));
    playRow2->Add(m_lblTotalDuration, 0, wxALIGN_CENTER_VERTICAL, 0);

    playSizer->Add(playRow2, 0, wxEXPAND | wxALL, 3);

    // 再生 3行目: シークバー (スライダー)
    auto* playRow3 = new wxBoxSizer(wxHORIZONTAL);
    m_sliderPlayback = new wxSlider(m_panelPlayback, ID_PLAY_SLIDER, 0, 0, 10000, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL);
    playRow3->Add(m_sliderPlayback, 1, wxALIGN_CENTER_VERTICAL, 0);
    playSizer->Add(playRow3, 0, wxEXPAND | wxALL, 3);

    playMainSizer->Add(playSizer, 1, wxEXPAND | wxALL, 2);
    m_panelPlayback->SetSizer(playMainSizer);
    m_modeNotebook->AddPage(m_panelPlayback, wxT("記録データ再生"), false);

    mainSizer->Add(m_modeNotebook, 0, wxEXPAND | wxALL, 4);

    // 波形描画オプションバー (時間幅・ゲイン)
    m_waveOptSizer = new wxBoxSizer(wxHORIZONTAL);
    m_lblWaveOptSettings = new wxStaticText(this, wxID_ANY, wxT("波形表示設定:  時間幅:"));
    m_waveOptSizer->Add(m_lblWaveOptSettings, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    wxArrayString timeWinArr;
    int defaultTimeWinSel = 10; // 2.0s
    for (size_t i = 0; i < NUM_TIME_WINDOW_CANDIDATES; ++i) {
        timeWinArr.Add(wxString::FromUTF8(TIME_WINDOW_CANDIDATES[i].label));
        if (std::fabs(TIME_WINDOW_CANDIDATES[i].seconds - 2.0) < 1e-4) {
            defaultTimeWinSel = static_cast<int>(i);
        }
    }
    m_choiceTimeWindow = new wxChoice(this, ID_TIME_WINDOW_CHOICE, wxDefaultPosition, wxSize(140, -1), timeWinArr);
    m_choiceTimeWindow->SetSelection(defaultTimeWinSel);
    m_waveOptSizer->Add(m_choiceTimeWindow, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_lblWaveOptGain = new wxStaticText(this, wxID_ANY, wxT("垂直ゲイン:"));
    m_waveOptSizer->Add(m_lblWaveOptGain, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
    wxArrayString gainArr;
    gainArr.Add(wxT("1x (±1.0 標準)"));
    gainArr.Add(wxT("2x (±0.5)"));
    gainArr.Add(wxT("5x (±0.2)"));
    gainArr.Add(wxT("10x (±0.1)"));
    gainArr.Add(wxT("20x (±0.05)"));
    gainArr.Add(wxT("50x (±0.02)"));
    m_choiceGain = new wxChoice(this, ID_GAIN_CHOICE, wxDefaultPosition, wxSize(120, -1), gainArr);
    m_choiceGain->SetSelection(0); // 1x
    m_waveOptSizer->Add(m_choiceGain, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_btnHold = new wxButton(this, ID_HOLD_BTN, wxT("波形ホールド"), wxDefaultPosition, wxSize(95, -1));
    m_btnHold->SetToolTip(wxT("ディスクへのデータ記録は継続したまま、画面上の波形描画のみを一時停止（静止）します"));
    m_waveOptSizer->Add(m_btnHold, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 15);

    m_lblWaveOptHint = new wxStaticText(this, wxID_ANY, wxT("(※記録中も変更可能)"));
    m_lblWaveOptHint->SetForegroundColour(wxColour(130, 140, 150));
    m_waveOptSizer->Add(m_lblWaveOptHint, 0, wxALIGN_CENTER_VERTICAL, 0);

    mainSizer->Add(m_waveOptSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // --- 中央: リアルタイム波形描画パネル ---
    m_waveformPanel = new WaveformPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 350));
    mainSizer->Add(m_waveformPanel, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    // --- 下部: ステータスパネル ---
    m_statusBox = new wxStaticBox(this, wxID_ANY, wxT(" 記録ステータス "));
    auto* statusSizer = new wxStaticBoxSizer(m_statusBox, wxHORIZONTAL);

    m_lblStatus = new wxStaticText(this, wxID_ANY, wxT("状態: 停止中"));
    m_lblStatus->SetForegroundColour(wxColour(120, 120, 120));
    m_lblStatus->SetFont(boldFont);
    statusSizer->Add(m_lblStatus, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 25);

    m_lblDuration = new wxStaticText(this, wxID_ANY, wxT("記録時間: 00:00:00"));
    statusSizer->Add(m_lblDuration, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 25);

    m_lblFileSize = new wxStaticText(this, wxID_ANY, wxT("ファイルサイズ: 0.00 MB"));
    statusSizer->Add(m_lblFileSize, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 25);

    m_lblOverruns = new wxStaticText(this, wxID_ANY, wxT("データ欠落: 0 件"));
    m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
    statusSizer->Add(m_lblOverruns, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 25);

    m_lblUiDrops = new wxStaticText(this, wxID_ANY, wxT("UI描画スキップ: 0 件"));
    m_lblUiDrops->SetForegroundColour(wxColour(100, 100, 100));
    statusSizer->Add(m_lblUiDrops, 0, wxALIGN_CENTER_VERTICAL, 0);

    mainSizer->Add(statusSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

    SetSizer(mainSizer);
    Layout();
    ApplyLanguage();
    UpdatePlaybackUI();
}

void MainFrame::ApplyLanguage() {
    SetTitle(TrWx(StringId::AppTitleLogger));

    if (m_menuLangJa && m_menuLangEn) {
        Language currentLang = GetUILanguage();
        m_menuLangJa->Check(currentLang == Language::Japanese);
        m_menuLangEn->Check(currentLang == Language::English);
    }

    if (m_modeNotebook) {
        m_modeNotebook->SetPageText(0, TrWx(StringId::TabLiveInput));
        m_modeNotebook->SetPageText(1, TrWx(StringId::TabPlayback));
    }

    if (m_ctrlBox) m_ctrlBox->SetLabel(TrWx(StringId::BoxSettingsAndControls));
    if (m_lblInputDevice) m_lblInputDevice->SetLabel(TrWx(StringId::LabelInputDevice));
    if (m_btnRefreshDevices) m_btnRefreshDevices->SetLabel(TrWx(StringId::ButtonRefresh));
    if (m_devices.empty() && m_choiceDevice && m_choiceDevice->GetCount() > 0) {
        m_choiceDevice->SetString(0, TrWx(StringId::DeviceNotFound));
        m_choiceDevice->SetSelection(0);
    }
    if (m_lblChannels) m_lblChannels->SetLabel(TrWx(StringId::LabelChannels));
    if (m_btnChannelConfig) {
        m_btnChannelConfig->SetLabel(TrWx(StringId::ButtonChannelConfig));
        m_btnChannelConfig->SetToolTip(TrWx(StringId::TipChannelConfig));
    }
    if (m_lblSampleRate) m_lblSampleRate->SetLabel(TrWx(StringId::LabelInputRate));
    if (m_lblDecimation) m_lblDecimation->SetLabel(TrWx(StringId::LabelDecimation));
    if (m_chkAntiAliasing) {
        m_chkAntiAliasing->SetLabel(TrWx(StringId::CheckAntiAliasing));
        m_chkAntiAliasing->SetToolTip(TrWx(StringId::TipAntiAliasing));
    }
    UpdateDecimationChoices();
    UpdateTargetRateDisplay();

    if (m_lblVoltageScale) m_lblVoltageScale->SetLabel(TrWx(StringId::LabelFullScaleVoltage));
    if (m_btnCalibrate) m_btnCalibrate->SetLabel(TrWx(StringId::ButtonCalibrate));
    if (m_btnAcGuide) {
        m_btnAcGuide->SetLabel(TrWx(StringId::ButtonAcGuide));
        m_btnAcGuide->SetToolTip(TrWx(StringId::TipAcGuide));
    }
    if (m_lblSavePath) m_lblSavePath->SetLabel(TrWx(StringId::LabelSavePath));
    if (m_btnBrowse) m_btnBrowse->SetLabel(TrWx(StringId::ButtonBrowse));

    // 再生タブ
    if (m_playBox) m_playBox->SetLabel(TrWx(StringId::BoxPlaybackControl));
    if (m_lblPlaybackFile) m_lblPlaybackFile->SetLabel(TrWx(StringId::LabelPlaybackFile));
    if (m_btnOpenPlaybackFile) m_btnOpenPlaybackFile->SetLabel(TrWx(StringId::ButtonOpenFile));
    if (m_btnPlayFirst) m_btnPlayFirst->SetLabel(TrWx(StringId::ButtonPlayFirst));
    if (m_btnPlayLast) m_btnPlayLast->SetLabel(TrWx(StringId::ButtonPlayLast));
    if (m_lblPlaySpeed) m_lblPlaySpeed->SetLabel(TrWx(StringId::LabelPlaySpeed));
    if (m_lblPlayPosition) m_lblPlayPosition->SetLabel(TrWx(StringId::LabelPlayPosition));
    if (m_txtPlayTime) m_txtPlayTime->SetToolTip(TrWx(StringId::TipPlayPosition));
    if (m_btnSeekTime) m_btnSeekTime->SetLabel(TrWx(StringId::ButtonSeek));
    if (m_btnExportWav) {
        m_btnExportWav->SetLabel(TrWx(StringId::ButtonExportWav));
        m_btnExportWav->SetToolTip(TrWx(StringId::TipExportWav));
    }

    // 波形オプション
    if (m_lblWaveOptSettings) m_lblWaveOptSettings->SetLabel(TrWx(StringId::LabelWaveformSettings));
    if (m_lblWaveOptGain) m_lblWaveOptGain->SetLabel(TrWx(StringId::LabelVerticalGain));
    if (m_btnHold) {
        m_btnHold->SetToolTip(TrWx(StringId::TipHoldWaveform));
        if (m_waveformPanel && m_waveformPanel->IsHold()) {
            m_btnHold->SetLabel(TrWx(StringId::ButtonResumeWaveform));
        } else {
            m_btnHold->SetLabel(TrWx(StringId::ButtonHoldWaveform));
        }
    }
    if (m_lblWaveOptHint) m_lblWaveOptHint->SetLabel(TrWx(StringId::LabelChangeDuringRecordHint));

    if (m_choiceGain && m_choiceGain->GetCount() > 0) {
        int sel = m_choiceGain->GetSelection();
        m_choiceGain->SetString(0, TrWx(StringId::GainStandard));
        m_choiceGain->SetSelection(sel);
    }

    if (m_statusBox) m_statusBox->SetLabel(TrWx(StringId::BoxRecordingStatus));

    UpdateControlStates();
    UpdatePlaybackUI();

    if ((!m_modeNotebook || m_modeNotebook->GetSelection() == 0) && m_appState != AppState::Recording && m_lastRecordingSummary.hasSummary) {
        int tot = static_cast<int>(m_lastRecordingSummary.durationSec);
        int h = tot / 3600;
        int m = (tot % 3600) / 60;
        int s = tot % 60;
        double mb = static_cast<double>(m_lastRecordingSummary.totalBytesWritten) / (1024.0 * 1024.0);
        m_lblDuration->SetLabel(TrFormatWx(StringId::DurationPrevFormat, wxString::Format(wxT("%02d:%02d:%02d (%.2f MB)"), h, m, s, mb)));
        if (m_lastRecordingSummary.isError) {
            m_lblFileSize->SetLabel(TrWx(StringId::StatusAbortedPartialKept));
        } else {
            m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxString::Format(wxT("%.2f"), mb)));
        }
        if (m_appState == AppState::Previewing) {
            LoggerStats audioStats = m_audioMgr->GetStats();
            uint32_t previewOverruns = audioStats.bufferOverruns;
            if (previewOverruns == 0) {
                if (m_lastRecordingSummary.totalOverruns > 0) {
                    m_lblOverruns->SetLabel(TrFormatWx(StringId::PreviewDropoutsNormalWithPrev, std::to_string(m_lastRecordingSummary.totalOverruns)));
                    m_lblOverruns->SetForegroundColour(wxColour(220, 120, 20));
                } else {
                    m_lblOverruns->SetLabel(TrWx(StringId::PreviewDropoutsNormal));
                    m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
                }
            } else {
                if (m_lastRecordingSummary.totalOverruns > 0) {
                    m_lblOverruns->SetLabel(TrFormatWx(StringId::PreviewDropoutsWarningWithPrev, std::to_string(previewOverruns), std::to_string(m_lastRecordingSummary.totalOverruns)));
                    m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
                } else {
                    m_lblOverruns->SetLabel(TrFormatWx(StringId::PreviewDropoutsWarning, std::to_string(previewOverruns)));
                    m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
                }
            }
        } else {
            if (m_lastRecordingSummary.totalOverruns > 0) {
                m_lblOverruns->SetLabel(TrFormatWx(StringId::OverrunsWarningFormat, std::to_string(m_lastRecordingSummary.totalOverruns)));
                m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
            } else {
                m_lblOverruns->SetLabel(TrWx(StringId::OverrunsNormal));
                m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
            }
        }
    }

    if (m_waveformPanel) {
        m_waveformPanel->Refresh(false);
    }
    Layout();
}

void MainFrame::OnLanguageSelectJa(wxCommandEvent& /*event*/) {
    if (GetUILanguage() != Language::Japanese) {
        std::string err;
        bool saved = LanguagePreferences::SetCurrentLanguage(Language::Japanese, true, &err);
        ApplyLanguage();
        if (!saved && !err.empty()) {
            SetStatusText(TrFormatWx(StringId::WarnSaveLanguageFailed, err));
        }
    }
}

void MainFrame::OnLanguageSelectEn(wxCommandEvent& /*event*/) {
    if (GetUILanguage() != Language::English) {
        std::string err;
        bool saved = LanguagePreferences::SetCurrentLanguage(Language::English, true, &err);
        ApplyLanguage();
        if (!saved && !err.empty()) {
            SetStatusText(TrFormatWx(StringId::WarnSaveLanguageFailed, err));
        }
    }
}

void MainFrame::RefreshDeviceList() {
    m_devices = m_audioMgr->GetInputDevices();
    m_choiceDevice->Clear();

    if (m_devices.empty()) {
        m_choiceDevice->Append(TrWx(StringId::DeviceNotFound));
        m_choiceDevice->SetSelection(0);
        m_choiceDevice->Disable();
        UpdateChannelChoices();
        UpdateSampleRateChoices();
        UpdateDecimationChoices();
        UpdateTargetRateDisplay();
        return;
    }

    m_choiceDevice->Enable();
    int defaultDevId = m_audioMgr->GetDefaultInputDevice();
    int selectIdx = 0;

    for (size_t i = 0; i < m_devices.size(); ++i) {
        const auto& d = m_devices[i];
        wxString item = wxString::Format(wxT("[%s] %s (%d ch)"),
                                         wxString::FromUTF8(d.hostApiName.c_str()),
                                         wxString::FromUTF8(d.name.c_str()),
                                         d.maxInputChannels);
        m_choiceDevice->Append(item);
        if (d.id == defaultDevId) {
            selectIdx = static_cast<int>(i);
        }
    }

    m_choiceDevice->SetSelection(selectIdx);
    wxCommandEvent dummy;
    OnDeviceSelect(dummy);
}

static wxString FormatRateLabel(double hz) {
    if (hz >= 1000.0) {
        double khz = hz / 1000.0;
        if (std::fabs(khz - std::round(khz)) < 1e-4) {
            return wxString::Format(wxT("%.0f kHz"), khz);
        } else if (std::fabs(khz * 10.0 - std::round(khz * 10.0)) < 1e-4) {
            return wxString::Format(wxT("%.1f kHz"), khz);
        } else if (std::fabs(khz * 100.0 - std::round(khz * 100.0)) < 1e-4) {
            return wxString::Format(wxT("%.2f kHz"), khz);
        } else {
            return wxString::Format(wxT("%.3f kHz"), khz);
        }
    }
    return wxString::Format(wxT("%.0f Hz"), hz);
}

void MainFrame::UpdateSampleRateChoices() {
    if (!m_choiceSampleRate) return;

    // 現在の選択値を取得（維持可能なら維持するため）
    double prevRate = 0.0;
    int curSel = m_choiceSampleRate->GetSelection();
    if (curSel >= 0 && curSel < static_cast<int>(m_availableSampleRates.size())) {
        prevRate = m_availableSampleRates[curSel];
    }

    // 選択中デバイスIDとチャネル数を取得
    int devIdx = m_choiceDevice ? m_choiceDevice->GetSelection() : -1;
    int devId = -1;
    double defaultRate = 44100.0;
    if (devIdx >= 0 && devIdx < static_cast<int>(m_devices.size())) {
        devId = m_devices[devIdx].id;
        defaultRate = m_devices[devIdx].defaultSampleRate;
    }
    int channels = GetSelectedChannelCount();

    m_availableSampleRates.clear();
    m_choiceSampleRate->Clear();

    bool hasQueryFailure = false;
    PaError lastQueryError = paNoError;

    if (devId >= 0 && channels > 0) {
        for (size_t i = 0; i < NUM_CANDIDATE_SAMPLE_RATES; ++i) {
            double rate = CANDIDATE_SAMPLE_RATES[i];
            PaError err = m_audioMgr->CheckInputFormatSupport(devId, channels, rate);
            if (err == paFormatIsSupported) {
                m_availableSampleRates.push_back(rate);
            } else if (err != paInvalidSampleRate && err != paFormatIsSupported) {
                hasQueryFailure = true;
                lastQueryError = err;
            }
        }
    }

    if (m_availableSampleRates.empty()) {
        wxString errorMsg = TrWx(StringId::ErrNoCompatibleRate);
        if (hasQueryFailure && lastQueryError != paNoError) {
            errorMsg += wxString::Format(wxT(" (%s)"), wxString::FromUTF8(Pa_GetErrorText(lastQueryError)));
        }
        m_choiceSampleRate->Append(errorMsg);
        m_choiceSampleRate->SetSelection(0);
        m_choiceSampleRate->Disable();
        if (m_btnStartStop && !m_audioMgr->IsStreaming()) {
            m_btnStartStop->Disable();
        }
        if (m_lblStatus && !m_audioMgr->IsStreaming()) {
            m_lblStatus->SetLabel(TrFormatWx(StringId::StatusErrorFormat, errorMsg));
            m_lblStatus->SetForegroundColour(wxColour(200, 50, 50));
        }
    } else {
        for (double r : m_availableSampleRates) {
            m_choiceSampleRate->Append(FormatRateLabel(r));
        }
        m_choiceSampleRate->Enable();
        if (m_btnStartStop && !m_audioMgr->IsStreaming()) {
            m_btnStartStop->Enable();
        }
        if (m_lblStatus && !m_audioMgr->IsStreaming()) {
            m_lblStatus->SetLabel(TrWx(StringId::StatusStopped));
            m_lblStatus->SetForegroundColour(wxColour(120, 120, 120));
        }

        // 優先順位に従って選択レートを決定 (要求 5)
        double selectedRate = SelectAppropriateSampleRate(m_availableSampleRates, prevRate, defaultRate);
        int selectIdx = 0;
        for (size_t i = 0; i < m_availableSampleRates.size(); ++i) {
            if (std::fabs(m_availableSampleRates[i] - selectedRate) < 0.5) {
                selectIdx = static_cast<int>(i);
                break;
            }
        }
        m_choiceSampleRate->SetSelection(selectIdx);
    }
}

double MainFrame::GetSelectedNativeSampleRate() const {
    if (!m_choiceSampleRate || m_availableSampleRates.empty()) {
        return 44100.0;
    }
    int rateSel = m_choiceSampleRate->GetSelection();
    if (rateSel >= 0 && rateSel < static_cast<int>(m_availableSampleRates.size())) {
        return m_availableSampleRates[rateSel];
    }
    return m_availableSampleRates[0];
}

void MainFrame::UpdateDecimationChoices() {
    if (!m_choiceDecimation) return;

    int currentSel = m_choiceDecimation->GetSelection();
    if (currentSel < 0) currentSel = 1; // デフォルト 1/2

    double nativeRate = GetSelectedNativeSampleRate();
    bool isJa = (GetUILanguage() == Language::Japanese);

    wxArrayString decArr;
    decArr.Add(wxString::Format(isJa ? wxT("1/1 (%s - ネイティブ)") : wxT("1/1 (%s - Native)"), FormatRateLabel(nativeRate)));
    decArr.Add(wxString::Format(wxT("1/2 (%s)"), FormatRateLabel(nativeRate / 2.0)));
    decArr.Add(wxString::Format(wxT("1/4 (%s)"), FormatRateLabel(nativeRate / 4.0)));
    decArr.Add(wxString::Format(wxT("1/8 (%s)"), FormatRateLabel(nativeRate / 8.0)));
    const auto addApproximateRate = [&](double requestedRate, const wxString& description) {
        int factor = std::max(1, static_cast<int>(std::round(nativeRate / requestedRate)));
        double actualRate = nativeRate / static_cast<double>(factor);
        decArr.Add(wxString::Format(isJa ? wxT("約 %.0f Hz: 1/%d (実 %.3f Hz%s)") : wxT("Approx %.0f Hz: 1/%d (Actual %.3f Hz%s)"),
                                    requestedRate, factor, actualRate, description));
    };
    addApproximateRate(1000.0, wxEmptyString);
    addApproximateRate(100.0, wxEmptyString);
    addApproximateRate(10.0, isJa ? wxT("・低周波") : wxT(" - Low Freq"));
    addApproximateRate(1.0, isJa ? wxT("・超長時間") : wxT(" - Ultra Long"));

    m_choiceDecimation->Clear();
    for (const auto& item : decArr) {
        m_choiceDecimation->Append(item);
    }

    if (currentSel >= 0 && currentSel < static_cast<int>(m_choiceDecimation->GetCount())) {
        m_choiceDecimation->SetSelection(currentSel);
    } else {
        m_choiceDecimation->SetSelection(1);
    }
}

void MainFrame::UpdateTargetRateDisplay() {
    if (!m_choiceDecimation || !m_lblTargetRate) return;

    double nativeRate = GetSelectedNativeSampleRate();
    int decSel = m_choiceDecimation->GetSelection();
    if (decSel < 0) decSel = 1;

    int decFactor = 1;
    double customRate = 0.0;
    switch (decSel) {
        case 0: decFactor = 1; break;
        case 1: decFactor = 2; break;
        case 2: decFactor = 4; break;
        case 3: decFactor = 8; break;
        case 4: customRate = 1000.0; break;
        case 5: customRate = 100.0; break;
        case 6: customRate = 10.0; break;
        case 7: customRate = 1.0; break;
        default: decFactor = 2; break;
    }

    if (customRate > 0.0) {
        decFactor = static_cast<int>(std::round(nativeRate / customRate));
        if (decFactor < 1) decFactor = 1;
    }

    double effectiveTargetRate = nativeRate / static_cast<double>(decFactor);

    wxString rateStr = wxString::Format(wxT("%.6g"), effectiveTargetRate);
    m_lblTargetRate->SetLabel(TrFormatWx(StringId::TargetRateFormat, rateStr, std::to_string(decFactor)));
}

void MainFrame::OnVoltageChange(wxCommandEvent& /*event*/) {
    double vFs = 1.0;
    if (m_txtVoltageScale && m_txtVoltageScale->GetValue().ToDouble(&vFs) && vFs > 0.0) {
        m_waveformPanel->SetFullScaleVoltage(vFs);
    }
}

void MainFrame::OnCalibrate(wxCommandEvent& /*event*/) {
    if (m_appState == AppState::Recording) {
        wxMessageBox(TrWx(StringId::ErrCannotCalibrateWhileRecording),
                     TrWx(StringId::WarningDialogTitle), wxOK | wxICON_WARNING, this);
        return;
    }

    if (m_appState == AppState::Stopped) {
        int ans = wxMessageBox(
            TrWx(StringId::CalibrateStartPromptConfirm),
            TrWx(StringId::CalibrateDialogTitle),
            wxYES_NO | wxICON_QUESTION, this
        );
        if (ans != wxYES) return;

        if (!StartPreview()) {
            return;
        }
    }

    CalibrationDialog dlg(this, m_waveformPanel, GetSelectedFullScaleVoltage(), GetSelectedChannelCount());
    if (dlg.ShowModal() == wxID_OK) {
        double newVfs = dlg.GetCalculatedVfs();
        m_txtVoltageScale->SetValue(wxString::Format(wxT("%.4f"), newVfs));
        m_waveformPanel->SetFullScaleVoltage(newVfs);

        wxMessageBox(
            TrFormatWx(StringId::CalibrateSuccessWithChFormat,
                       std::to_string(dlg.GetSelectedChannel() + 1),
                       wxString::Format(wxT("%.4f"), dlg.GetMeasuredVpp()),
                       wxString::Format(wxT("%.3f"), dlg.GetTargetVpp()),
                       wxString::Format(wxT("%.4f"), newVfs)),
            TrWx(StringId::CalibrateSuccessTitle), wxOK | wxICON_INFORMATION, this
        );
    }
}

void MainFrame::OnToggleHold(wxCommandEvent& /*event*/) {
    if (!m_waveformPanel || !m_btnHold) return;
    bool newHold = !m_waveformPanel->IsHold();
    m_waveformPanel->SetHold(newHold);

    if (newHold) {
        m_btnHold->SetLabel(TrWx(StringId::ButtonResumeWaveform));
        m_btnHold->SetBackgroundColour(wxColour(220, 110, 20));
        m_btnHold->SetForegroundColour(*wxWHITE);
    } else {
        m_btnHold->SetLabel(TrWx(StringId::ButtonHoldWaveform));
        m_btnHold->SetBackgroundColour(wxNullColour);
        m_btnHold->SetForegroundColour(wxNullColour);
    }
    m_btnHold->Refresh();
}

void MainFrame::OnAcGuide(wxCommandEvent& /*event*/) {
    wxMessageBox(TrWx(StringId::AcGuideContent), TrWx(StringId::AcGuideTitle), wxOK | wxICON_INFORMATION, this);
}

void MainFrame::OnChannelConfig(wxCommandEvent& /*event*/) {
    int channels = GetSelectedChannelCount();
    ChannelConfigDialog dlg(this, m_channelMetadata, channels);
    if (dlg.ShowModal() == wxID_OK) {
        m_channelMetadata = dlg.GetMetadata();
        m_waveformPanel->SetChannelMetadata(m_channelMetadata);
    }
}

int MainFrame::GetSelectedChannelCount() const {
    int sel = m_choiceChannels->GetSelection();
    if (sel >= 0 && sel < static_cast<int>(m_availableChannels.size())) {
        return m_availableChannels[sel];
    }
    return (!m_availableChannels.empty()) ? m_availableChannels[0] : 2;
}

double MainFrame::GetSelectedFullScaleVoltage() const {
    double vFs = 1.0;
    if (m_txtVoltageScale && m_txtVoltageScale->GetValue().ToDouble(&vFs) && vFs > 0.0) {
        return vFs;
    }
    return 1.0;
}

void MainFrame::UpdateChannelChoices() {
    int devIdx = m_choiceDevice->GetSelection();
    int maxChannels = 2;
    if (devIdx >= 0 && devIdx < static_cast<int>(m_devices.size())) {
        maxChannels = std::min(static_cast<int>(MAX_AUDIO_CHANNELS),
                               std::max(1, m_devices[devIdx].maxInputChannels));
    }

    int prevSelectedCount = GetSelectedChannelCount();

    m_availableChannels.clear();
    for (int chCandidate : {1, 2, 4, 8}) {
        if (chCandidate <= maxChannels) {
            m_availableChannels.push_back(chCandidate);
        }
    }
    // 端数チャネル数（例: 6ch等）もカバー
    if (std::find(m_availableChannels.begin(), m_availableChannels.end(), maxChannels) == m_availableChannels.end()) {
        m_availableChannels.push_back(maxChannels);
        std::sort(m_availableChannels.begin(), m_availableChannels.end());
    }
    if (m_availableChannels.empty()) {
        m_availableChannels.push_back(1);
    }

    m_choiceChannels->Clear();
    int newSelectIdx = 0;
    for (size_t i = 0; i < m_availableChannels.size(); ++i) {
        int ch = m_availableChannels[i];
        wxString label;
        if (ch == 1) {
            label = wxT("1 ch (Mono)");
        } else if (ch == 2) {
            label = wxT("2 ch (Stereo)");
        } else {
            label = wxString::Format(wxT("%d ch"), ch);
        }
        m_choiceChannels->Append(label);

        if (ch == prevSelectedCount) {
            newSelectIdx = static_cast<int>(i);
        } else if (prevSelectedCount > maxChannels && ch == m_availableChannels.back()) {
            newSelectIdx = static_cast<int>(i);
        }
    }

    m_choiceChannels->SetSelection(newSelectIdx);
    int activeCh = GetSelectedChannelCount();
    if (m_channelMetadata.size() < static_cast<size_t>(activeCh)) {
        size_t oldSize = m_channelMetadata.size();
        m_channelMetadata.resize(activeCh);
        for (size_t i = oldSize; i < static_cast<size_t>(activeCh); ++i) {
            m_channelMetadata[i].name = "CH" + std::to_string(i + 1);
            m_channelMetadata[i].unit = "V";
        }
    }
    m_waveformPanel->SetChannelMetadata(m_channelMetadata);
    m_waveformPanel->SetChannelCount(activeCh);
}

void MainFrame::OnDeviceSelect(wxCommandEvent& /*event*/) {
    UpdateChannelChoices();
    UpdateSampleRateChoices();
    UpdateDecimationChoices();
    UpdateTargetRateDisplay();
    UpdateControlStates();
}

void MainFrame::OnRefreshDevices(wxCommandEvent& /*event*/) {
    RefreshDeviceList();
}

void MainFrame::OnChannelSelect(wxCommandEvent& /*event*/) {
    int ch = GetSelectedChannelCount();
    if (m_channelMetadata.size() < static_cast<size_t>(ch)) {
        size_t oldSize = m_channelMetadata.size();
        m_channelMetadata.resize(ch);
        for (size_t i = oldSize; i < static_cast<size_t>(ch); ++i) {
            m_channelMetadata[i].name = "CH" + std::to_string(i + 1);
            m_channelMetadata[i].unit = "V";
        }
    }
    m_waveformPanel->SetChannelMetadata(m_channelMetadata);
    m_waveformPanel->SetChannelCount(ch);

    UpdateSampleRateChoices();
    UpdateDecimationChoices();
    UpdateTargetRateDisplay();
    UpdateControlStates();
}

void MainFrame::OnRateSelect(wxCommandEvent& /*event*/) {
    UpdateDecimationChoices();
    UpdateTargetRateDisplay();
    UpdateControlStates();
}

void MainFrame::OnDecimationSelect(wxCommandEvent& /*event*/) {
    UpdateTargetRateDisplay();
    UpdateControlStates();
}

void MainFrame::OnTimeWindowSelect(wxCommandEvent& /*event*/) {
    int sel = m_choiceTimeWindow->GetSelection();
    double win = (sel >= 0 && sel < static_cast<int>(NUM_TIME_WINDOW_CANDIDATES))
                     ? TIME_WINDOW_CANDIDATES[sel].seconds
                     : 2.0;
    m_waveformPanel->SetTimeWindow(win);
    if (m_modeNotebook && m_modeNotebook->GetSelection() == 1 && m_playbackLoader.IsOpen()) {
        RequestPlaybackWindow(GetCurrentPlaybackPosition(), true /* isNewEpoch */);
    }
    m_waveformPanel->Refresh(false);

}

void MainFrame::OnGainSelect(wxCommandEvent& /*event*/) {
    float gains[] = {1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f};
    int sel = m_choiceGain->GetSelection();
    float g = (sel >= 0 && sel < 6) ? gains[sel] : 1.0f;
    m_waveformPanel->SetVerticalGain(g);
    m_waveformPanel->Refresh(false);
}

wxString MainFrame::GetDefaultSaveDirectory() const {
    wxFileName exeFn(wxStandardPaths::Get().GetExecutablePath());
    wxString exeDir = exeFn.GetPath();
    wxString tempDir = wxFileName(wxStandardPaths::Get().GetTempDir(), wxEmptyString).GetPath();

    auto isInsideTempOrStage = [&](const wxString& dir) {
        if (dir.IsEmpty()) return true;
        wxString normDir = dir.Lower();
        if (!tempDir.IsEmpty() && normDir.StartsWith(tempDir.Lower())) {
            return true;
        }
        if (normDir.Contains(wxT("\\appdata\\local\\temp")) ||
            normDir.Contains(wxT("/appdata/local/temp")) ||
            normDir.Contains(wxT(".binary-stage-")) ||
            normDir.Contains(wxT(".builder-stage-"))) {
            return true;
        }
        return false;
    };

    if (!exeDir.IsEmpty() && !isInsideTempOrStage(exeDir) && wxFileName::IsDirWritable(exeDir)) {
        return exeDir;
    }
    wxString cwd = wxGetCwd();
    if (!cwd.IsEmpty() && !isInsideTempOrStage(cwd) && wxFileName::IsDirWritable(cwd)) {
        return cwd;
    }
    wxString docsDir = wxStandardPaths::Get().GetDocumentsDir();
    if (!docsDir.IsEmpty() && wxFileName::IsDirWritable(docsDir)) {
        return docsDir;
    }
    return !exeDir.IsEmpty() ? exeDir : cwd;
}

void MainFrame::OnBrowseFile(wxCommandEvent& /*event*/) {
    wxFileName currentFn(m_txtFilePath->GetValue().Trim().Trim(false));
    wxString defaultDir = currentFn.GetPath();
    if (defaultDir.IsEmpty() || !wxDirExists(defaultDir)) {
        defaultDir = GetDefaultSaveDirectory();
    }
    wxString defaultName = currentFn.GetFullName();
    if (defaultName.IsEmpty()) {
        wxDateTime now = wxDateTime::Now();
        defaultName = wxString::Format(wxT("dlog_%s.bin"), now.Format(wxT("%Y%m%d_%H%M%S")));
    }

    wxFileDialog saveFileDialog(this, TrWx(StringId::LabelSavePath), defaultDir,
                                defaultName,
                                TrWx(StringId::FilterDlogFiles),
                                wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

    if (saveFileDialog.ShowModal() == wxID_CANCEL) return;
    m_txtFilePath->SetValue(saveFileDialog.GetPath());
}

LoggerConfig MainFrame::BuildConfig(bool forRecording, wxString& errorMsg) const {
    LoggerConfig config;
    errorMsg.Clear();

    if (m_devices.empty() || m_choiceDevice->GetSelection() < 0) {
        errorMsg = TrWx(StringId::ErrNoInputDevice);
        return config;
    }

    int devIdx = m_choiceDevice->GetSelection();
    int devId = m_devices[devIdx].id;

    int channels = GetSelectedChannelCount();
    if (channels < 1 || channels > static_cast<int>(MAX_AUDIO_CHANNELS) ||
        channels > m_devices[devIdx].maxInputChannels) {
        errorMsg = TrFormatWx(StringId::ErrChannelsExceedMaxFormat,
                              std::to_string(channels),
                              std::to_string(m_devices[devIdx].maxInputChannels));
        return config;
    }

    if (m_availableSampleRates.empty()) {
        errorMsg = TrWx(StringId::ErrNoCompatibleRate);
        return config;
    }

    double nativeRate = GetSelectedNativeSampleRate();

    int decSel = m_choiceDecimation->GetSelection();
    int decFactor = 1;
    double customRate = 0.0;
    switch (decSel) {
        case 0: decFactor = 1; break;
        case 1: decFactor = 2; break;
        case 2: decFactor = 4; break;
        case 3: decFactor = 8; break;
        case 4: customRate = 1000.0; break;
        case 5: customRate = 100.0; break;
        case 6: customRate = 10.0; break;
        case 7: customRate = 1.0; break;
        default: decFactor = 2; break;
    }

    if (customRate > 0.0) {
        decFactor = static_cast<int>(std::round(nativeRate / customRate));
        if (decFactor < 1) decFactor = 1;
    }

    double vFs = GetSelectedFullScaleVoltage();

    config.deviceId = devId;
    config.channels = channels;
    config.nativeSampleRate = nativeRate;
    config.decimationFactor = decFactor;
    config.customTargetSampleRate = customRate;
    config.enableAntiAliasing = (m_chkAntiAliasing != nullptr) ? m_chkAntiAliasing->GetValue() : true;
    config.fullScaleVoltage = vFs;
    config.channelMetadata = m_channelMetadata;
    config.framesPerBuffer = 512;
    config.sampleFormat = SampleFormatType::Float32;

    if (forRecording) {
        wxString filePath = m_txtFilePath->GetValue().Trim().Trim(false);
        if (filePath.IsEmpty()) {
            errorMsg = TrWx(StringId::PromptSpecifySavePath);
            return config;
        }
        wxFileName fn(filePath);
        if (!fn.IsAbsolute()) {
            fn.MakeAbsolute(GetDefaultSaveDirectory());
            filePath = fn.GetFullPath();
        }
        config.outputFilePath = std::string(filePath.utf8_str());
    } else {
        config.outputFilePath = "";
    }

    return config;
}

bool MainFrame::StartPreview() {
    if (m_devices.empty() || m_choiceDevice->GetSelection() < 0) {
        wxMessageBox(TrWx(StringId::ErrNoInputDevice), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        return false;
    }

    if (m_availableSampleRates.empty()) {
        wxMessageBox(TrWx(StringId::ErrNoCompatibleRate), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        return false;
    }

    wxString errorMsg;
    LoggerConfig config = BuildConfig(false /* forRecording = false */, errorMsg);
    if (!errorMsg.IsEmpty()) {
        wxMessageBox(errorMsg, TrWx(StringId::ConfigErrorTitle), wxOK | wxICON_ERROR, this);
        return false;
    }

    // プレビューではファイル保存ワーカーを渡さない (nullptr)
    if (!m_audioMgr->OpenStream(config, nullptr)) {
        wxMessageBox(TrWx(StringId::ErrAudioStreamOpen), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        return false;
    }

    if (!m_audioMgr->StartStream()) {
        m_audioMgr->StopStream();
        wxMessageBox(TrWx(StringId::ErrAudioStreamStart), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        return false;
    }

    m_activeConfig = config;
    m_hasActiveConfig = true;
    m_hasErrorStatus = false;

    // 波形パネル初期化
    m_waveformPanel->SetTimeReferenceMode(WaveformPanel::TimeReferenceMode::PreviewStart);
    if (m_waveformPanel->IsHold()) {
        m_waveformPanel->SetHold(false);
        if (m_btnHold) {
            m_btnHold->SetLabel(TrWx(StringId::ButtonHoldWaveform));
            m_btnHold->SetBackgroundColour(wxNullColour);
            m_btnHold->SetForegroundColour(wxNullColour);
            m_btnHold->Refresh();
        }
    }
    m_waveformPanel->SetSampleRate(config.getTargetSampleRate());
    m_waveformPanel->SetChannelCount(config.channels);
    m_waveformPanel->Clear();

    if (m_lastRecordingSummary.hasSummary) {
        int tot = static_cast<int>(m_lastRecordingSummary.durationSec);
        int h = tot / 3600;
        int m = (tot % 3600) / 60;
        int s = tot % 60;
        double mb = static_cast<double>(m_lastRecordingSummary.totalBytesWritten) / (1024.0 * 1024.0);
        m_lblDuration->SetLabel(TrFormatWx(StringId::DurationPrevFormat, wxString::Format(wxT("%02d:%02d:%02d (%.2f MB)"), h, m, s, mb)));
        if (m_lastRecordingSummary.isError) {
            m_lblFileSize->SetLabel(TrWx(StringId::StatusAbortedPartialKept));
        } else {
            m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxString::Format(wxT("%.2f"), mb)));
        }
    } else {
        m_lblDuration->SetLabel(TrFormatWx(StringId::DurationLiveFormat, wxT("--:--:--")));
        m_lblFileSize->SetLabel(TrWx(StringId::FileSizeNone));
    }
    if (m_lastRecordingSummary.hasSummary && m_lastRecordingSummary.totalOverruns > 0) {
        m_lblOverruns->SetLabel(TrFormatWx(StringId::PreviewDropoutsNormalWithPrev, std::to_string(m_lastRecordingSummary.totalOverruns)));
        m_lblOverruns->SetForegroundColour(wxColour(220, 120, 20));
    } else {
        m_lblOverruns->SetLabel(TrWx(StringId::PreviewDropoutsNormal));
        m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
    }
    m_lblUiDrops->SetLabel(TrFormatWx(StringId::UiDropsFormat, std::string("0")));

    m_appState = AppState::Previewing;
    UpdateControlStates();

    if (!m_timer.IsRunning()) {
        m_timer.Start(30);
    }

    return true;
}

void MainFrame::StopPreview(bool isError, wxString errorMsg) {
    m_audioMgr->StopStream();

    m_appState = AppState::Stopped;
    m_hasErrorStatus = isError;
    UpdateControlStates();

    if (m_lastRecordingSummary.hasSummary) {
        int tot = static_cast<int>(m_lastRecordingSummary.durationSec);
        int h = tot / 3600;
        int m = (tot % 3600) / 60;
        int s = tot % 60;
        double mb = static_cast<double>(m_lastRecordingSummary.totalBytesWritten) / (1024.0 * 1024.0);
        m_lblDuration->SetLabel(TrFormatWx(StringId::DurationPrevFormat, wxString::Format(wxT("%02d:%02d:%02d (%.2f MB)"), h, m, s, mb)));
        if (m_lastRecordingSummary.isError) {
            m_lblFileSize->SetLabel(TrWx(StringId::StatusAbortedPartialKept));
        } else {
            m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxString::Format(wxT("%.2f"), mb)));
        }
        if (m_lastRecordingSummary.totalOverruns > 0) {
            m_lblOverruns->SetLabel(TrFormatWx(StringId::OverrunsWarningFormat, std::to_string(m_lastRecordingSummary.totalOverruns)));
            m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
        } else {
            m_lblOverruns->SetLabel(TrWx(StringId::OverrunsNormal));
            m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
        }
    }

    if (isError) {
        m_lblStatus->SetLabel(TrWx(StringId::StatusStoppedError));
        m_lblStatus->SetForegroundColour(wxColour(220, 40, 40));
        if (!errorMsg.IsEmpty()) {
            wxMessageBox(errorMsg, TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        }
    }
}

bool MainFrame::StartRecording() {
    // 1. 保存先ファイルパスの事前検証と絶対パスへの正規化
    wxString filePath = m_txtFilePath->GetValue().Trim().Trim(false);
    if (filePath.IsEmpty()) {
        wxMessageBox(TrWx(StringId::PromptSpecifySavePath), TrWx(StringId::WarningDialogTitle), wxOK | wxICON_WARNING, this);
        return false;
    }

    wxFileName resolvedFn(filePath);
    if (!resolvedFn.IsAbsolute()) {
        resolvedFn.MakeAbsolute(GetDefaultSaveDirectory());
        filePath = resolvedFn.GetFullPath();
        m_txtFilePath->SetValue(filePath);
    }

    if (wxFileExists(filePath)) {
        int ans = wxMessageBox(
            TrFormatWx(StringId::ConfirmOverwritePrompt, filePath),
            TrWx(StringId::ConfirmDialogTitle),
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
            this);
        if (ans != wxYES) {
            return false;
        }
    }

    // 2. 記録開始前のプレビュー状態を保持し、プレビュー中なら安全に一時停止
    m_wasPreviewingBeforeRecord = (m_appState == AppState::Previewing);
    if (m_wasPreviewingBeforeRecord) {
        m_audioMgr->StopStream();
        m_appState = AppState::Stopped;
        UpdateControlStates();
    }

    auto restorePreviewOrStop = [&]() {
        if (m_wasPreviewingBeforeRecord) {
            if (!StartPreview()) {
                m_appState = AppState::Stopped;
                UpdateControlStates();
            }
        } else {
            m_appState = AppState::Stopped;
            UpdateControlStates();
        }
    };

    wxString errorMsg;
    LoggerConfig config = BuildConfig(true /* forRecording = true */, errorMsg);
    if (!errorMsg.IsEmpty()) {
        wxMessageBox(errorMsg, TrWx(StringId::ConfigErrorTitle), wxOK | wxICON_ERROR, this);
        restorePreviewOrStop();
        return false;
    }

    // 3. 録音用ストリームを開く
    if (!m_audioMgr->OpenStream(config, m_loggerWorker)) {
        wxMessageBox(TrWx(StringId::ErrAudioStreamOpen), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        restorePreviewOrStop();
        return false;
    }

    // 4. ファイル保存ワーカーは一時ファイルへ出力する
    if (!m_loggerWorker->Start(config)) {
        m_audioMgr->StopStream();
        wxMessageBox(TrWx(StringId::ErrSaveFileCreate), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        restorePreviewOrStop();
        return false;
    }

    // 5. ストリーム開始
    if (!m_audioMgr->StartStream()) {
        m_audioMgr->StopStream();
        m_loggerWorker->Abort();
        wxMessageBox(TrWx(StringId::ErrAudioStreamStart), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        restorePreviewOrStop();
        return false;
    }


    m_activeConfig = config;
    m_hasActiveConfig = true;
    m_hasErrorStatus = false;
    m_lastRecordingSummary.hasSummary = false;

    // 波形パネル初期化
    m_waveformPanel->SetTimeReferenceMode(WaveformPanel::TimeReferenceMode::RecordStart);
    if (m_waveformPanel->IsHold()) {
        m_waveformPanel->SetHold(false);
        if (m_btnHold) {
            m_btnHold->SetLabel(TrWx(StringId::ButtonHoldWaveform));
            m_btnHold->SetBackgroundColour(wxNullColour);
            m_btnHold->SetForegroundColour(wxNullColour);
            m_btnHold->Refresh();
        }
    }
    m_waveformPanel->SetSampleRate(config.getTargetSampleRate());
    m_waveformPanel->SetChannelCount(config.channels);
    m_waveformPanel->Clear();

    m_lblDuration->SetLabel(TrFormatWx(StringId::DurationLiveFormat, wxT("00:00:00")));
    m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxT("0.00")));
    m_lblOverruns->SetLabel(TrWx(StringId::OverrunsNormal));
    m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
    m_lblUiDrops->SetLabel(TrFormatWx(StringId::UiDropsFormat, std::string("0")));

    m_appState = AppState::Recording;
    UpdateControlStates();

    if (!m_timer.IsRunning()) {
        m_timer.Start(30);
    }

    return true;
}

void MainFrame::OnPreviewStartStop(wxCommandEvent& /*event*/) {
    if (m_appState == AppState::Stopped) {
        StartPreview();
    } else if (m_appState == AppState::Previewing) {
        StopPreview();
    }
}

void MainFrame::OnStartStop(wxCommandEvent& /*event*/) {
    if (m_appState == AppState::Recording) {
        StopRecording(false);
    } else {
        StartRecording();
    }
}

bool MainFrame::WriteCompanionMetadata(const LoggerConfig& config, const DLogFileHeader& header) {
    std::filesystem::path finalPath = std::filesystem::u8path(config.outputFilePath + ".meta");
    std::filesystem::path tempPath = finalPath;
    tempPath += L".partial";

    std::error_code ec;
    for (unsigned int suffix = 1; std::filesystem::exists(tempPath, ec); ++suffix) {
        ec.clear();
        tempPath = finalPath;
        tempPath += L".partial." + std::to_wstring(suffix);
    }
    if (ec) return false;

    std::ofstream metaOut(tempPath, std::ios::out | std::ios::trunc);
    if (!metaOut.is_open()) return false;

    metaOut << "[DLOG_METADATA]\n";
    metaOut << "magic=DLOG\n";
    metaOut << "version=" << header.version << "\n";
    metaOut << "start_timestamp_utc=" << header.startTimestampUtc << "\n";
    metaOut << "channels=" << header.channels << "\n";
    metaOut << "full_scale_voltage=" << header.fullScaleVoltage << "\n";
    metaOut << "sample_rate=" << header.sampleRate << "\n";
    metaOut << "native_rate=" << header.nativeRate << "\n";
    metaOut << "decimation_factor=" << header.decimationFactor << "\n";
    metaOut << "anti_aliasing=" << (config.enableAntiAliasing ? "true" : "false") << "\n\n";
    for (int i = 0; i < config.channels; ++i) {
        std::string chName = (i < static_cast<int>(config.channelMetadata.size()) &&
                              !config.channelMetadata[i].name.empty())
            ? config.channelMetadata[i].name : ("CH" + std::to_string(i + 1));
        std::string chUnit = (i < static_cast<int>(config.channelMetadata.size()) &&
                              !config.channelMetadata[i].unit.empty())
            ? config.channelMetadata[i].unit : "V";
        metaOut << "[CH" << (i + 1) << "]\n";
        metaOut << "name=" << chName << "\n";
        metaOut << "unit=" << chUnit << "\n\n";
    }
    metaOut.flush();
    bool writeOk = metaOut.good();
    metaOut.close();
    if (!writeOk) {
        std::filesystem::remove(tempPath, ec);
        return false;
    }

#ifdef _WIN32
    ec.clear();
    const bool targetExists = std::filesystem::exists(finalPath, ec);
    if (ec) return false;
    if (targetExists) {
        return ReplaceFileW(finalPath.c_str(), tempPath.c_str(), nullptr,
                            REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != 0;
    }
    return MoveFileExW(tempPath.c_str(), finalPath.c_str(), MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(tempPath, finalPath, ec);
    return !ec;
#endif
}

void MainFrame::StopRecording(bool isError, wxString errorMsg, bool resumePreview) {
    // 1. まずストリームとワーカーを停止
    m_audioMgr->StopStream();
    m_loggerWorker->Stop();

    // 2. 停止後に確定した最終統計と書込ヘッダを取得 (DATA-001 / STATE-001)
    LoggerStats loggerStats = m_loggerWorker->GetStats();
    LoggerStats audioStats = m_audioMgr->GetStats();
    DLogFileHeader writtenHeader = m_loggerWorker->GetWrittenHeader();

    if (m_loggerWorker->HasDiskError()) {
        isError = true;
        wxString recovery = wxString::FromUTF8(m_loggerWorker->GetRecoveryFilePath().c_str());
        errorMsg = TrFormatWx(StringId::ErrDiskWriteRecoveryFormat, recovery);
    } else if (m_hasActiveConfig && !WriteCompanionMetadata(m_activeConfig, writtenHeader)) {
        isError = true;
        errorMsg = TrWx(StringId::ErrMetaWriteFailed);
    }


    m_lastRecordingSummary.hasSummary = true;
    m_lastRecordingSummary.isError = isError;
    m_lastRecordingSummary.durationSec = loggerStats.wallClockDurationSec > 0.0 ? loggerStats.wallClockDurationSec : loggerStats.currentDurationSec;
    m_lastRecordingSummary.dataDurationSec = loggerStats.dataDurationSec;
    m_lastRecordingSummary.totalBytesWritten = loggerStats.totalBytesWritten;
    m_lastRecordingSummary.totalOverruns = loggerStats.bufferOverruns + audioStats.bufferOverruns;
    m_lastRecordingSummary.finalPath = m_hasActiveConfig ? m_activeConfig.outputFilePath : "";
    m_lastRecordingSummary.recoveryPath = m_loggerWorker->GetRecoveryFilePath();

    if (!isError) {
        // 次回録音用にファイル名を自動インクリメント (絶対ディレクトリパスを維持)
        wxFileName curFn(m_txtFilePath->GetValue());
        wxString dir = curFn.GetPath();
        if (dir.IsEmpty()) {
            dir = GetDefaultSaveDirectory();
        }
        wxDateTime now = wxDateTime::Now();
        wxString nextName = wxString::Format(wxT("dlog_%s.bin"), now.Format(wxT("%Y%m%d_%H%M%S")));
        wxFileName nextFn(dir, nextName);
        m_txtFilePath->SetValue(nextFn.GetFullPath());
    }
    m_hasActiveConfig = false;

    if (isError && !errorMsg.IsEmpty()) {
        wxMessageBox(errorMsg, TrWx(StringId::ErrRecordingGeneral), wxOK | wxICON_ERROR, this);
    }

    // プレビュー再開の試行 (記録開始時にプレビュー状態だった場合のみ復帰)
    bool shouldResumePreview = resumePreview && m_wasPreviewingBeforeRecord && !m_audioMgr->HasDeviceError();
    m_wasPreviewingBeforeRecord = false;
    if (shouldResumePreview) {
        if (StartPreview()) {
            return;
        }
    }

    m_appState = AppState::Stopped;
    m_hasErrorStatus = isError;
    UpdateControlStates();

    if (isError) {
        m_lblStatus->SetLabel(TrWx(StringId::StatusStoppedError));
        m_lblStatus->SetForegroundColour(wxColour(220, 40, 40));
    } else {
        m_lblStatus->SetLabel(TrWx(StringId::StatusRecordingComplete));
        m_lblStatus->SetForegroundColour(wxColour(120, 120, 120));
    }

    if (m_lastRecordingSummary.hasSummary) {
        int tot = static_cast<int>(m_lastRecordingSummary.durationSec);
        int h = tot / 3600;
        int m = (tot % 3600) / 60;
        int s = tot % 60;
        double mb = static_cast<double>(m_lastRecordingSummary.totalBytesWritten) / (1024.0 * 1024.0);
        m_lblDuration->SetLabel(TrFormatWx(StringId::DurationPrevFormat, wxString::Format(wxT("%02d:%02d:%02d (%.2f MB)"), h, m, s, mb)));
        if (m_lastRecordingSummary.isError) {
            m_lblFileSize->SetLabel(TrWx(StringId::StatusAbortedPartialKept));
        } else {
            m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxString::Format(wxT("%.2f"), mb)));
        }
        if (m_lastRecordingSummary.totalOverruns > 0) {
            m_lblOverruns->SetLabel(TrFormatWx(StringId::OverrunsWarningFormat, std::to_string(m_lastRecordingSummary.totalOverruns)));
            m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
        } else {
            m_lblOverruns->SetLabel(TrWx(StringId::OverrunsNormal));
            m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
        }
    }
}

void MainFrame::UpdateControlStates() {
    bool hasRates = !m_availableSampleRates.empty();

    if (m_appState == AppState::Stopped) {
        m_choiceDevice->Enable();
        m_btnRefreshDevices->Enable();
        m_choiceChannels->Enable();
        if (m_btnChannelConfig) m_btnChannelConfig->Enable();
        if (hasRates) {
            m_choiceSampleRate->Enable();
            m_btnPreview->Enable();
            m_btnStartStop->Enable();
        } else {
            m_choiceSampleRate->Disable();
            m_btnPreview->Disable();
            m_btnStartStop->Disable();
        }
        m_choiceDecimation->Enable();
        if (m_chkAntiAliasing) {
            if (m_choiceDecimation->GetSelection() == 0) {
                m_chkAntiAliasing->Disable();
            } else {
                m_chkAntiAliasing->Enable();
            }
        }
        if (m_txtVoltageScale) m_txtVoltageScale->Enable();
        if (m_btnCalibrate) m_btnCalibrate->Enable();
        m_txtFilePath->Enable();
        m_btnBrowse->Enable();

        m_btnPreview->SetLabel(TrWx(StringId::ButtonPreviewStart));
        m_btnPreview->SetBackgroundColour(wxColour(0, 120, 180));
        m_btnStartStop->SetLabel(TrWx(StringId::ButtonRecordStart));
        m_btnStartStop->SetBackgroundColour(wxColour(30, 160, 60));

        if (!m_hasErrorStatus) {
            if (m_lastRecordingSummary.hasSummary) {
                m_lblStatus->SetLabel(TrWx(StringId::StatusRecordingComplete));
            } else {
                m_lblStatus->SetLabel(TrWx(StringId::StatusStopped));
            }
            m_lblStatus->SetForegroundColour(wxColour(120, 120, 120));
        }
    } else if (m_appState == AppState::Previewing) {
        // プレビュー中: 入力設定はロック、ファイル保存設定は変更可能
        m_choiceDevice->Disable();
        m_btnRefreshDevices->Disable();
        m_choiceChannels->Disable();
        if (m_btnChannelConfig) m_btnChannelConfig->Disable();
        m_choiceSampleRate->Disable();
        m_choiceDecimation->Disable();
        if (m_chkAntiAliasing) m_chkAntiAliasing->Disable();
        if (m_txtVoltageScale) m_txtVoltageScale->Enable();
        if (m_btnCalibrate) m_btnCalibrate->Enable();
        m_txtFilePath->Enable();
        m_btnBrowse->Enable();

        m_btnPreview->Enable();
        m_btnPreview->SetLabel(TrWx(StringId::ButtonPreviewStop));
        m_btnPreview->SetBackgroundColour(wxColour(220, 120, 30));

        m_btnStartStop->Enable();
        m_btnStartStop->SetLabel(TrWx(StringId::ButtonRecordStart));
        m_btnStartStop->SetBackgroundColour(wxColour(30, 160, 60));

        m_lblStatus->SetLabel(TrWx(StringId::StatusPreviewing));
        m_lblStatus->SetForegroundColour(wxColour(30, 120, 200));
    } else if (m_appState == AppState::Recording) {
        // 記録中: 入力設定およびファイル設定をすべてロック
        m_choiceDevice->Disable();
        m_btnRefreshDevices->Disable();
        m_choiceChannels->Disable();
        if (m_btnChannelConfig) m_btnChannelConfig->Disable();
        m_choiceSampleRate->Disable();
        m_choiceDecimation->Disable();
        if (m_chkAntiAliasing) m_chkAntiAliasing->Disable();
        if (m_txtVoltageScale) m_txtVoltageScale->Disable();
        if (m_btnCalibrate) m_btnCalibrate->Disable();
        m_txtFilePath->Disable();
        m_btnBrowse->Disable();

        m_btnPreview->Disable();
        m_btnPreview->SetLabel(TrWx(StringId::ButtonPreviewStart));
        m_btnPreview->SetBackgroundColour(wxColour(0, 120, 180));

        m_btnStartStop->Enable();
        m_btnStartStop->SetLabel(TrWx(StringId::ButtonRecordStop));
        m_btnStartStop->SetBackgroundColour(wxColour(220, 40, 40));

        m_lblStatus->SetLabel(TrWx(StringId::StatusRecording));
        m_lblStatus->SetForegroundColour(wxColour(220, 40, 40));
    }

    Layout();
}

void MainFrame::OnTimer(wxTimerEvent& /*event*/) {
    if (m_modeNotebook && m_modeNotebook->GetSelection() == 1) {
        // 再生モードの処理
        if (m_playbackLoader.IsOpen()) {
            // 1. バックグラウンド読み込み結果のポーリング
            PlaybackWindowData winData;
            if (m_playbackLoader.PollResult(winData)) {
                if (winData.hasError) {
                    m_isPlaying = false;
                    m_hasPlaybackError = true;
                    m_playbackErrorMessage = winData.errorMessage;
                    UpdatePlaybackUI();
                } else {
                    m_hasPlaybackError = false;
                    m_playbackErrorMessage.clear();
                    m_waveformPanel->SetPlaybackWindow(winData);
                    m_waveformPanel->Refresh(false);
                }
            }


            // 2. 再生進行
            if (m_isPlaying) {
                double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());
                double curPos = GetCurrentPlaybackPosition();
                if (curPos >= maxPos) {
                    curPos = maxPos;
                    m_isPlaying = false;
                    m_isPlaybackFinished = true;
                    m_anchorPosition = maxPos;
                }
                RequestPlaybackWindow(curPos);
                UpdatePlaybackUI();
            } else {
                // 停止中でもウィンドウ幅が変わった場合は再要求
                int curPixelWidth = m_waveformPanel->GetPlotPixelWidth();
                if (curPixelWidth != m_lastRequestedPixelWidth && m_lastRequestedCenterTime >= 0.0) {
                    RequestPlaybackWindow(m_lastRequestedCenterTime);
                }
            }
        }
        return;
    }

    if (m_audioMgr->IsStreaming()) {
        // 1. オーディオデバイス切断・ストリーム異常停止の監視（USB切断等: プレビュー・記録共通）
        if (m_audioMgr->HasDeviceError()) {
            if (m_appState == AppState::Recording) {
                StopRecording(true, TrWx(StringId::ErrDeviceDisconnectedRecord), false /* resumePreview = false */);
            } else {
                StopPreview(true, TrWx(StringId::ErrDeviceDisconnectedPreview));
            }
            RefreshDeviceList();
            return;
        }

        // 2. ディスク書き込みエラーの監視（記録中のみ）
        if (m_appState == AppState::Recording && m_loggerWorker->HasDiskError()) {
            StopRecording(true, TrWx(StringId::ErrDiskWriteDuringRecord), true /* attempt resume preview if device ok */);
            return;
        }

        // 3. UI用キューからデータを取り出して波形パネルへプッシュ
        AudioBlock block;
        bool hasBlock = false;
        int blockCount = 0;
        while (m_audioMgr->TryDequeueUIBlock(block)) {
            hasBlock = true;
            m_waveformPanel->PushAudioBlock(block);
            blockCount++;
            if (blockCount >= 16) {
                // UIが遅延している場合、古いフレームを抜いて最新へ追従
                break;
            }
        }

        if (hasBlock) {
            m_waveformPanel->Refresh(false);
        }

        // 4. 統計情報の取得・表示
        LoggerStats audioStats = m_audioMgr->GetStats();

        if (m_appState == AppState::Recording) {
            LoggerStats loggerStats = m_loggerWorker->GetStats();

            // 経過時間
            int totalSec = static_cast<int>(loggerStats.currentDurationSec);
            int hours = totalSec / 3600;
            int mins = (totalSec % 3600) / 60;
            int secs = totalSec % 60;
            m_lblDuration->SetLabel(TrFormatWx(StringId::DurationLiveFormat, wxString::Format(wxT("%02d:%02d:%02d"), hours, mins, secs)));

            // ファイルサイズ (MB / GB)
            double mb = static_cast<double>(loggerStats.totalBytesWritten) / (1024.0 * 1024.0);
            if (mb >= 1024.0) {
                m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatGb, wxString::Format(wxT("%.2f"), mb / 1024.0)));
            } else {
                m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxString::Format(wxT("%.2f"), mb)));
            }

            // 欠落（Overrun）監視
            uint32_t totalOverruns = loggerStats.bufferOverruns + audioStats.bufferOverruns;
            if (totalOverruns == 0) {
                m_lblOverruns->SetLabel(TrWx(StringId::OverrunsNormal));
                m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
            } else {
                m_lblOverruns->SetLabel(TrFormatWx(StringId::OverrunsWarningFormat, std::to_string(totalOverruns)));
                m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
            }
        } else if (m_appState == AppState::Previewing) {
            if (m_lastRecordingSummary.hasSummary) {
                int tot = static_cast<int>(m_lastRecordingSummary.durationSec);
                int h = tot / 3600;
                int m = (tot % 3600) / 60;
                int s = tot % 60;
                double mb = static_cast<double>(m_lastRecordingSummary.totalBytesWritten) / (1024.0 * 1024.0);
                m_lblDuration->SetLabel(TrFormatWx(StringId::DurationPrevFormat, wxString::Format(wxT("%02d:%02d:%02d (%.2f MB)"), h, m, s, mb)));
                if (m_lastRecordingSummary.isError) {
                    m_lblFileSize->SetLabel(TrWx(StringId::StatusAbortedPartialKept));
                } else {
                    m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxString::Format(wxT("%.2f"), mb)));
                }
            } else {
                m_lblDuration->SetLabel(TrFormatWx(StringId::DurationLiveFormat, wxT("--:--:--")));
                m_lblFileSize->SetLabel(TrWx(StringId::FileSizeNone));
            }

            uint32_t previewOverruns = audioStats.bufferOverruns;
            if (previewOverruns == 0) {
                if (m_lastRecordingSummary.hasSummary && m_lastRecordingSummary.totalOverruns > 0) {
                    m_lblOverruns->SetLabel(TrFormatWx(StringId::PreviewDropoutsNormalWithPrev, std::to_string(m_lastRecordingSummary.totalOverruns)));
                    m_lblOverruns->SetForegroundColour(wxColour(220, 120, 20));
                } else {
                    m_lblOverruns->SetLabel(TrWx(StringId::PreviewDropoutsNormal));
                    m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
                }
            } else {
                if (m_lastRecordingSummary.hasSummary && m_lastRecordingSummary.totalOverruns > 0) {
                    m_lblOverruns->SetLabel(TrFormatWx(StringId::PreviewDropoutsWarningWithPrev, std::to_string(previewOverruns), std::to_string(m_lastRecordingSummary.totalOverruns)));
                    m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
                } else {
                    m_lblOverruns->SetLabel(TrFormatWx(StringId::PreviewDropoutsWarning, std::to_string(previewOverruns)));
                    m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
                }
            }
        }

        // UI描画ドロップ数
        m_lblUiDrops->SetLabel(TrFormatWx(StringId::UiDropsFormat, std::to_string(audioStats.uiFramesDropped)));
    }
}

void MainFrame::OnClose(wxCloseEvent& event) {
    if (m_activeWavExportWorker && m_activeWavExportWorker->IsRunning()) {
        int res = wxMessageBox(TrWx(StringId::WavExportConfirmCancel),
                               TrWx(StringId::ConfirmDialogTitle), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, this);
        if (res != wxYES) {
            event.Veto();
            return;
        }
        m_activeWavExportWorker->RequestCancel();
        event.Veto();
        return;
    }

    if (m_appState == AppState::Recording) {
        int res = wxMessageBox(TrWx(StringId::ConfirmCloseRecording),
                               TrWx(StringId::ConfirmDialogTitle), wxYES_NO | wxICON_QUESTION, this);
        if (res != wxYES) {
            event.Veto();
            return;
        }
        StopRecording(false, wxEmptyString, false /* resumePreview = false */);
    } else if (m_appState == AppState::Previewing) {
        StopPreview();
    }
    if (m_isPlaying) {
        PausePlayback();
    }
    m_playbackLoader.Stop();
    if (m_timer.IsRunning()) {
        m_timer.Stop();
    }
    Destroy();
}

void MainFrame::OnModeChanging(wxBookCtrlEvent& event) {
    if (m_isInitializing) return;

    if (m_appState == AppState::Recording) {
        wxMessageBox(TrWx(StringId::ErrCannotSwitchModeWhileRecording),
                     TrWx(StringId::WarningDialogTitle), wxOK | wxICON_WARNING, this);
        event.Veto();
        return;
    }

    // プレビュー中に再生タブへ切り替える場合はプレビューを安全停止
    if (event.GetSelection() == 1 && m_appState == AppState::Previewing) {
        StopPreview(false);
    }
}

void MainFrame::OnModeChanged(wxBookCtrlEvent& event) {
    if (m_isInitializing) return;
    if (!m_waveformPanel) return;

    int sel = event.GetSelection();
    if (sel == 1) {
        // 再生モード
        m_waveformPanel->SetPlaybackMode(true);
        if (m_btnHold) m_btnHold->Show(false);
        if (m_btnAcGuide) m_btnAcGuide->Show(false);
        if (m_waveOptSizer) m_waveOptSizer->Layout();

        if (m_playbackLoader.IsOpen()) {
            m_waveformPanel->SetChannelCount(m_playbackLoader.GetChannels());
            m_waveformPanel->SetChannelMetadata(m_playbackLoader.GetChannelMetadata());
            m_waveformPanel->SetFullScaleVoltage(m_playbackLoader.GetFullScaleVoltage());
            RequestPlaybackWindow(GetCurrentPlaybackPosition());
        } else {
            m_waveformPanel->Clear();
            if (m_lblStatus) {
                m_lblStatus->SetLabel(TrWx(StringId::StatusPausedEndOfFile));
                m_lblStatus->SetForegroundColour(wxColour(120, 120, 120));
            }
            if (m_lblDuration) m_lblDuration->SetLabel(TrFormatWx(StringId::DurationLiveFormat, wxT("--:--:--")));
            if (m_lblFileSize) m_lblFileSize->SetLabel(TrWx(StringId::FileSizeNone));
            if (m_lblOverruns) m_lblOverruns->SetLabel(wxEmptyString);
            if (m_lblUiDrops) m_lblUiDrops->SetLabel(wxEmptyString);
        }
        UpdatePlaybackUI();
    } else {
        // ライブ入力モード
        if (m_isPlaying) {
            PausePlayback();
        }
        m_waveformPanel->SetPlaybackMode(false);
        m_waveformPanel->SetChannelCount(GetSelectedChannelCount());
        m_waveformPanel->SetChannelMetadata(m_channelMetadata);
        m_waveformPanel->SetFullScaleVoltage(GetSelectedFullScaleVoltage());
        if (m_btnHold) m_btnHold->Show(true);
        if (m_btnAcGuide) m_btnAcGuide->Show(true);
        if (m_waveOptSizer) m_waveOptSizer->Layout();
        if (m_lastRecordingSummary.hasSummary) {
            int tot = static_cast<int>(m_lastRecordingSummary.durationSec);
            int h = tot / 3600;
            int m = (tot % 3600) / 60;
            int s = tot % 60;
            double mb = static_cast<double>(m_lastRecordingSummary.totalBytesWritten) / (1024.0 * 1024.0);
            if (m_lblDuration) {
                m_lblDuration->SetLabel(TrFormatWx(StringId::DurationPrevFormat, wxString::Format(wxT("%02d:%02d:%02d (%.2f MB)"), h, m, s, mb)));
            }
            if (m_lblFileSize) {
                if (m_lastRecordingSummary.isError) {
                    m_lblFileSize->SetLabel(TrWx(StringId::StatusAbortedPartialKept));
                } else {
                    m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxString::Format(wxT("%.2f"), mb)));
                }
            }
            if (m_lblOverruns) {
                if (m_lastRecordingSummary.totalOverruns > 0) {
                    m_lblOverruns->SetLabel(TrFormatWx(StringId::OverrunsWarningFormat, std::to_string(m_lastRecordingSummary.totalOverruns)));
                    m_lblOverruns->SetForegroundColour(wxColour(220, 40, 40));
                } else {
                    m_lblOverruns->SetLabel(TrWx(StringId::OverrunsNormal));
                    m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
                }
            }
        } else {
            if (m_lblDuration) m_lblDuration->SetLabel(TrFormatWx(StringId::DurationLiveFormat, wxT("00:00:00")));
            if (m_lblFileSize) m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxT("0.00")));
            if (m_lblOverruns) {
                m_lblOverruns->SetLabel(TrWx(StringId::OverrunsNormal));
                m_lblOverruns->SetForegroundColour(wxColour(30, 150, 30));
            }
        }
        if (m_lblUiDrops) m_lblUiDrops->SetLabel(TrFormatWx(StringId::UiDropsFormat, std::string("0")));
        UpdateControlStates();
    }
    m_waveformPanel->Refresh(false);
}

void MainFrame::OnPlayOpenFile(wxCommandEvent& /*event*/) {
    wxString defaultDir;
    wxString defaultFile;
    if (!m_txtPlaybackFile->GetValue().IsEmpty()) {
        wxFileName fn(m_txtPlaybackFile->GetValue());
        if (wxDirExists(fn.GetPath())) {
            defaultDir = fn.GetPath();
        }
    }
    if (defaultDir.IsEmpty() && !m_lastRecordingSummary.finalPath.empty()) {
        wxFileName fn(wxString::FromUTF8(m_lastRecordingSummary.finalPath.c_str()));
        if (wxDirExists(fn.GetPath())) {
            defaultDir = fn.GetPath();
            if (wxFileExists(fn.GetFullPath())) {
                defaultFile = fn.GetFullName();
            }
        }
    }
    if (defaultDir.IsEmpty() && m_txtFilePath) {
        wxFileName fn(m_txtFilePath->GetValue());
        if (wxDirExists(fn.GetPath())) {
            defaultDir = fn.GetPath();
        }
    }
    if (defaultDir.IsEmpty()) {
        defaultDir = GetDefaultSaveDirectory();
    }

    wxFileDialog openFileDialog(this, TrWx(StringId::ButtonOpenFile), defaultDir, defaultFile,
                                TrWx(StringId::FilterDlogFiles),
                                wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (openFileDialog.ShowModal() == wxID_CANCEL) return;
    wxString path = openFileDialog.GetPath();

    if (m_isPlaying) {
        PausePlayback();
    }

    std::string errMsg;
    LocalizedError openErr;
    if (!m_playbackLoader.OpenFile(std::string(path.utf8_str()), errMsg, &openErr)) {
        wxString displayMsg = openErr.hasError() ? wxString::FromUTF8(openErr.FormatCurrent().c_str()) : wxString::FromUTF8(errMsg.c_str());
        wxMessageBox(displayMsg, TrWx(StringId::ErrFileOpenTitle), wxOK | wxICON_ERROR, this);
        return;
    }

    m_txtPlaybackFile->SetValue(path);
    m_waveformPanel->SetChannelCount(m_playbackLoader.GetChannels());
    m_waveformPanel->SetChannelMetadata(m_playbackLoader.GetChannelMetadata());
    m_waveformPanel->SetFullScaleVoltage(m_playbackLoader.GetFullScaleVoltage());

    double totalDuration = m_playbackLoader.GetDurationSec();
    m_lblTotalDuration->SetLabel(wxString::Format(wxT("/ %s"),
        wxString::FromUTF8(FormatPlaybackTime(totalDuration).c_str())));

    m_anchorPosition = 0.0;
    m_anchorTime = std::chrono::steady_clock::now();
    m_isPlaying = false;
    m_isPlaybackFinished = false;

    SeekPlaybackTo(0.0);
    UpdatePlaybackUI();
}

void MainFrame::OnPlayFirst(wxCommandEvent& /*event*/) {
    if (!m_playbackLoader.IsOpen()) return;
    SeekPlaybackTo(0.0);
}

void MainFrame::OnPlayPause(wxCommandEvent& /*event*/) {
    if (!m_playbackLoader.IsOpen()) return;
    if (m_isPlaying) {
        PausePlayback();
    } else {
        double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());
        if (m_isPlaybackFinished || GetCurrentPlaybackPosition() >= maxPos - 1e-6) {
            SeekPlaybackTo(0.0);
        }
        ResumePlayback();
    }
}

void MainFrame::OnPlayLast(wxCommandEvent& /*event*/) {
    if (!m_playbackLoader.IsOpen()) return;
    double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());
    SeekPlaybackTo(maxPos);
}

void MainFrame::OnPlaySpeedSelect(wxCommandEvent& /*event*/) {
    static const double speeds[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
    int sel = m_choicePlaySpeed->GetSelection();
    if (sel >= 0 && sel < 7) {
        if (m_isPlaying) {
            m_anchorPosition = GetCurrentPlaybackPosition();
            m_anchorTime = std::chrono::steady_clock::now();
        }
        m_playbackSpeed = speeds[sel];
    }
}

void MainFrame::OnPlayTimeEnter(wxCommandEvent& event) {
    OnPlaySeek(event);
}

void MainFrame::OnPlaySeek(wxCommandEvent& /*event*/) {
    if (!m_playbackLoader.IsOpen()) return;

    wxString inputStr = m_txtPlayTime->GetValue().Trim().Trim(false);
    double Fs = m_playbackLoader.GetSampleRate();
    uint64_t totalFrames = m_playbackLoader.GetTotalFrames();
    double maxPos = CalculateMaxPosition(totalFrames, Fs);

    double alignedSec = 0.0;
    std::string errorMsg;
    if (!ParsePlaybackTimeString(std::string(inputStr.utf8_str()), maxPos, Fs, alignedSec, errorMsg)) {
        wxMessageBox(wxString::FromUTF8(errorMsg.c_str()), TrWx(StringId::ErrPlaybackSeekTitle), wxOK | wxICON_WARNING, this);
        return;
    }

    SeekPlaybackTo(alignedSec);
}

void MainFrame::OnPlaySliderThumbTrack(wxScrollEvent& event) {
    if (!m_playbackLoader.IsOpen()) return;
    if (!m_isSliderDragging) {
        m_isSliderDragging = true;
        m_wasPlayingBeforeDrag = m_isPlaying;
        if (m_isPlaying) {
            PausePlayback();
        }
    }
    int sliderVal = event.GetPosition();
    double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());
    double targetSec = (static_cast<double>(sliderVal) / 10000.0) * maxPos;
    SeekPlaybackTo(targetSec);
}

void MainFrame::OnPlaySliderThumbRelease(wxScrollEvent& /*event*/) {
    if (!m_playbackLoader.IsOpen()) return;
    m_isSliderDragging = false;
    if (m_wasPlayingBeforeDrag) {
        ResumePlayback();
    }
}

void MainFrame::OnPlaySliderScroll(wxScrollEvent& event) {
    if (!m_playbackLoader.IsOpen()) return;
    if (m_isSliderDragging) return;
    int sliderVal = event.GetPosition();
    double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());
    double targetSec = (static_cast<double>(sliderVal) / 10000.0) * maxPos;
    SeekPlaybackTo(targetSec);
}

void MainFrame::PausePlayback() {
    if (!m_isPlaying) return;
    m_anchorPosition = GetCurrentPlaybackPosition();
    m_isPlaying = false;
    UpdatePlaybackUI();
}

void MainFrame::ResumePlayback() {
    if (!m_playbackLoader.IsOpen()) return;
    m_hasPlaybackError = false;
    m_playbackErrorMessage.clear();
    double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());
    if (m_anchorPosition >= maxPos - 1e-6) {
        m_anchorPosition = 0.0;
    }
    m_isPlaying = true;
    m_isPlaybackFinished = false;
    m_anchorTime = std::chrono::steady_clock::now();
    RequestPlaybackWindow(m_anchorPosition, true /* isNewEpoch */);
    UpdatePlaybackUI();
}

void MainFrame::SeekPlaybackTo(double timeSec) {
    if (!m_playbackLoader.IsOpen()) return;
    m_hasPlaybackError = false;
    m_playbackErrorMessage.clear();
    double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());
    timeSec = std::clamp(timeSec, 0.0, maxPos);
    m_anchorPosition = timeSec;
    m_anchorTime = std::chrono::steady_clock::now();
    m_isPlaybackFinished = (timeSec >= maxPos - 1e-6);
    RequestPlaybackWindow(timeSec, true /* isNewEpoch */);
    UpdatePlaybackUI();
}


double MainFrame::GetCurrentPlaybackPosition() const {
    if (!m_playbackLoader.IsOpen()) return 0.0;
    double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());
    if (!m_isPlaying) return m_anchorPosition;
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - m_anchorTime).count();
    double pos = m_anchorPosition + elapsed * m_playbackSpeed;
    if (pos >= maxPos) return maxPos;
    return pos;
}

void MainFrame::RequestPlaybackWindow(double centerTimeSec, bool isNewEpoch) {
    if (!m_playbackLoader.IsOpen()) return;
    int pixelWidth = m_waveformPanel->GetPlotPixelWidth();
    double win = m_waveformPanel->GetTimeWindow();
    m_lastRequestedPixelWidth = pixelWidth;
    m_lastRequestedGen = m_playbackLoader.RequestWindow(centerTimeSec, win, pixelWidth, isNewEpoch);
    m_lastRequestedCenterTime = centerTimeSec;
}

void MainFrame::UpdatePlaybackUI() {
    bool opened = m_playbackLoader.IsOpen();
    m_btnPlayFirst->Enable(opened);
    m_btnPlayPause->Enable(opened);
    m_btnPlayLast->Enable(opened);
    m_choicePlaySpeed->Enable(opened);
    m_txtPlayTime->Enable(opened);
    m_btnSeekTime->Enable(opened);
    m_sliderPlayback->Enable(opened);
    if (m_btnExportWav) m_btnExportWav->Enable(opened);

    if (opened) {
        double curPos = GetCurrentPlaybackPosition();
        double maxPos = CalculateMaxPosition(m_playbackLoader.GetTotalFrames(), m_playbackLoader.GetSampleRate());

        if (!m_txtPlayTime->HasFocus()) {
            m_txtPlayTime->SetValue(wxString::FromUTF8(FormatPlaybackTime(curPos).c_str()));
        }

        if (!m_isSliderDragging && maxPos > 0.0) {
            int sliderVal = static_cast<int>(std::round((curPos / maxPos) * 10000.0));
            m_sliderPlayback->SetValue(std::clamp(sliderVal, 0, 10000));
        }

        if (m_hasPlaybackError) {
            m_btnPlayPause->SetLabel(TrWx(StringId::ButtonPlayStart));
            m_btnPlayPause->SetBackgroundColour(wxColour(30, 160, 60));
            m_lblStatus->SetLabel(TrFormatWx(StringId::StatusErrorFormat, wxString::FromUTF8(m_playbackErrorMessage.c_str())));
            m_lblStatus->SetForegroundColour(wxColour(220, 40, 40));
        } else if (m_isPlaying) {
            m_btnPlayPause->SetLabel(TrWx(StringId::ButtonPlayPause));
            m_btnPlayPause->SetBackgroundColour(wxColour(220, 120, 30));
            m_lblStatus->SetLabel(TrWx(StringId::StatusPlaying));
            m_lblStatus->SetForegroundColour(wxColour(30, 160, 60));
        } else if (m_isPlaybackFinished) {
            m_btnPlayPause->SetLabel(TrWx(StringId::ButtonPlayStart));
            m_btnPlayPause->SetBackgroundColour(wxColour(30, 160, 60));
            m_lblStatus->SetLabel(TrWx(StringId::StatusPlaybackFinished));
            m_lblStatus->SetForegroundColour(wxColour(120, 120, 120));
        } else {
            m_btnPlayPause->SetLabel(TrWx(StringId::ButtonPlayStart));
            m_btnPlayPause->SetBackgroundColour(wxColour(30, 160, 60));
            m_lblStatus->SetLabel(TrWx(StringId::StatusPaused));
            m_lblStatus->SetForegroundColour(wxColour(120, 120, 120));
        }


        m_lblDuration->SetLabel(wxString::Format(wxT("%s: %s"),
            TrWx(StringId::LabelPlayPosition),
            wxString::FromUTF8(FormatPlaybackTime(curPos).c_str())));
        double mb = static_cast<double>(m_playbackLoader.GetFileSize()) / (1024.0 * 1024.0);
        if (mb >= 1024.0) {
            m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatGb, wxString::Format(wxT("%.2f"), mb / 1024.0)));
        } else {
            m_lblFileSize->SetLabel(TrFormatWx(StringId::FileSizeFormatMb, wxString::Format(wxT("%.2f"), mb)));
        }
        m_lblOverruns->SetLabel(wxEmptyString);
        m_lblUiDrops->SetLabel(wxEmptyString);
    } else {
        m_btnPlayPause->SetLabel(TrWx(StringId::ButtonPlayStart));
        m_btnPlayPause->SetBackgroundColour(wxColour(30, 160, 60));
    }
}

void MainFrame::OnPlayExportWav(wxCommandEvent& /*event*/) {
    if (!m_playbackLoader.IsOpen()) return;

    if (m_isPlaying) {
        PausePlayback();
    }

    std::vector<std::string> chLabels;
    for (const auto& meta : m_playbackLoader.GetChannelMetadata()) {
        chLabels.push_back(meta.name);
    }

    WavExportDialog dlg(this, m_playbackLoader.GetHeader(), m_playbackLoader.GetTotalFrames(),
                        m_playbackLoader.GetFilePath(), chLabels);
    if (dlg.ShowModal() != wxID_OK) {
        return;
    }

    WavExportOptions options = dlg.GetOptions();

    WavExportWorker exportWorker;
    m_activeWavExportWorker = &exportWorker;

    struct WorkerGuard {
        WavExportWorker*& target;
        ~WorkerGuard() { target = nullptr; }
    } guard{m_activeWavExportWorker};

    wxProgressDialog progressDlg(
        TrWx(StringId::WavExportProgressTitle),
        wxEmptyString,
        1000,
        this,
        wxPD_APP_MODAL | wxPD_AUTO_HIDE | wxPD_CAN_ABORT | wxPD_ELAPSED_TIME | wxPD_REMAINING_TIME
    );

    auto startTime = std::chrono::steady_clock::now();
    bool started = exportWorker.Start(options);
    if (!started) {
        wxMessageBox(TrWx(StringId::ErrWavGeneral), TrWx(StringId::WavExportErrorTitle), wxOK | wxICON_ERROR, this);
        return;
    }

    bool userCancelConfirmed = false;
    while (exportWorker.IsRunning()) {
        wxMilliSleep(50);
        wxTheApp->Yield();

        double progressRatio = exportWorker.GetProgress();
        uint64_t processed = exportWorker.GetProcessedFrames();
        uint64_t total = exportWorker.GetTotalFrames();

        int permille = static_cast<int>(std::round(progressRatio * 1000.0));
        permille = std::clamp(permille, 0, 999);

        auto now = std::chrono::steady_clock::now();
        double elapsedSec = std::chrono::duration<double>(now - startTime).count();
        double etaSec = (progressRatio > 0.01) ? (elapsedSec / progressRatio - elapsedSec) : 0.0;
        char etaBuf[32];
        int etaM = static_cast<int>(etaSec) / 60;
        int etaS = static_cast<int>(etaSec) % 60;
        std::snprintf(etaBuf, sizeof(etaBuf), "%02d:%02d", etaM, etaS);

        char pctBuf[16];
        std::snprintf(pctBuf, sizeof(pctBuf), "%.1f", progressRatio * 100.0);

        std::string progMsg = TrFormat(StringId::WavExportProgressMsgFormat, {
            pctBuf,
            std::to_string(processed),
            std::to_string(total),
            std::string(etaBuf)
        });

        bool keepGoing = progressDlg.Update(permille, wxString::FromUTF8(progMsg.c_str()));
        if (!keepGoing && !userCancelConfirmed) {
            int res = wxMessageBox(TrWx(StringId::WavExportConfirmCancel),
                                   TrWx(StringId::ConfirmDialogTitle),
                                   wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION, &progressDlg);
            if (res == wxYES) {
                userCancelConfirmed = true;
                exportWorker.RequestCancel();
            } else {
                progressDlg.Resume();
            }
        }
    }

    exportWorker.Wait();
    progressDlg.Update(1000);

    auto endTime = std::chrono::steady_clock::now();
    double totalElapsedSec = std::chrono::duration<double>(endTime - startTime).count();
    char elapsedBuf[32];
    int elM = static_cast<int>(totalElapsedSec) / 60;
    int elS = static_cast<int>(totalElapsedSec) % 60;
    std::snprintf(elapsedBuf, sizeof(elapsedBuf), "%02d:%02d", elM, elS);

    WavExportResult result = exportWorker.GetResult();
    LocalizedError error = exportWorker.GetError();

    if (result.canceled) {
        wxMessageBox(TrWx(StringId::WavExportCanceled), TrWx(StringId::InfoDialogTitle), wxOK | wxICON_INFORMATION, this);
        return;
    }

    if (!result.success) {
        std::string errDetail = error.FormatCurrent();
        if (errDetail.empty()) {
            errDetail = Tr(StringId::ErrWavGeneral);
        }
        wxMessageBox(wxString::FromUTF8(errDetail.c_str()), TrWx(StringId::WavExportErrorTitle), wxOK | wxICON_ERROR, this);
        return;
    }

    char szBuf[64];
    double mb = static_cast<double>(result.totalBytesWritten) / (1024.0 * 1024.0);
    std::snprintf(szBuf, sizeof(szBuf), "%.2f MB", mb);

    if (result.pcm16ClippedSamples > 0) {
        std::string successMsg = TrFormat(StringId::WavExportSuccessWithClippingFormat, {
            std::to_string(result.pcm16ClippedSamples),
            result.destinationPath,
            std::to_string(result.outputFramesWritten),
            std::string(szBuf),
            std::string(elapsedBuf)
        });
        wxMessageBox(wxString::FromUTF8(successMsg.c_str()), TrWx(StringId::WavExportSuccessTitle), wxOK | wxICON_WARNING, this);
    } else {
        std::string successMsg = TrFormat(StringId::WavExportSuccessFormat, {
            result.destinationPath,
            std::to_string(result.outputFramesWritten),
            std::string(szBuf),
            std::string(elapsedBuf)
        });
        wxMessageBox(wxString::FromUTF8(successMsg.c_str()), TrWx(StringId::WavExportSuccessTitle), wxOK | wxICON_INFORMATION, this);
    }
}

} // namespace dlog

