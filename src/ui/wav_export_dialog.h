#ifndef DLOG_WAV_EXPORT_DIALOG_H
#define DLOG_WAV_EXPORT_DIALOG_H

#include <wx/dialog.h>
#include <wx/radiobut.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/filedlg.h>
#include <wx/msgdlg.h>

#include "export/wav_export_types.h"
#include "export/wav_export_plan.h"
#include "dlog_reader.h"
#include "common/localized_strings.h"

namespace dlog {

class WavExportDialog : public wxDialog {
public:
    WavExportDialog(wxWindow* parent, const DLogFileHeader& header, uint64_t totalFrames,
                    const std::string& sourceDlogPath,
                    const std::vector<std::string>& channelLabels = {});
    ~WavExportDialog() override = default;

    WavExportOptions GetOptions() const;

private:
    void InitUI();
    void UpdateEstimate();

    void OnRangeToggled(wxCommandEvent& event);
    void OnRangeTextChanged(wxCommandEvent& event);
    void OnChannelToggled(wxCommandEvent& event);
    void OnSelectAllChannels(wxCommandEvent& event);
    void OnMethodChanged(wxCommandEvent& event);
    void OnFormatChanged(wxCommandEvent& event);
    void OnLimit4GiBToggled(wxCommandEvent& event);
    void OnBrowseOutput(wxCommandEvent& event);
    void OnOutputPathChanged(wxCommandEvent& event);
    void OnOk(wxCommandEvent& event);

    DLogFileHeader m_header;
    uint64_t m_totalFrames = 0;
    std::string m_sourceDlogPath;
    std::vector<std::string> m_channelLabels;

    // UI Controls
    wxStaticText* m_lblSourceInfo = nullptr;
    wxRadioButton* m_radioRangeAll = nullptr;
    wxRadioButton* m_radioRangeCustom = nullptr;
    wxTextCtrl* m_txtTimeFrom = nullptr;
    wxTextCtrl* m_txtTimeTo = nullptr;

    std::vector<wxCheckBox*> m_chkChannels;
    wxButton* m_btnSelectAll = nullptr;

    wxChoice* m_choiceMethod = nullptr;
    wxStaticText* m_lblFirNotice = nullptr;

    wxChoice* m_choiceFormat = nullptr;
    wxStaticText* m_lblPcm16Notice = nullptr;

    wxCheckBox* m_chkLimit4GiB = nullptr;

    wxTextCtrl* m_txtOutputPath = nullptr;
    wxButton* m_btnBrowseOutput = nullptr;

    wxStaticText* m_lblEstimate = nullptr;
    wxStaticText* m_lblEstimateWarning = nullptr;
    wxButton* m_btnOk = nullptr;
    wxButton* m_btnCancel = nullptr;

    WavExportEstimate m_currentEstimate;
    uint64_t m_currentStartFrame = 0;
    uint64_t m_currentEndFrame = 0;
    std::string m_browseConfirmedPath;
    bool m_allChannelsSelectedState = true;
};

} // namespace dlog

#endif // DLOG_WAV_EXPORT_DIALOG_H
