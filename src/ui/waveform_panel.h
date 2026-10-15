#pragma once

#include "audio/audio_types.h"
#include "dsp/calibration_calculator.h"
#include "ui/time_axis_calculator.h"
#include "playback/playback_types.h"
#include <wx/panel.h>
#include <wx/timer.h>
#include <vector>
#include <deque>
#include <mutex>

namespace dlog {

/**
 * @brief マルチチャネルリアルタイム波形描画パネル
 * Min/Max エンベロープアルゴリズムと wxBufferedPaintDC を採用
 */
class WaveformPanel : public wxPanel {
public:
    WaveformPanel(wxWindow* parent, wxWindowID id = wxID_ANY,
                  const wxPoint& pos = wxDefaultPosition,
                  const wxSize& size = wxDefaultSize);
    ~WaveformPanel() override = default;

    /**
     * @brief 新しいオーディオブロックを追加
     */
    void PushAudioBlock(const AudioBlock& block);

    /**
     * @brief 表示チャネル数の設定
     */
    void SetChannelCount(int channels);

    /**
     * @brief 表示サンプリングレートの設定（バッファ長を再計算）
     */
    void SetSampleRate(double sampleRate);

    /**
     * @brief 表示タイムウィンドウ（秒数）の設定
     */
    void SetTimeWindow(double seconds);
    double GetTimeWindow() const { return m_timeWindowSec; }

    /**
     * @brief 表示垂直ゲイン（倍率）の設定
     */
    void SetVerticalGain(float gain);
    float GetVerticalGain() const { return m_verticalGain; }

    /**
     * @brief フルスケール電圧 (V) の設定
     */
    void SetFullScaleVoltage(double vFs);
    double GetFullScaleVoltage() const { return m_fullScaleVoltage; }

    /**
     * @brief CAL信号校正用: 直近の波形データから指定チャネルの詳細統計 (Vpp, Peak, Status) を測定
     */
    SignalStats MeasureChannelStats(int channel, double durationSec = 1.0);

    /**
     * @brief CAL信号校正用: 直近の波形データから指定チャネルの Peak-to-Peak 振幅 (0.0〜2.0) を測定
     */
    double MeasureChannelVpp(int channel, double durationSec = 1.0);

    /**
     * @brief バッファのクリア
     */
    void Clear();

    /**
     * @brief 波形描画の一時停止 (Hold) 切り替え
     */
    void SetHold(bool hold);
    bool IsHold() const { return m_isHold; }

    /**
     * @brief チャネル名称・単位の設定
     */
    void SetChannelMetadata(const std::vector<ChannelMetadata>& meta);

    /**
     * @brief 現在の時間軸スケール情報を取得
     */
    TimeScaleInfo GetCurrentTimeScaleInfo() const;

    /**
     * @brief 現在の表示時間範囲情報を取得
     */
    TimeRangeInfo GetCurrentTimeRangeInfo() const;

    uint64_t GetLastFrameIndex() const { return m_lastFrameIndex; }
    bool HasData() const { return m_hasData; }

    enum class TimeReferenceMode {
        None,
        PreviewStart,
        RecordStart
    };

    /**
     * @brief 時間軸基準モードの設定
     */
    void SetTimeReferenceMode(TimeReferenceMode mode);
    TimeReferenceMode GetTimeReferenceMode() const { return m_timeRefMode; }

    /**
     * @brief 時間軸基準ラベル（後方互換用）
     */
    void SetTimeReferenceLabel(const wxString& label);
    wxString GetTimeReferenceLabel() const;

    /**
     * @brief 再生モードの切り替え
     */
    void SetPlaybackMode(bool isPlayback);
    bool IsPlaybackMode() const { return m_isPlaybackMode; }

    /**
     * @brief 再生表示用ウィンドウデータの更新
     */
    void SetPlaybackWindow(const PlaybackWindowData& data);
    const PlaybackWindowData& GetPlaybackWindow() const { return m_playbackData; }

    /**
     * @brief 波形描画領域のピクセル幅（レベルメーター除外）を取得
     */
    int GetPlotPixelWidth() const;

private:
    void OnPaint(wxPaintEvent& event);
    void OnSize(wxSizeEvent& event);
    void OnEraseBackground(wxEraseEvent& event);
    void ReinitializeBuffersLocked();

    int m_channels = 2;
    double m_sampleRate = 22050.0;
    double m_timeWindowSec = 2.0; // 表示窓幅: 2秒
    float m_verticalGain = 1.0f;  // 垂直ゲイン倍率 (1.0 = 標準)
    double m_fullScaleVoltage = 1.0; // フルスケール電圧 [V] (デフォルト 1.0 V)
    bool m_isHold = false;        // 波形一時停止 (Hold) フラグ
    std::vector<ChannelMetadata> m_channelMetadata; // チャネル名・単位
    TimeReferenceMode m_timeRefMode = TimeReferenceMode::None; // 時間軸基準モード
    wxString m_timeReferenceLabel;                  // 時間軸基準ラベル（例: "プレビュー開始から", "記録開始から"）

    bool m_isPlaybackMode = false;                  // 再生モードフラグ
    PlaybackWindowData m_playbackData;              // 再生用表示区間データ

    // 循環サンプルリングバッファ（最大10秒分の容量）
    size_t m_bufferCapacity = 22050 * 10;
    std::vector<uint64_t> m_frameIndices; // 各スロットの累積フレーム番号 (未記録/無効時はUINT64_MAX)
    std::vector<std::vector<float>> m_channelBuffers; // [ch][slot]
    uint64_t m_lastFrameIndex = 0; // 受信済み最新フレームのインデックス
    bool m_hasData = false;        // データが受信済みか（記録開始前・Clear直後はfalse）

    mutable std::mutex m_bufferMutex; // UI描画とデータ供給の非ブロッキング排他

    // チャネル描画カラーパレット
    std::vector<wxColour> m_channelColors;

    wxDECLARE_EVENT_TABLE();
};

} // namespace dlog
