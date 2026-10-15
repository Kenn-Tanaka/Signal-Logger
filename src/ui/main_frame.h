#pragma once

#include "audio/audio_types.h"
#include "audio/audio_device_mgr.h"
#include "storage/data_logger_worker.h"
#include "ui/waveform_panel.h"
#include "playback/playback_loader_worker.h"
#include "playback/playback_time_parser.h"

#include <wx/frame.h>
#include <wx/choice.h>
#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/sizer.h>
#include <wx/notebook.h>
#include <wx/slider.h>
#include <wx/checkbox.h>
#include <memory>
#include <vector>
#include <chrono>

namespace dlog {

class MainFrame : public wxFrame {
public:
    MainFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    ~MainFrame() override;

    bool PerformSmokeTest();

private:
    bool m_isInitializing = true;
    void InitUI();
    void RefreshDeviceList();
    void UpdateTargetRateDisplay();
    void UpdateDecimationChoices();
    void UpdateSampleRateChoices();
    double GetSelectedNativeSampleRate() const;
    void UpdateChannelChoices();
    int GetSelectedChannelCount() const;
    double GetSelectedFullScaleVoltage() const;
    enum class AppState {
        Stopped,    // 停止中
        Previewing, // プレビュー中・保存なし
        Recording   // 記録中
    };

    void OnDeviceSelect(wxCommandEvent& event);
    void OnRefreshDevices(wxCommandEvent& event);
    void OnChannelSelect(wxCommandEvent& event);
    void OnRateSelect(wxCommandEvent& event);
    void OnDecimationSelect(wxCommandEvent& event);
    void OnTimeWindowSelect(wxCommandEvent& event);
    void OnGainSelect(wxCommandEvent& event);
    void OnBrowseFile(wxCommandEvent& event);
    void OnStartStop(wxCommandEvent& event);
    void OnPreviewStartStop(wxCommandEvent& event);
    void OnVoltageChange(wxCommandEvent& event);
    void OnCalibrate(wxCommandEvent& event);
    void OnToggleHold(wxCommandEvent& event);
    void OnAcGuide(wxCommandEvent& event);
    void OnChannelConfig(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);
    void OnClose(wxCloseEvent& event);

    // 再生モード用イベントハンドラ
    void OnModeChanging(wxBookCtrlEvent& event);
    void OnModeChanged(wxBookCtrlEvent& event);
    void OnPlayOpenFile(wxCommandEvent& event);
    void OnPlayFirst(wxCommandEvent& event);
    void OnPlayPause(wxCommandEvent& event);
    void OnPlayLast(wxCommandEvent& event);
    void OnPlaySpeedSelect(wxCommandEvent& event);
    void OnPlayTimeEnter(wxCommandEvent& event);
    void OnPlaySeek(wxCommandEvent& event);
    void OnPlaySliderThumbTrack(wxScrollEvent& event);
    void OnPlaySliderThumbRelease(wxScrollEvent& event);
    void OnPlaySliderScroll(wxScrollEvent& event);

    // 再生制御ヘルパー
    void PausePlayback();
    void ResumePlayback();
    void SeekPlaybackTo(double timeSec);
    double GetCurrentPlaybackPosition() const;
    void RequestPlaybackWindow(double centerTimeSec, bool isNewEpoch = false);
    void UpdatePlaybackUI();

    bool StartPreview();
    void StopPreview(bool isError = false, wxString errorMsg = wxEmptyString);
    bool StartRecording();
    void StopRecording(bool isError = false, wxString errorMsg = wxEmptyString, bool resumePreview = true);
    void UpdateControlStates();
    LoggerConfig BuildConfig(bool forRecording, wxString& errorMsg) const;
    bool WriteCompanionMetadata(const LoggerConfig& config, const DLogFileHeader& header);

    AppState m_appState = AppState::Stopped;

    // コアマネージャ
    std::unique_ptr<AudioDeviceManager> m_audioMgr;
    std::shared_ptr<DataLoggerWorker> m_loggerWorker;

    std::vector<DeviceInfo> m_devices;
    std::vector<int> m_availableChannels;
    std::vector<double> m_availableSampleRates;

    // モード切替ノートブック
    wxNotebook* m_modeNotebook = nullptr;
    wxPanel* m_panelLive = nullptr;
    wxPanel* m_panelPlayback = nullptr;

    // ライブ入力 UI コントロール
    wxChoice* m_choiceDevice = nullptr;
    wxButton* m_btnRefreshDevices = nullptr;
    wxChoice* m_choiceChannels = nullptr;
    wxChoice* m_choiceSampleRate = nullptr;
    wxChoice* m_choiceDecimation = nullptr;
    wxCheckBox* m_chkAntiAliasing = nullptr;
    wxStaticText* m_lblTargetRate = nullptr;

    // 電圧スケール & CAL 校正コントロール (FUNC-001)
    wxTextCtrl* m_txtVoltageScale = nullptr;
    wxButton* m_btnCalibrate = nullptr;

    wxTextCtrl* m_txtFilePath = nullptr;
    wxButton* m_btnBrowse = nullptr;

    wxButton* m_btnPreview = nullptr;   // プレビュー開始/停止ボタン
    wxButton* m_btnStartStop = nullptr; // ロギング開始/停止ボタン

    // 再生モード UI コントロール
    wxTextCtrl* m_txtPlaybackFile = nullptr;
    wxButton* m_btnOpenPlaybackFile = nullptr;
    wxButton* m_btnPlayFirst = nullptr;
    wxButton* m_btnPlayPause = nullptr;
    wxButton* m_btnPlayLast = nullptr;
    wxChoice* m_choicePlaySpeed = nullptr;
    wxTextCtrl* m_txtPlayTime = nullptr;
    wxButton* m_btnSeekTime = nullptr;
    wxStaticText* m_lblTotalDuration = nullptr;
    wxSlider* m_sliderPlayback = nullptr;

    // 再生制御状態
    PlaybackLoaderWorker m_playbackLoader;
    bool m_isPlaying = false;
    bool m_isPlaybackFinished = false;
    double m_anchorPosition = 0.0;
    std::chrono::steady_clock::time_point m_anchorTime;
    double m_playbackSpeed = 1.0;
    bool m_wasPlayingBeforeDrag = false;
    bool m_isSliderDragging = false;
    uint64_t m_lastRequestedGen = 0;
    double m_lastRequestedCenterTime = -1.0;
    int m_lastRequestedPixelWidth = -1;
    bool m_hasPlaybackError = false;
    std::string m_playbackErrorMessage;

    // 波形描画オプションコントロール
    wxBoxSizer* m_waveOptSizer = nullptr;
    wxChoice* m_choiceTimeWindow = nullptr;
    wxChoice* m_choiceGain = nullptr;
    wxButton* m_btnHold = nullptr;         // 波形一時停止 (Hold) ボタン
    wxButton* m_btnAcGuide = nullptr;      // AC結合特性ガイダンスボタン
    wxButton* m_btnChannelConfig = nullptr;// チャネル名称・単位設定ボタン

    std::vector<ChannelMetadata> m_channelMetadata; // 各チャネルの名称・単位
    LoggerConfig m_activeConfig;
    bool m_hasActiveConfig = false;

    struct RecordingSummary {
        bool hasSummary = false;
        bool isError = false;
        double durationSec = 0.0;
        double dataDurationSec = 0.0;
        uint64_t totalBytesWritten = 0;
        uint32_t totalOverruns = 0;
        std::string finalPath;
        std::string recoveryPath;
    };
    RecordingSummary m_lastRecordingSummary;

    // ステータス表示
    wxStaticText* m_lblStatus = nullptr;
    wxStaticText* m_lblDuration = nullptr;
    wxStaticText* m_lblFileSize = nullptr;
    wxStaticText* m_lblOverruns = nullptr;
    wxStaticText* m_lblUiDrops = nullptr;

    // 波形描画パネル
    WaveformPanel* m_waveformPanel = nullptr;

    // 描画・更新タイマー (30ms 周期)
    wxTimer m_timer;

    enum {
        ID_DEVICE_CHOICE = 10001,
        ID_REFRESH_DEVICES,
        ID_CHANNEL_CHOICE,
        ID_RATE_CHOICE,
        ID_DECIMATION_CHOICE,
        ID_ANTIALIASING_CHK,
        ID_VOLTAGE_TXT,
        ID_CALIBRATE_BTN,
        ID_AC_GUIDE_BTN,
        ID_CH_CONFIG_BTN,
        ID_TIME_WINDOW_CHOICE,
        ID_GAIN_CHOICE,
        ID_HOLD_BTN,
        ID_BROWSE_FILE,
        ID_START_STOP_BTN,
        ID_PREVIEW_BTN,
        ID_MODE_NOTEBOOK,
        ID_PLAY_OPEN_BTN,
        ID_PLAY_FIRST_BTN,
        ID_PLAY_PAUSE_BTN,
        ID_PLAY_LAST_BTN,
        ID_PLAY_SPEED_CHOICE,
        ID_PLAY_TIME_TXT,
        ID_PLAY_SEEK_BTN,
        ID_PLAY_SLIDER,
        ID_UPDATE_TIMER
    };

    wxDECLARE_EVENT_TABLE();
};

} // namespace dlog
