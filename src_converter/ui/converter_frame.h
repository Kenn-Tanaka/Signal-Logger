#pragma once

#include "converter_types.h"
#include "batch_converter_worker.h"

#include <wx/frame.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/radiobut.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/dnd.h>

#include <vector>
#include <memory>

namespace dlog {

class ConverterFrame : public wxFrame {
public:
    ConverterFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    ~ConverterFrame() override;

    void AddFiles(const wxArrayString& filePaths);

private:
    void InitUI();
    void UpdateListDisplay();
    void SetControlsEnabled(bool enabled);
    ConversionOptions GatherOptions();

    // イベントハンドラ
    void OnAddFiles(wxCommandEvent& event);
    void OnAddFolder(wxCommandEvent& event);
    void OnRemoveSelected(wxCommandEvent& event);
    void OnClearList(wxCommandEvent& event);
    void OnStartConversion(wxCommandEvent& event);
    void OnCancelConversion(wxCommandEvent& event);
    void OnOpenOutputDir(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnClose(wxCloseEvent& event);
    void OnBrowseOutputDir(wxCommandEvent& event);
    void OnOutputDirRadio(wxCommandEvent& event);
    void OnSplitCheck(wxCommandEvent& event);
    void OnSetExcelLimit(wxCommandEvent& event);
    void OnAllChannelsCheck(wxCommandEvent& event);
    void OnTimeRangeCheck(wxCommandEvent& event);
    void OnChannelConfig(wxCommandEvent& event);
    void OnRecoverPartial(wxCommandEvent& event);

    std::unique_ptr<BatchConverterWorker> m_worker;
    std::vector<BatchItem> m_items;

    // UI コントロール
    wxListCtrl* m_listCtrl = nullptr;
    wxButton* m_btnAddFiles = nullptr;
    wxButton* m_btnAddFolder = nullptr;
    wxButton* m_btnRemove = nullptr;
    wxButton* m_btnClear = nullptr;
    wxButton* m_btnRecoverPartial = nullptr;

    // オプション
    wxRadioButton* m_radioOutSameDir = nullptr;
    wxRadioButton* m_radioOutCustomDir = nullptr;
    wxTextCtrl* m_txtCustomOutputDir = nullptr;
    wxButton* m_btnBrowseOutputDir = nullptr;

    wxRadioButton* m_radioTimeRelSec = nullptr;
    wxRadioButton* m_radioTimeAbs = nullptr;
    wxRadioButton* m_radioTimeIndex = nullptr;
    wxRadioButton* m_radioTimeNone = nullptr;

    wxChoice* m_choiceDecimation = nullptr;
    wxChoice* m_choiceDelimiter = nullptr;
    wxChoice* m_choicePrecision = nullptr;
    wxCheckBox* m_chkApplyVoltageScale = nullptr;

    // 行数自動分割
    wxCheckBox* m_chkSplitLines = nullptr;
    wxSpinCtrl* m_spinMaxLines = nullptr;
    wxButton* m_btnExcelLimit = nullptr;

    // 出力チャネル選択 (MED-02, FUNC-003)
    wxCheckBox* m_chkAllChannels = nullptr;
    std::vector<wxCheckBox*> m_chkChannels;
    wxButton* m_btnChannelConfig = nullptr;
    std::vector<ChannelMetadata> m_channelMetadata;
    bool m_hasCustomChannelMetadata = false;

    // 時間範囲トリミング (MED-03)
    wxCheckBox* m_chkTimeRange = nullptr;
    wxTextCtrl* m_txtStartTime = nullptr;
    wxTextCtrl* m_txtEndTime = nullptr;

    // 進捗
    wxGauge* m_gaugeCurrent = nullptr;
    wxGauge* m_gaugeOverall = nullptr;
    wxStaticText* m_lblCurrentFile = nullptr;
    wxStaticText* m_lblOverallStatus = nullptr;
    wxStaticText* m_lblSpeedAndEta = nullptr;

    wxButton* m_btnStart = nullptr;
    wxButton* m_btnCancel = nullptr;
    wxButton* m_btnOpenOutputDir = nullptr;

    bool m_isConverting = false;
    wxTimer m_timer;

    enum {
        ID_ADD_FILES = 20001,
        ID_ADD_FOLDER,
        ID_REMOVE_SEL,
        ID_CLEAR_LIST,
        ID_START_CONV,
        ID_CANCEL_CONV,
        ID_OPEN_OUT_DIR,
        ID_BROWSE_OUT_DIR,
        ID_RADIO_OUT_SAME,
        ID_RADIO_OUT_CUSTOM,
        ID_CHK_SPLIT,
        ID_BTN_EXCEL_LIMIT,
        ID_CHK_ALL_CH,
        ID_CHK_TIME_RANGE,
        ID_CH_CONFIG,
        ID_RECOVER_PARTIAL,
        ID_CONV_TIMER
    };


    wxDECLARE_EVENT_TABLE();
};

// ドラッグ＆ドロップファイルハンドラ
class ConverterFileDropTarget : public wxFileDropTarget {
public:
    ConverterFileDropTarget(ConverterFrame* frame) : m_frame(frame) {}
    bool OnDropFiles(wxCoord /*x*/, wxCoord /*y*/, const wxArrayString& filenames) override {
        if (m_frame) {
            m_frame->AddFiles(filenames);
            return true;
        }
        return false;
    }
private:
    ConverterFrame* m_frame = nullptr;
};

} // namespace dlog
