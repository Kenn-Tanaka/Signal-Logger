#ifndef DLOG_CALIBRATION_DIALOG_H
#define DLOG_CALIBRATION_DIALOG_H

#include <wx/dialog.h>
#include <wx/timer.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include "ui/waveform_panel.h"
#include "dsp/calibration_calculator.h"

namespace dlog {

class CalibrationDialog : public wxDialog {
public:
    CalibrationDialog(wxWindow* parent, WaveformPanel* waveformPanel, double currentVfs, int channelCount);
    ~CalibrationDialog() override;

    double GetCalculatedVfs() const { return m_calculatedVfs; }
    double GetMeasuredVpp() const { return m_measuredVpp; }
    double GetTargetVpp() const { return m_targetVpp; }
    int GetSelectedChannel() const { return m_selectedChannel; }

private:
    void OnTimer(wxTimerEvent& event);
    void OnTargetVppChanged(wxCommandEvent& event);
    void OnChannelSelected(wxCommandEvent& event);
    void OnApply(wxCommandEvent& event);

    void UpdateCalculations();

    WaveformPanel* m_waveformPanel = nullptr;
    double m_currentVfs = 1.0;
    int m_channelCount = 1;

    wxTimer m_timer;

    wxChoice* m_choiceChannel = nullptr;
    wxTextCtrl* m_txtTargetVpp = nullptr;
    wxStaticText* m_lblMeasuredVpp = nullptr;
    wxStaticText* m_lblPeakValue = nullptr;
    wxStaticText* m_lblCalculatedVfs = nullptr;
    wxStaticText* m_lblStatus = nullptr;
    wxButton* m_btnApply = nullptr;
    wxButton* m_btnCancel = nullptr;

    double m_measuredVpp = 0.0;
    double m_targetVpp = 1.0;
    double m_calculatedVfs = 1.0;
    int m_selectedChannel = 0;

    enum {
        ID_CAL_TIMER = 10001,
        ID_CAL_TARGET_VPP,
        ID_CAL_CHANNEL,
        ID_CAL_APPLY
    };

    wxDECLARE_EVENT_TABLE();
};

} // namespace dlog

#endif // DLOG_CALIBRATION_DIALOG_H
