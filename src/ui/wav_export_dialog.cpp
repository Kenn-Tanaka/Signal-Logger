#include "ui/wav_export_dialog.h"
#include "playback/playback_time_parser.h"
#include <filesystem>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace dlog {

namespace {

static std::string FormatBytes(uint64_t bytes) {
    char buf[64];
    if (bytes >= (1024ULL * 1024ULL * 1024ULL)) {
        double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        std::snprintf(buf, sizeof(buf), "%.2f GiB (%llu bytes)", gb, static_cast<unsigned long long>(bytes));
    } else {
        double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        std::snprintf(buf, sizeof(buf), "%.2f MiB (%llu bytes)", mb, static_cast<unsigned long long>(bytes));
    }
    return std::string(buf);
}

static std::string FormatDuration(double sec) {
    return FormatPlaybackTime(sec, true);
}

static std::string ProposeDefaultOutputPath(const std::string& src) {
    try {
        std::filesystem::path p = std::filesystem::u8path(src);
        std::filesystem::path wavPath = p;
        wavPath.replace_extension(".wav");
        if (wavPath == p) {
            wavPath = p.parent_path() / std::filesystem::u8path(p.stem().u8string() + "_export.wav");
        }
        return wavPath.u8string();
    } catch (...) {
        return src + ".wav";
    }
}

} // namespace

WavExportDialog::WavExportDialog(wxWindow* parent, const DLogFileHeader& header, uint64_t totalFrames,
                                 const std::string& sourceDlogPath,
                                 const std::vector<std::string>& channelLabels)
    : wxDialog(parent, wxID_ANY, TrWx(StringId::WavExportDialogTitle),
               wxDefaultPosition, wxSize(640, 720), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_header(header),
      m_totalFrames(totalFrames),
      m_sourceDlogPath(sourceDlogPath),
      m_channelLabels(channelLabels)
{
    InitUI();
    UpdateEstimate();
    CentreOnParent();
}

void WavExportDialog::InitUI() {
    auto* mainSizer = new wxBoxSizer(wxVERTICAL);

    // 1. 入力ソース情報グループ
    auto* boxSource = new wxStaticBoxSizer(wxVERTICAL, this, TrWx(StringId::WavExportGroupSource));
    double effectiveRate = (m_header.decimationFactor > 0)
        ? (static_cast<double>(m_header.nativeRate) / static_cast<double>(m_header.decimationFactor))
        : static_cast<double>(m_header.sampleRate);
    double durationSec = (effectiveRate > 0.0) ? (static_cast<double>(m_totalFrames) / effectiveRate) : 0.0;

    std::string sourceInfoStr = TrFormat(StringId::WavExportSourceFormat, {
        m_sourceDlogPath,
        std::to_string(m_header.nativeRate),
        std::to_string(m_header.decimationFactor),
        std::to_string(static_cast<uint32_t>(std::round(effectiveRate))),
        std::to_string(m_header.channels),
        std::to_string(m_totalFrames),
        FormatDuration(durationSec)
    });
    m_lblSourceInfo = new wxStaticText(boxSource->GetStaticBox(), wxID_ANY, wxString::FromUTF8(sourceInfoStr.c_str()));
    boxSource->Add(m_lblSourceInfo, 0, wxALL | wxEXPAND, 5);

    auto* lblNoticeInterp = new wxStaticText(boxSource->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportNoticeInterpolation));
    lblNoticeInterp->SetForegroundColour(wxColour(110, 110, 110));
    boxSource->Add(lblNoticeInterp, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);
    mainSizer->Add(boxSource, 0, wxALL | wxEXPAND, 6);

    // 2. 出力時間範囲グループ
    auto* boxRange = new wxStaticBoxSizer(wxVERTICAL, this, TrWx(StringId::WavExportGroupRange));
    m_radioRangeAll = new wxRadioButton(boxRange->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportRangeAll), wxDefaultPosition, wxDefaultSize, wxRB_GROUP);
    m_radioRangeAll->SetValue(true);
    m_radioRangeAll->Bind(wxEVT_RADIOBUTTON, &WavExportDialog::OnRangeToggled, this);
    boxRange->Add(m_radioRangeAll, 0, wxALL, 4);

    m_radioRangeCustom = new wxRadioButton(boxRange->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportRangeCustom));
    m_radioRangeCustom->Bind(wxEVT_RADIOBUTTON, &WavExportDialog::OnRangeToggled, this);
    boxRange->Add(m_radioRangeCustom, 0, wxLEFT | wxRIGHT | wxBOTTOM, 4);

    auto* timeRowSizer = new wxBoxSizer(wxHORIZONTAL);
    timeRowSizer->Add(new wxStaticText(boxRange->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportTimeFrom)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 20);
    m_txtTimeFrom = new wxTextCtrl(boxRange->GetStaticBox(), wxID_ANY, "00:00:00.000000", wxDefaultPosition, wxSize(120, -1));
    m_txtTimeFrom->Enable(false);
    m_txtTimeFrom->Bind(wxEVT_TEXT, &WavExportDialog::OnRangeTextChanged, this);
    timeRowSizer->Add(m_txtTimeFrom, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);

    timeRowSizer->Add(new wxStaticText(boxRange->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportTimeTo)), 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 15);
    m_txtTimeTo = new wxTextCtrl(boxRange->GetStaticBox(), wxID_ANY, wxString::FromUTF8(FormatDuration(durationSec).c_str()), wxDefaultPosition, wxSize(120, -1));
    m_txtTimeTo->Enable(false);
    m_txtTimeTo->Bind(wxEVT_TEXT, &WavExportDialog::OnRangeTextChanged, this);
    timeRowSizer->Add(m_txtTimeTo, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, 5);

    boxRange->Add(timeRowSizer, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
    mainSizer->Add(boxRange, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    // 3. 出力チャネルグループ
    auto* boxChannels = new wxStaticBoxSizer(wxVERTICAL, this, TrWx(StringId::WavExportGroupChannels));
    auto* chGridSizer = new wxFlexGridSizer(2, 4, 4, 8);
    for (uint32_t c = 0; c < m_header.channels && c < 8; ++c) {
        std::string labelText;
        if (c < m_channelLabels.size() && !m_channelLabels[c].empty()) {
            labelText = "Ch " + std::to_string(c + 1) + ": " + m_channelLabels[c];
        } else {
            labelText = "Ch " + std::to_string(c + 1);
        }
        auto* chk = new wxCheckBox(boxChannels->GetStaticBox(), wxID_ANY, wxString::FromUTF8(labelText.c_str()));
        chk->SetValue(true);
        chk->Bind(wxEVT_CHECKBOX, &WavExportDialog::OnChannelToggled, this);
        m_chkChannels.push_back(chk);
        chGridSizer->Add(chk, 0, wxALIGN_CENTER_VERTICAL);
    }
    boxChannels->Add(chGridSizer, 0, wxALL | wxEXPAND, 5);

    m_btnSelectAll = new wxButton(boxChannels->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportAllChannels));
    m_btnSelectAll->Bind(wxEVT_BUTTON, &WavExportDialog::OnSelectAllChannels, this);
    boxChannels->Add(m_btnSelectAll, 0, wxLEFT | wxBOTTOM, 5);
    mainSizer->Add(boxChannels, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    // 4. 補間方式グループ
    std::string methodBoxTitle = TrFormat(StringId::WavExportGroupMethod, std::to_string(m_header.nativeRate));
    auto* boxMethod = new wxStaticBoxSizer(wxVERTICAL, this, wxString::FromUTF8(methodBoxTitle.c_str()));

    wxArrayString methodChoices;
    methodChoices.Add(TrWx(StringId::WavExportMethodZOH));
    methodChoices.Add(TrWx(StringId::WavExportMethodLinear));
    methodChoices.Add(TrWx(StringId::WavExportMethodFIR));

    m_choiceMethod = new wxChoice(boxMethod->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, methodChoices);
    m_choiceMethod->SetSelection(1); // 既定: 線形補間
    m_choiceMethod->Bind(wxEVT_CHOICE, &WavExportDialog::OnMethodChanged, this);

    if (m_header.decimationFactor == 1) {
        m_choiceMethod->Clear();
        m_choiceMethod->Append(TrWx(StringId::WavExportMethodNoInterpolation));
        m_choiceMethod->SetSelection(0);
        m_choiceMethod->Enable(false);
    }
    boxMethod->Add(m_choiceMethod, 0, wxALL | wxEXPAND, 5);

    m_lblFirNotice = new wxStaticText(boxMethod->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportNoticeFIR));
    m_lblFirNotice->SetForegroundColour(wxColour(110, 110, 110));
    m_lblFirNotice->Show(m_choiceMethod->GetSelection() == 2);
    boxMethod->Add(m_lblFirNotice, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);
    mainSizer->Add(boxMethod, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    // 5. 出力フォーマットグループ
    auto* boxFormat = new wxStaticBoxSizer(wxVERTICAL, this, TrWx(StringId::WavExportGroupFormat));
    wxArrayString formatChoices;
    formatChoices.Add(TrWx(StringId::WavExportFormatFloat32));
    formatChoices.Add(TrWx(StringId::WavExportFormatPcm16));

    m_choiceFormat = new wxChoice(boxFormat->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, formatChoices);
    m_choiceFormat->SetSelection(0); // 既定: Float32
    m_choiceFormat->Bind(wxEVT_CHOICE, &WavExportDialog::OnFormatChanged, this);
    boxFormat->Add(m_choiceFormat, 0, wxALL | wxEXPAND, 5);

    m_lblPcm16Notice = new wxStaticText(boxFormat->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportNoticePcm16));
    m_lblPcm16Notice->SetForegroundColour(wxColour(110, 110, 110));
    m_lblPcm16Notice->Show(false);
    boxFormat->Add(m_lblPcm16Notice, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

    m_chkLimit4GiB = new wxCheckBox(boxFormat->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportCheckLimit4GiB));
    m_chkLimit4GiB->SetValue(true); // 既定で有効、永続化しない
    m_chkLimit4GiB->SetToolTip(TrWx(StringId::WavExportTipLimit4GiB));
    m_chkLimit4GiB->Bind(wxEVT_CHECKBOX, &WavExportDialog::OnLimit4GiBToggled, this);
    boxFormat->Add(m_chkLimit4GiB, 0, wxALL, 5);

    mainSizer->Add(boxFormat, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    // 6. 出力先ファイルグループ
    auto* boxOutput = new wxStaticBoxSizer(wxVERTICAL, this, TrWx(StringId::WavExportGroupOutput));
    auto* outRowSizer = new wxBoxSizer(wxHORIZONTAL);
    outRowSizer->Add(new wxStaticText(boxOutput->GetStaticBox(), wxID_ANY, TrWx(StringId::WavExportLabelOutputFile)), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    std::string defaultOut = ProposeDefaultOutputPath(m_sourceDlogPath);
    m_txtOutputPath = new wxTextCtrl(boxOutput->GetStaticBox(), wxID_ANY, wxString::FromUTF8(defaultOut.c_str()), wxDefaultPosition, wxDefaultSize);
    m_txtOutputPath->Bind(wxEVT_TEXT, &WavExportDialog::OnOutputPathChanged, this);
    outRowSizer->Add(m_txtOutputPath, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

    m_btnBrowseOutput = new wxButton(boxOutput->GetStaticBox(), wxID_ANY, TrWx(StringId::ButtonBrowse));
    m_btnBrowseOutput->Bind(wxEVT_BUTTON, &WavExportDialog::OnBrowseOutput, this);
    outRowSizer->Add(m_btnBrowseOutput, 0, wxALIGN_CENTER_VERTICAL);

    boxOutput->Add(outRowSizer, 0, wxALL | wxEXPAND, 5);
    mainSizer->Add(boxOutput, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    // 7. 出力予測情報グループ
    auto* boxEstimate = new wxStaticBoxSizer(wxVERTICAL, this, TrWx(StringId::WavExportGroupEstimate));
    m_lblEstimate = new wxStaticText(boxEstimate->GetStaticBox(), wxID_ANY, "");
    boxEstimate->Add(m_lblEstimate, 0, wxALL | wxEXPAND, 5);

    m_lblEstimateWarning = new wxStaticText(boxEstimate->GetStaticBox(), wxID_ANY, "");
    m_lblEstimateWarning->SetForegroundColour(wxColour(190, 30, 30));
    boxEstimate->Add(m_lblEstimateWarning, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 5);

    mainSizer->Add(boxEstimate, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, 6);

    // 8. 下部ボタン (OK / Cancel)
    auto* btnSizer = new wxBoxSizer(wxHORIZONTAL);
    btnSizer->AddStretchSpacer(1);

    m_btnOk = new wxButton(this, wxID_OK, TrWx(StringId::WavExportButtonExport));
    m_btnOk->SetDefault();
    m_btnOk->Bind(wxEVT_BUTTON, &WavExportDialog::OnOk, this);
    btnSizer->Add(m_btnOk, 0, wxRIGHT, 8);

    m_btnCancel = new wxButton(this, wxID_CANCEL, TrWx(StringId::ButtonCancel));
    btnSizer->Add(m_btnCancel, 0);

    mainSizer->Add(btnSizer, 0, wxALL | wxEXPAND, 10);

    SetSizerAndFit(mainSizer);
}

void WavExportDialog::UpdateEstimate() {
    // 範囲解析
    uint64_t startFrame = 0;
    uint64_t endFrame = m_totalFrames;
    std::string rangeErr;
    LocalizedError rangeLocErr;

    double effectiveRate = (m_header.decimationFactor > 0)
        ? (static_cast<double>(m_header.nativeRate) / static_cast<double>(m_header.decimationFactor))
        : static_cast<double>(m_header.sampleRate);

    bool exportAll = m_radioRangeAll ? m_radioRangeAll->GetValue() : true;
    if (!exportAll) {
        std::string sFrom = std::string(m_txtTimeFrom->GetValue().utf8_str());
        std::string sTo = std::string(m_txtTimeTo->GetValue().utf8_str());
        double totalDur = (effectiveRate > 0.0) ? (static_cast<double>(m_totalFrames) / effectiveRate) : 0.0;
        double tStart = 0.0, tEnd = 0.0;
        if (!ParsePlaybackTimeRaw(sFrom, totalDur, tStart, rangeErr, 1e-6)) {
            // 解析失敗 (rangeErr は現在のUI言語で格納される)
        } else if (!ParsePlaybackTimeRaw(sTo, totalDur, tEnd, rangeErr, 1e-6)) {
            // 解析失敗
        } else if (!WavExportPlan::CalculateFrameRange(tStart, tEnd, m_totalFrames, effectiveRate, startFrame, endFrame, rangeErr, &rangeLocErr)) {
            if (rangeLocErr.HasError()) {
                rangeErr = Tr(rangeLocErr.code);
            }
        }
    }
    m_currentStartFrame = startFrame;
    m_currentEndFrame = endFrame;

    WavExportOptions opts = GetOptions();

    m_currentEstimate = WavExportPlan::AnalyzePlan(m_header, m_totalFrames, opts);
    if (!rangeErr.empty()) {
        m_currentEstimate.isValid = false;
        m_currentEstimate.validationError = rangeErr;
        m_currentEstimate.errorInfo.Clear();
    } else if (!m_currentEstimate.isValid && m_currentEstimate.errorInfo.HasError()) {
        m_currentEstimate.validationError = FormatString(
            m_currentEstimate.errorInfo.code,
            m_currentEstimate.errorInfo.args,
            LanguagePreferences::GetCurrentLanguage()
        );
    }

    std::string pathErr;
    LocalizedError pathLocErr;
    bool pathValid = WavExportPlan::ValidatePaths(opts.sourceDlogPath, opts.destinationWavPath, pathErr, &pathLocErr);
    if (!pathValid && m_currentEstimate.isValid) {
        m_currentEstimate.isValid = false;
        if (pathLocErr.HasError()) {
            m_currentEstimate.validationError = FormatString(
                pathLocErr.code,
                pathLocErr.args,
                LanguagePreferences::GetCurrentLanguage()
            );
        } else {
            m_currentEstimate.validationError = pathErr;
        }
    }

    // 表示更新
    std::string containerName = (m_currentEstimate.containerFormat == WavContainerFormat::Rf64) ? "64-bit RF64" : "Standard WAV";
    std::string estStr = TrFormat(StringId::WavExportEstimateFormat, {
        std::to_string(m_currentEstimate.outputFrames),
        FormatDuration(m_currentEstimate.outputDurationSec),
        FormatBytes(m_currentEstimate.estimatedTotalBytes),
        containerName
    });
    m_lblEstimate->SetLabel(wxString::FromUTF8(estStr.c_str()));

    if (!m_currentEstimate.isValid) {
        m_lblEstimateWarning->SetLabel(wxString::FromUTF8(m_currentEstimate.validationError.c_str()));
        m_lblEstimateWarning->Show(true);
        m_btnOk->Enable(false);
    } else {
        m_lblEstimateWarning->SetLabel("");
        m_lblEstimateWarning->Show(false);
        m_btnOk->Enable(true);
    }

    Layout();
}

WavExportOptions WavExportDialog::GetOptions() const {
    WavExportOptions opts;
    opts.sourceDlogPath = m_sourceDlogPath;
    opts.destinationWavPath = std::string(m_txtOutputPath->GetValue().utf8_str());
    opts.exportEntireFile = m_radioRangeAll->GetValue();
    opts.startFrame = opts.exportEntireFile ? 0 : m_currentStartFrame;
    opts.endFrame = opts.exportEntireFile ? m_totalFrames : m_currentEndFrame;

    opts.explicitChannelSelection = true;
    opts.selectedChannels.clear();
    for (size_t i = 0; i < m_chkChannels.size(); ++i) {
        if (m_chkChannels[i]->GetValue()) {
            opts.selectedChannels.push_back(static_cast<int>(i));
        }
    }

    if (m_header.decimationFactor == 1) {
        opts.interpolationMethod = InterpolationMethod::Linear;
    } else {
        int sel = m_choiceMethod->GetSelection();
        if (sel == 0) opts.interpolationMethod = InterpolationMethod::ZeroOrderHold;
        else if (sel == 2) opts.interpolationMethod = InterpolationMethod::BandlimitedFir;
        else opts.interpolationMethod = InterpolationMethod::Linear;
    }

    int fmtSel = m_choiceFormat->GetSelection();
    opts.sampleFormat = (fmtSel == 1) ? WavSampleFormat::Pcm16 : WavSampleFormat::IeeeFloat32;

    opts.limitTo4GiB = m_chkLimit4GiB->GetValue();
    return opts;
}

void WavExportDialog::OnRangeToggled(wxCommandEvent& WXUNUSED(event)) {
    bool isCustom = m_radioRangeCustom->GetValue();
    m_txtTimeFrom->Enable(isCustom);
    m_txtTimeTo->Enable(isCustom);
    UpdateEstimate();
}

void WavExportDialog::OnRangeTextChanged(wxCommandEvent& WXUNUSED(event)) {
    UpdateEstimate();
}

void WavExportDialog::OnChannelToggled(wxCommandEvent& WXUNUSED(event)) {
    UpdateEstimate();
}

void WavExportDialog::OnSelectAllChannels(wxCommandEvent& WXUNUSED(event)) {
    m_allChannelsSelectedState = !m_allChannelsSelectedState;
    for (auto* chk : m_chkChannels) {
        chk->SetValue(m_allChannelsSelectedState);
    }
    UpdateEstimate();
}

void WavExportDialog::OnMethodChanged(wxCommandEvent& WXUNUSED(event)) {
    if (m_lblFirNotice) {
        m_lblFirNotice->Show(m_choiceMethod->GetSelection() == 2);
    }
    UpdateEstimate();
}

void WavExportDialog::OnFormatChanged(wxCommandEvent& WXUNUSED(event)) {
    if (m_lblPcm16Notice) {
        m_lblPcm16Notice->Show(m_choiceFormat->GetSelection() == 1);
    }
    UpdateEstimate();
}

void WavExportDialog::OnLimit4GiBToggled(wxCommandEvent& WXUNUSED(event)) {
    UpdateEstimate();
}

void WavExportDialog::OnBrowseOutput(wxCommandEvent& WXUNUSED(event)) {
    wxFileDialog saveDlg(this, TrWx(StringId::WavExportDialogTitle),
                         wxEmptyString, m_txtOutputPath->GetValue(),
                         TrWx(StringId::FilterWavFiles),
                         wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (saveDlg.ShowModal() == wxID_OK) {
        wxString chosenPath = saveDlg.GetPath();
        m_browseConfirmedPath = std::string(chosenPath.utf8_str());
        m_txtOutputPath->SetValue(chosenPath);
        UpdateEstimate();
    }
}

void WavExportDialog::OnOutputPathChanged(wxCommandEvent& WXUNUSED(event)) {
    std::string curPath = std::string(m_txtOutputPath->GetValue().utf8_str());
    if (curPath != m_browseConfirmedPath) {
        m_browseConfirmedPath.clear();
    }
    UpdateEstimate();
}

void WavExportDialog::OnOk(wxCommandEvent& event) {
    UpdateEstimate();
    if (!m_currentEstimate.isValid) {
        return;
    }

    std::string destPath = std::string(m_txtOutputPath->GetValue().utf8_str());
    std::error_code ec;
    if (destPath != m_browseConfirmedPath &&
        std::filesystem::exists(std::filesystem::u8path(destPath), ec)) {
        int ans = wxMessageBox(
            TrFormatWx(StringId::ConfirmOverwritePrompt, wxString::FromUTF8(destPath.c_str())),
            TrWx(StringId::ConfirmDialogTitle),
            wxYES_NO | wxNO_DEFAULT | wxICON_WARNING,
            this
        );
        if (ans != wxYES) {
            return;
        }
    }

    event.Skip();
}

} // namespace dlog
