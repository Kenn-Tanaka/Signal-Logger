#include "calibration_dialog.h"
#include "common/localized_strings.h"
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/msgdlg.h>
#include <iomanip>
#include <sstream>

namespace dlog {

wxBEGIN_EVENT_TABLE(CalibrationDialog, wxDialog)
    EVT_TIMER(ID_CAL_TIMER, CalibrationDialog::OnTimer)
    EVT_TEXT(ID_CAL_TARGET_VPP, CalibrationDialog::OnTargetVppChanged)
    EVT_CHOICE(ID_CAL_CHANNEL, CalibrationDialog::OnChannelSelected)
    EVT_BUTTON(ID_CAL_APPLY, CalibrationDialog::OnApply)
wxEND_EVENT_TABLE()

CalibrationDialog::CalibrationDialog(wxWindow* parent, WaveformPanel* waveformPanel, double currentVfs, int channelCount)
    : wxDialog(parent, wxID_ANY, TrWx(StringId::CalibrateDialogTitle), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_waveformPanel(waveformPanel),
      m_currentVfs(currentVfs),
      m_channelCount(std::max(1, channelCount)),
      m_timer(this, ID_CAL_TIMER)
{
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. 説明ヘッダ
    auto* headerDesc = new wxStaticText(this, wxID_ANY, TrWx(StringId::CalibrateHeaderDesc));
    headerDesc->Wrap(440);
    mainSizer->Add(headerDesc, 0, wxALL, 12);

    // 2. 設定行 (チャネル選択 & 目標 Vpp 入力)
    auto* inputGrid = new wxFlexGridSizer(2, 2, 8, 12);
    inputGrid->AddGrowableCol(1, 1);

    // チャネル選択
    inputGrid->Add(new wxStaticText(this, wxID_ANY, TrWx(StringId::CalibrateChannelLabel)), 0, wxALIGN_CENTER_VERTICAL);
    wxArrayString chChoices;
    for (int i = 0; i < m_channelCount; ++i) {
        chChoices.Add(wxString::Format(wxT("CH %d"), i + 1));
    }
    m_choiceChannel = new wxChoice(this, ID_CAL_CHANNEL, wxDefaultPosition, wxDefaultSize, chChoices);
    m_choiceChannel->SetSelection(0);
    inputGrid->Add(m_choiceChannel, 0, wxEXPAND);

    // 基準信号 Vpp [V] 入力
    inputGrid->Add(new wxStaticText(this, wxID_ANY, TrWx(StringId::CalibrateTargetVppLabel)), 0, wxALIGN_CENTER_VERTICAL);
    auto* vppSizer = new wxBoxSizer(wxHORIZONTAL);
    m_txtTargetVpp = new wxTextCtrl(this, ID_CAL_TARGET_VPP, wxT("1.000"), wxDefaultPosition, wxSize(100, -1));
    vppSizer->Add(m_txtTargetVpp, 0, wxALIGN_CENTER_VERTICAL);
    vppSizer->Add(new wxStaticText(this, wxID_ANY, wxT(" Vpp")), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);
    inputGrid->Add(vppSizer, 0, wxEXPAND);

    mainSizer->Add(inputGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // 3. リアルタイム信号監視グループ
    auto* monitorBox = new wxStaticBoxSizer(wxVERTICAL, this, TrWx(StringId::CalibrateMonitorGroup));
    auto* monitorGrid = new wxFlexGridSizer(4, 2, 6, 15);
    monitorGrid->AddGrowableCol(1, 1);

    // 実測デジタル Vpp
    monitorGrid->Add(new wxStaticText(monitorBox->GetStaticBox(), wxID_ANY, TrWx(StringId::CalibrateMeasuredVpp)), 0, wxALIGN_CENTER_VERTICAL);
    m_lblMeasuredVpp = new wxStaticText(monitorBox->GetStaticBox(), wxID_ANY, wxT("0.0000"));
    wxFont boldFont = m_lblMeasuredVpp->GetFont();
    boldFont.SetWeight(wxFONTWEIGHT_BOLD);
    m_lblMeasuredVpp->SetFont(boldFont);
    monitorGrid->Add(m_lblMeasuredVpp, 0, wxALIGN_CENTER_VERTICAL);

    // ピーク振幅
    monitorGrid->Add(new wxStaticText(monitorBox->GetStaticBox(), wxID_ANY, TrWx(StringId::CalibratePeakValue)), 0, wxALIGN_CENTER_VERTICAL);
    m_lblPeakValue = new wxStaticText(monitorBox->GetStaticBox(), wxID_ANY, wxT("0.0000"));
    monitorGrid->Add(m_lblPeakValue, 0, wxALIGN_CENTER_VERTICAL);

    // 計算フルスケール電圧
    monitorGrid->Add(new wxStaticText(monitorBox->GetStaticBox(), wxID_ANY, TrWx(StringId::CalibrateCalculatedVfs)), 0, wxALIGN_CENTER_VERTICAL);
    m_lblCalculatedVfs = new wxStaticText(monitorBox->GetStaticBox(), wxID_ANY, wxT("±1.0000 V"));
    m_lblCalculatedVfs->SetFont(boldFont);
    monitorGrid->Add(m_lblCalculatedVfs, 0, wxALIGN_CENTER_VERTICAL);

    // 信号品質ステータス
    monitorGrid->Add(new wxStaticText(monitorBox->GetStaticBox(), wxID_ANY, wxT("Status:")), 0, wxALIGN_CENTER_VERTICAL);
    m_lblStatus = new wxStaticText(monitorBox->GetStaticBox(), wxID_ANY, TrWx(StringId::CalibrateStatusNoSignal));
    monitorGrid->Add(m_lblStatus, 0, wxALIGN_CENTER_VERTICAL);

    monitorBox->Add(monitorGrid, 1, wxEXPAND | wxALL, 8);
    mainSizer->Add(monitorBox, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    // 4. ボタン行
    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    m_btnApply = new wxButton(this, ID_CAL_APPLY, TrWx(StringId::CalibrateBtnApply));
    m_btnCancel = new wxButton(this, wxID_CANCEL, TrWx(StringId::ButtonCancel));

    btnSizer->AddStretchSpacer(1);
    btnSizer->Add(m_btnApply, 0, wxRIGHT, 8);
    btnSizer->Add(m_btnCancel, 0);

    mainSizer->Add(btnSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    SetSizerAndFit(mainSizer);
    CenterOnParent();

    // 初回計算とタイマー開始 (100ms周期)
    UpdateCalculations();
    m_timer.Start(100);
}

CalibrationDialog::~CalibrationDialog() {
    if (m_timer.IsRunning()) {
        m_timer.Stop();
    }
}

void CalibrationDialog::OnTimer(wxTimerEvent& /*event*/) {
    UpdateCalculations();
}

void CalibrationDialog::OnTargetVppChanged(wxCommandEvent& /*event*/) {
    UpdateCalculations();
}

void CalibrationDialog::OnChannelSelected(wxCommandEvent& /*event*/) {
    if (m_choiceChannel) {
        m_selectedChannel = m_choiceChannel->GetSelection();
        if (m_selectedChannel < 0) m_selectedChannel = 0;
    }
    UpdateCalculations();
}

void CalibrationDialog::UpdateCalculations() {
    if (!m_waveformPanel) return;

    // 1. 直近1.0秒間の信号統計を取得
    SignalStats stats = m_waveformPanel->MeasureChannelStats(m_selectedChannel, 1.0);
    m_measuredVpp = stats.vpp;

    // 表示更新: 実測 Vpp & ピーク値
    if (m_lblMeasuredVpp) {
        m_lblMeasuredVpp->SetLabel(wxString::Format(wxT("%.4f"), stats.vpp));
    }
    if (m_lblPeakValue) {
        m_lblPeakValue->SetLabel(wxString::Format(wxT("%.4f"), stats.peakVal));
    }

    // ステータス表示と配色
    if (m_lblStatus) {
        switch (stats.status) {
            case CalibrationSignalStatus::Good:
                m_lblStatus->SetLabel(TrWx(StringId::CalibrateStatusGood));
                m_lblStatus->SetForegroundColour(wxColour(30, 150, 30)); // Green
                break;
            case CalibrationSignalStatus::LowSignal:
                m_lblStatus->SetLabel(TrWx(StringId::CalibrateStatusLow));
                m_lblStatus->SetForegroundColour(wxColour(200, 130, 0)); // Amber
                break;
            case CalibrationSignalStatus::Clipping:
                m_lblStatus->SetLabel(TrWx(StringId::CalibrateStatusClipping));
                m_lblStatus->SetForegroundColour(wxColour(220, 40, 40)); // Red
                break;
            case CalibrationSignalStatus::NoSignal:
            default:
                m_lblStatus->SetLabel(TrWx(StringId::CalibrateStatusNoSignal));
                m_lblStatus->SetForegroundColour(wxColour(120, 120, 120)); // Gray
                break;
        }
        m_lblStatus->Refresh();
    }

    // 2. 目標 Vpp の解析
    double targetVpp = 0.0;
    bool validTarget = false;
    if (m_txtTargetVpp) {
        wxString vStr = m_txtTargetVpp->GetValue().Trim().Trim(false);
        if (vStr.ToDouble(&targetVpp) && targetVpp > 0.0) {
            validTarget = true;
            m_targetVpp = targetVpp;
        }
    }

    // 3. フルスケール電圧 (V_FS) のリアルタイム計算
    double calVfs = 0.0;
    bool calcOk = validTarget && CalibrationCalculator::CalculateFullScale(targetVpp, stats.vpp, calVfs);

    if (calcOk) {
        m_calculatedVfs = calVfs;
        if (m_lblCalculatedVfs) {
            m_lblCalculatedVfs->SetLabel(wxString::Format(wxT("±%.4f V"), calVfs));
            m_lblCalculatedVfs->SetForegroundColour(wxColour(0, 0, 0));
        }
    } else {
        if (m_lblCalculatedVfs) {
            if (!validTarget) {
                m_lblCalculatedVfs->SetLabel(wxT("-- (Invalid Input)"));
            } else if (stats.status == CalibrationSignalStatus::Clipping) {
                m_lblCalculatedVfs->SetLabel(wxT("-- (Clipping)"));
            } else {
                m_lblCalculatedVfs->SetLabel(wxT("-- (No Signal)"));
            }
            m_lblCalculatedVfs->SetForegroundColour(wxColour(120, 120, 120));
        }
    }

    // 適用ボタンの有効化判定:
    // 目標値が有効、かつ信号ステータスが Good または LowSignal の場合に適用可能
    bool canApply = calcOk && (stats.status == CalibrationSignalStatus::Good || stats.status == CalibrationSignalStatus::LowSignal);
    if (m_btnApply) {
        m_btnApply->Enable(canApply);
    }
}

void CalibrationDialog::OnApply(wxCommandEvent& /*event*/) {
    // 最終バリデーション
    double targetVpp = 0.0;
    if (!m_txtTargetVpp || !m_txtTargetVpp->GetValue().ToDouble(&targetVpp) || targetVpp <= 0.0) {
        wxMessageBox(TrWx(StringId::CalibrateInvalidInput), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        return;
    }

    SignalStats stats = m_waveformPanel ? m_waveformPanel->MeasureChannelStats(m_selectedChannel, 1.0) : SignalStats{};
    if (stats.vpp < 0.01) {
        wxMessageBox(
            TrFormatWx(StringId::ErrCalibratePrompt, wxString::Format(wxT("%.4f"), stats.vpp)),
            TrWx(StringId::CalibrateErrorTitle), wxOK | wxICON_WARNING, this
        );
        return;
    }

    double calVfs = 0.0;
    if (!CalibrationCalculator::CalculateFullScale(targetVpp, stats.vpp, calVfs)) {
        wxMessageBox(TrWx(StringId::CalibrateInvalidInput), TrWx(StringId::ErrorDialogTitle), wxOK | wxICON_ERROR, this);
        return;
    }

    m_targetVpp = targetVpp;
    m_measuredVpp = stats.vpp;
    m_calculatedVfs = calVfs;

    EndModal(wxID_OK);
}

} // namespace dlog
