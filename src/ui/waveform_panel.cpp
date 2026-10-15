#include "ui/waveform_panel.h"
#include "playback/playback_time_parser.h"
#include "common/localized_strings.h"
#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <algorithm>
#include <cmath>

namespace dlog {

wxBEGIN_EVENT_TABLE(WaveformPanel, wxPanel)
    EVT_PAINT(WaveformPanel::OnPaint)
    EVT_SIZE(WaveformPanel::OnSize)
    EVT_ERASE_BACKGROUND(WaveformPanel::OnEraseBackground)
wxEND_EVENT_TABLE()

WaveformPanel::WaveformPanel(wxWindow* parent, wxWindowID id,
                             const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxFULL_REPAINT_ON_RESIZE) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);

    // オシロスコープ風の視認性の高いパレット
    m_channelColors = {
        wxColour(0, 220, 220),   // Ch 1: シアン
        wxColour(80, 240, 80),   // Ch 2: ネオングリーン
        wxColour(255, 170, 0),   // Ch 3: オレンジ
        wxColour(255, 80, 180),  // Ch 4: マゼンタ
        wxColour(255, 230, 0),   // Ch 5: イエロー
        wxColour(100, 180, 255), // Ch 6: スカイブルー
        wxColour(200, 130, 255), // Ch 7: パープル
        wxColour(255, 100, 100)  // Ch 8: コーラルレッド
    };

    SetChannelCount(2);
}

void WaveformPanel::SetChannelCount(int channels) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_channels = std::max(1, std::min(static_cast<int>(MAX_AUDIO_CHANNELS), channels));
    ReinitializeBuffersLocked();
}

void WaveformPanel::ReinitializeBuffersLocked() {
    m_bufferCapacity = static_cast<size_t>(std::ceil(m_sampleRate * 10.0));
    if (m_bufferCapacity < 1000) m_bufferCapacity = 1000;

    m_frameIndices.assign(m_bufferCapacity, UINT64_MAX);
    m_channelBuffers.assign(m_channels, std::vector<float>(m_bufferCapacity, 0.0f));
    m_lastFrameIndex = 0;
    m_hasData = false;
}

void WaveformPanel::SetSampleRate(double sampleRate) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    if (sampleRate > 0.0 && std::fabs(m_sampleRate - sampleRate) > 1e-4) {
        m_sampleRate = sampleRate;
        ReinitializeBuffersLocked();
    }
}

void WaveformPanel::SetTimeWindow(double seconds) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_timeWindowSec = std::max(0.001, seconds);
    Refresh(false);
}

void WaveformPanel::SetVerticalGain(float gain) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_verticalGain = std::max(0.1f, gain);
    Refresh(false);
}

void WaveformPanel::SetFullScaleVoltage(double vFs) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_fullScaleVoltage = std::max(0.0001, vFs);
    Refresh(false);
}

SignalStats WaveformPanel::MeasureChannelStats(int channel, double durationSec) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    SignalStats emptyStats;
    if (!m_hasData) return emptyStats;
    if (channel < 0 || channel >= static_cast<int>(m_channelBuffers.size())) return emptyStats;
    if (m_bufferCapacity == 0) return emptyStats;

    size_t samplesToInspect = static_cast<size_t>(std::ceil(m_sampleRate * durationSec));
    if (samplesToInspect == 0) return emptyStats;

    uint64_t endFrame = m_lastFrameIndex;
    uint64_t startFrame = (endFrame + 1 >= samplesToInspect) ? (endFrame + 1 - samplesToInspect) : 0;

    std::vector<float> sampleBuf;
    sampleBuf.reserve(std::min(samplesToInspect, static_cast<size_t>(endFrame - startFrame + 1)));

    for (uint64_t f = startFrame; f <= endFrame; ++f) {
        size_t slot = f % m_bufferCapacity;
        if (m_frameIndices[slot] == f) {
            sampleBuf.push_back(m_channelBuffers[channel][slot]);
        }
    }

    return CalibrationCalculator::AnalyzeSamples(sampleBuf.data(), sampleBuf.size());
}

double WaveformPanel::MeasureChannelVpp(int channel, double durationSec) {
    return MeasureChannelStats(channel, durationSec).vpp;
}

void WaveformPanel::Clear() {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    std::fill(m_frameIndices.begin(), m_frameIndices.end(), UINT64_MAX);
    for (int ch = 0; ch < m_channels; ++ch) {
        std::fill(m_channelBuffers[ch].begin(), m_channelBuffers[ch].end(), 0.0f);
    }
    m_lastFrameIndex = 0;
    m_hasData = false;
    Refresh(false);
}

void WaveformPanel::SetHold(bool hold) {
    m_isHold = hold;
    Refresh(false);
}

void WaveformPanel::SetChannelMetadata(const std::vector<ChannelMetadata>& meta) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_channelMetadata = meta;
    Refresh(false);
}

void WaveformPanel::SetTimeReferenceMode(TimeReferenceMode mode) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_timeRefMode = mode;
    Refresh(false);
}

void WaveformPanel::SetTimeReferenceLabel(const wxString& label) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_timeReferenceLabel = label;
    if (label.Contains(wxT("プレビュー")) || label.Lower().Contains(wxT("preview"))) {
        m_timeRefMode = TimeReferenceMode::PreviewStart;
    } else if (label.Contains(wxT("記録")) || label.Lower().Contains(wxT("record"))) {
        m_timeRefMode = TimeReferenceMode::RecordStart;
    } else if (label.IsEmpty()) {
        m_timeRefMode = TimeReferenceMode::None;
    }
    Refresh(false);
}

wxString WaveformPanel::GetTimeReferenceLabel() const {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    return m_timeReferenceLabel;
}

void WaveformPanel::SetPlaybackMode(bool isPlayback) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_isPlaybackMode = isPlayback;
    Refresh(false);
}

void WaveformPanel::SetPlaybackWindow(const PlaybackWindowData& data) {
    std::lock_guard<std::mutex> lock(m_bufferMutex);
    m_playbackData = data;
    if (data.channels > 0) {
        m_channels = data.channels;
    }
    if (data.sampleRate > 0.0) {
        m_sampleRate = data.sampleRate;
    }
    if (data.fullScaleVoltage > 0.0) {
        m_fullScaleVoltage = data.fullScaleVoltage;
    }
    Refresh(false);
}

int WaveformPanel::GetPlotPixelWidth() const {
    int w, h;
    GetClientSize(&w, &h);
    int meterWidth = (w > 200) ? 24 : 0;
    return std::max(10, w - meterWidth);
}

TimeScaleInfo WaveformPanel::GetCurrentTimeScaleInfo() const {
    return CalculateTimeScale(m_timeWindowSec);
}

TimeRangeInfo WaveformPanel::GetCurrentTimeRangeInfo() const {
    return CalculateTimeRange(m_lastFrameIndex, m_hasData, m_timeWindowSec, m_sampleRate);
}

void WaveformPanel::PushAudioBlock(const AudioBlock& block) {
    if (m_isHold) return;
    if (block.frameCount == 0 || block.channels == 0) return;

    std::unique_lock<std::mutex> lock(m_bufferMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        // 描画中でロックが取れない場合はフレームをスキップ（非ブロッキング＆ドロップ許容）
        return;
    }

    if (m_channelBuffers.empty() || m_bufferCapacity == 0) return;

    if (block.sampleRate > 0.0 && std::fabs(m_sampleRate - block.sampleRate) > 1e-4) {
        m_sampleRate = block.sampleRate;
        ReinitializeBuffersLocked();
    }

    int channels = std::min({block.channels, m_channels, static_cast<int>(m_channelBuffers.size())});
    size_t frames = block.frameCount;
    uint64_t startFrame = block.firstOutputFrameIndex;

    for (size_t f = 0; f < frames; ++f) {
        uint64_t frameIdx = startFrame + f;
        size_t slot = frameIdx % m_bufferCapacity;
        m_frameIndices[slot] = frameIdx;
        for (int ch = 0; ch < m_channels; ++ch) {
            float val = (ch < channels)
                ? block.samples[f * static_cast<size_t>(block.channels) + ch]
                : 0.0f;
            m_channelBuffers[ch][slot] = val;
        }
    }
    m_lastFrameIndex = startFrame + frames - 1;
    m_hasData = true;
}

void WaveformPanel::OnSize(wxSizeEvent& event) {
    Refresh(false);
    event.Skip();
}

void WaveformPanel::OnEraseBackground(wxEraseEvent& /*event*/) {
    // チラツキ防止のため背景消去は何もしない
}

void WaveformPanel::OnPaint(wxPaintEvent& /*event*/) {
    wxBufferedPaintDC dc(this);
    wxSize clientSize = GetClientSize();
    int width = clientSize.GetWidth();
    int height = clientSize.GetHeight();

    if (width <= 0 || height <= 0) return;

    // 背景描画（オシロスコープ風ダークテーマ）
    dc.SetBackground(wxBrush(wxColour(15, 18, 24)));
    dc.Clear();

    std::unique_lock<std::mutex> lock(m_bufferMutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        return;
    }

    int numChannels = m_channels;
    if (numChannels <= 0) return;

    int meterWidth = (width > 200) ? 24 : 0;
    int timeAxisHeight = 44;
    int plotLeft = 0;
    int plotWidth = (width > meterWidth) ? (width - meterWidth) : width;
    int plotHeight = height - timeAxisHeight;
    if (plotHeight < 40) {
        timeAxisHeight = 0;
        plotHeight = height;
    }

    TimeScaleInfo scaleInfo;
    TimeRangeInfo rangeInfo;
    double activeWindowSec = m_timeWindowSec;

    if (m_isPlaybackMode) {
        if (m_playbackData.timeWindowSec > 0.0) {
            activeWindowSec = m_playbackData.timeWindowSec;
        }
        scaleInfo = CalculateTimeScale(activeWindowSec);
    } else {
        scaleInfo = CalculateTimeScale(m_timeWindowSec);
        rangeInfo = CalculateTimeRange(m_lastFrameIndex, m_hasData, m_timeWindowSec, m_sampleRate);
    }

    int chHeight = plotHeight / numChannels;

    // 各チャネルのグリッド線描画
    for (int ch = 0; ch < numChannels; ++ch) {
        int topY = ch * chHeight;
        int bottomY = (ch == numChannels - 1) ? plotHeight : (topY + chHeight);
        int chH = bottomY - topY;
        int centerY = topY + chH / 2;

        // チャネル境界線
        dc.SetPen(wxPen(wxColour(40, 48, 60), 1, wxPENSTYLE_SOLID));
        dc.DrawLine(0, bottomY, plotWidth, bottomY);

        // 水平グリッド (センター0V, ±0.5V)
        dc.SetPen(wxPen(wxColour(45, 55, 70), 1, wxPENSTYLE_DOT));
        dc.DrawLine(0, centerY, plotWidth, centerY);
        dc.SetPen(wxPen(wxColour(28, 35, 45), 1, wxPENSTYLE_DOT));
        dc.DrawLine(0, topY + chH / 4, plotWidth, topY + chH / 4);
        dc.DrawLine(0, topY + (3 * chH) / 4, plotWidth, topY + (3 * chH) / 4);
    }

    // 垂直グリッド線描画 (10 div)
    for (int i = 0; i <= 10; ++i) {
        int x_i = plotLeft + static_cast<int>(std::round(static_cast<double>(i * plotWidth) / 10.0));
        if (i == 5) {
            // 中央垂直基準線 / 再生位置カーソル
            if (m_isPlaybackMode) {
                dc.SetPen(wxPen(wxColour(0, 220, 255), 2, wxPENSTYLE_SOLID));
            } else {
                dc.SetPen(wxPen(wxColour(60, 75, 95), 1, wxPENSTYLE_SOLID));
            }
        } else {
            dc.SetPen(wxPen(wxColour(28, 35, 45), 1, wxPENSTYLE_DOT));
        }
        dc.DrawLine(x_i, 0, x_i, plotHeight);
    }

    wxFont labelFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
    dc.SetFont(labelFont);

    double W_F = m_timeWindowSec * m_sampleRate;
    double samplesPerPixel = (plotWidth > 0) ? (W_F / plotWidth) : 1.0;

    for (int ch = 0; ch < numChannels; ++ch) {
        if (!m_isPlaybackMode && ch >= static_cast<int>(m_channelBuffers.size())) break;

        int topY = ch * chHeight;
        int bottomY = (ch == numChannels - 1) ? plotHeight : (topY + chHeight);
        int chH = bottomY - topY;
        int centerY = topY + chH / 2;
        int halfH = chH / 2 - 2;
        const float halfHeight = static_cast<float>(halfH);

        wxColour chCol = m_channelColors[ch % m_channelColors.size()];
        dc.SetPen(wxPen(chCol, 1, wxPENSTYLE_SOLID));
        dc.SetBrush(wxBrush(chCol));

        float peakAbs = 0.0f;

        if (m_isPlaybackMode) {
            // ========================================================
            // 再生モードの波形描画
            // ========================================================
            if (ch < static_cast<int>(m_playbackData.peakAbsPerChannel.size())) {
                peakAbs = m_playbackData.peakAbsPerChannel[ch];
            }

            double centerTime = m_playbackData.centerTimeSec;
            double leftTime = centerTime - activeWindowSec / 2.0;
            double rightTime = leftTime + activeWindowSec;
            double effectiveFs = (m_playbackData.sampleRate > 0.0) ? m_playbackData.sampleRate : m_sampleRate;
            double totalDuration = (effectiveFs > 0.0) ? (static_cast<double>(m_playbackData.totalFrames) / effectiveFs) : 0.0;

            // 時間空間でクリップしてからピクセル座標へ変換 (PLAY-003: 整数オーバーフロー防止)
            double visStartT = std::max(leftTime, 0.0);
            double visEndT = std::min(rightTime, totalDuration);

            int minX = plotLeft;
            int maxX = plotLeft - 1;

            if (visStartT < visEndT && activeWindowSec > 0.0 && totalDuration > 0.0) {
                double normStart = std::clamp((visStartT - leftTime) / activeWindowSec, 0.0, 1.0);
                double normEnd   = std::clamp((visEndT - leftTime)   / activeWindowSec, 0.0, 1.0);
                minX = plotLeft + static_cast<int>(std::floor(normStart * plotWidth));
                maxX = plotLeft + static_cast<int>(std::ceil(normEnd * plotWidth)) - 1;
                minX = std::clamp(minX, plotLeft, plotLeft + plotWidth - 1);
                maxX = std::clamp(maxX, plotLeft, plotLeft + plotWidth - 1);
            }

            if (minX <= maxX && m_playbackData.frameCount > 0) {
                if (m_playbackData.isAggregated && ch < static_cast<int>(m_playbackData.minPerChannel.size())) {
                    // Min/Max エンベロープ描画
                    bool prevValid = false;
                    int prevYMin = centerY;
                    int prevYMax = centerY;

                    for (int x = minX; x <= maxX; ++x) {
                        int col = static_cast<int>(static_cast<double>(x - plotLeft) / plotWidth * m_playbackData.numColumns);
                        if (col < 0) col = 0;
                        if (col >= m_playbackData.numColumns) col = m_playbackData.numColumns - 1;

                        float minVal = m_playbackData.minPerChannel[ch][col];
                        float maxVal = m_playbackData.maxPerChannel[ch][col];

                        minVal = std::max(-1.0f, std::min(1.0f, minVal));
                        maxVal = std::max(-1.0f, std::min(1.0f, maxVal));

                        int yMin = centerY - static_cast<int>(maxVal * m_verticalGain * halfHeight);
                        int yMax = centerY - static_cast<int>(minVal * m_verticalGain * halfHeight);

                        if (yMin > yMax) std::swap(yMin, yMax);
                        if (yMin == yMax) yMax = yMin + 1;

                        yMin = std::max(topY + 1, std::min(bottomY - 1, yMin));
                        yMax = std::max(topY + 1, std::min(bottomY - 1, yMax));

                        if (prevValid) {
                            int connMin = std::min(yMin, prevYMax);
                            int connMax = std::max(yMax, prevYMin);
                            dc.DrawLine(x, connMin, x, connMax);
                        } else {
                            dc.DrawLine(x, yMin, x, yMax);
                        }

                        prevValid = true;
                        prevYMin = yMin;
                        prevYMax = yMax;
                    }
                } else if (!m_playbackData.isAggregated && ch < static_cast<int>(m_playbackData.rawSamples.size())) {
                    // Raw サンプル描画 (折れ線・点)
                    const auto& samples = m_playbackData.rawSamples[ch];
                    size_t nSamples = samples.size();
                    bool hasPrev = false;
                    int prevX = 0, prevY = 0;
                    bool drawDots = (nSamples <= 120);

                    for (size_t f = 0; f < nSamples; ++f) {
                        double t = (effectiveFs > 0.0) ? (static_cast<double>(m_playbackData.startFrame + f) / effectiveFs) : 0.0;
                        double normT = (t - leftTime) / activeWindowSec;
                        if (normT < -0.1 || normT > 1.1) {
                            hasPrev = false;
                            continue;
                        }
                        int x = plotLeft + static_cast<int>(std::round(normT * plotWidth));
                        if (x < minX || x > maxX) {
                            hasPrev = false;
                            continue;
                        }

                        float v = std::max(-1.0f, std::min(1.0f, samples[f]));
                        int y = centerY - static_cast<int>(v * m_verticalGain * halfHeight);
                        y = std::max(topY + 1, std::min(bottomY - 1, y));

                        if (hasPrev) {
                            dc.DrawLine(prevX, prevY, x, y);
                        }
                        if (drawDots) {
                            dc.DrawRectangle(x - 1, y - 1, 3, 3);
                        }

                        hasPrev = true;
                        prevX = x;
                        prevY = y;
                    }
                }
            }
        } else if (m_hasData && plotWidth > 0 && m_bufferCapacity > 0) {
            if (samplesPerPixel <= 2.0) {
                // ----------------------------------------------------
                // Mode B: 低サンプル密度 (実サンプルを折れ線または点で描画)
                // ----------------------------------------------------
                int64_t fStart = static_cast<int64_t>(std::floor((static_cast<double>(m_lastFrameIndex) + 1.0) - W_F));
                if (fStart < 0) fStart = 0;
                int64_t fEnd = static_cast<int64_t>(m_lastFrameIndex);

                bool hasPrev = false;
                int64_t prevFrame = -1;
                int prevX = 0;
                int prevY = 0;

                bool drawDots = (fEnd - fStart <= 120) || (samplesPerPixel <= 0.5);

                for (int64_t f = fStart; f <= fEnd; ++f) {
                    uint64_t uFrame = static_cast<uint64_t>(f);
                    size_t slot = uFrame % m_bufferCapacity;
                    if (m_frameIndices[slot] != uFrame) {
                        hasPrev = false;
                        continue;
                    }

                    float v = m_channelBuffers[ch][slot];
                    float absV = std::fabs(v);
                    if (absV > peakAbs) peakAbs = absV;

                    v = std::max(-1.0f, std::min(1.0f, v));
                    int y = centerY - static_cast<int>(v * m_verticalGain * halfHeight);
                    y = std::max(topY + 1, std::min(bottomY - 1, y));

                    double t = static_cast<double>(f) / m_sampleRate;
                    int x = plotLeft + static_cast<int>(std::round((t - rangeInfo.leftTime) / m_timeWindowSec * plotWidth));
                    if (x < plotLeft || x >= plotLeft + plotWidth) {
                        hasPrev = false;
                        continue;
                    }

                    if (hasPrev && (prevFrame + 1 == f)) {
                        dc.DrawLine(prevX, prevY, x, y);
                    }

                    if (drawDots) {
                        dc.DrawRectangle(x - 1, y - 1, 3, 3);
                    }

                    hasPrev = true;
                    prevFrame = f;
                    prevX = x;
                    prevY = y;
                }
            } else {
                // ----------------------------------------------------
                // Mode A: 高サンプル密度 (ピクセル区間 [Fx, Fx+1] の Min/Max エンベロープ)
                // ----------------------------------------------------
                bool prevPixelValid = false;
                int prevYMin = centerY;
                int prevYMax = centerY;

                for (int x = 0; x < plotWidth; ++x) {
                    double t0 = rangeInfo.leftTime + (static_cast<double>(x) / plotWidth) * m_timeWindowSec;
                    double t1 = rangeInfo.leftTime + (static_cast<double>(x + 1) / plotWidth) * m_timeWindowSec;

                    int64_t f0 = static_cast<int64_t>(std::floor(t0 * m_sampleRate));
                    int64_t f1 = static_cast<int64_t>(std::ceil(t1 * m_sampleRate));
                    if (f1 <= f0) f1 = f0 + 1;

                    if (f0 < 0) f0 = 0;
                    if (f1 > static_cast<int64_t>(m_lastFrameIndex) + 1) {
                        f1 = static_cast<int64_t>(m_lastFrameIndex) + 1;
                    }

                    float minVal = 1.0f;
                    float maxVal = -1.0f;
                    bool validFound = false;

                    for (int64_t f = f0; f < f1; ++f) {
                        uint64_t uFrame = static_cast<uint64_t>(f);
                        size_t slot = uFrame % m_bufferCapacity;
                        if (m_frameIndices[slot] == uFrame) {
                            float v = m_channelBuffers[ch][slot];
                            if (!validFound) {
                                minVal = v;
                                maxVal = v;
                                validFound = true;
                            } else {
                                if (v < minVal) minVal = v;
                                if (v > maxVal) maxVal = v;
                            }
                            float absV = std::fabs(v);
                            if (absV > peakAbs) peakAbs = absV;
                        }
                    }

                    if (!validFound) {
                        prevPixelValid = false;
                        continue;
                    }

                    minVal = std::max(-1.0f, std::min(1.0f, minVal));
                    maxVal = std::max(-1.0f, std::min(1.0f, maxVal));

                    int yMin = centerY - static_cast<int>(maxVal * m_verticalGain * halfHeight);
                    int yMax = centerY - static_cast<int>(minVal * m_verticalGain * halfHeight);

                    if (yMin > yMax) std::swap(yMin, yMax);
                    if (yMin == yMax) yMax = yMin + 1;

                    yMin = std::max(topY + 1, std::min(bottomY - 1, yMin));
                    yMax = std::max(topY + 1, std::min(bottomY - 1, yMax));

                    if (prevPixelValid) {
                        int connMin = std::min(yMin, prevYMax);
                        int connMax = std::max(yMax, prevYMin);
                        dc.DrawLine(x, connMin, x, connMax);
                    } else {
                        dc.DrawLine(x, yMin, x, yMax);
                    }

                    prevPixelValid = true;
                    prevYMin = yMin;
                    prevYMax = yMax;
                }
            }
        }

        // チャネル名ラベルとピーク値表示
        dc.SetFont(labelFont);
        dc.SetTextForeground(chCol);
        wxString label = wxString::Format(wxT("CH %d"), ch + 1);
        wxString unitStr = wxT("V");
        if (ch < static_cast<int>(m_channelMetadata.size())) {
            if (!m_channelMetadata[ch].name.empty()) {
                label = wxString::FromUTF8(m_channelMetadata[ch].name.c_str());
            }
            if (!m_channelMetadata[ch].unit.empty()) {
                unitStr = wxString::FromUTF8(m_channelMetadata[ch].unit.c_str());
            }
        }
        wxString labelWithUnit = wxString::Format(wxT("%s [%s]"), label, unitStr);
        dc.DrawText(labelWithUnit, 8, topY + 4);

        double peakVolt = peakAbs * m_fullScaleVoltage;
        wxString peakStr = wxString::Format(wxT("Peak: %.3f %s (%+.1f dBFS)"), peakVolt, unitStr,
            (peakAbs > 1e-5f) ? (20.0f * std::log10(peakAbs)) : -100.0f);
        if (peakAbs >= 0.99f) {
            dc.SetTextForeground(wxColour(255, 60, 60)); // クリップ警告赤
            peakStr += wxT(" CLIP!");
        } else {
            dc.SetTextForeground(wxColour(160, 170, 180));
        }
        dc.DrawText(peakStr, 110, topY + 4);

        // ========================================================
        // グラフィカル VU/Peak レベルメーター描画 (右端)
        // ========================================================
        int meterW = 12;
        int meterX = width - meterW - 8;
        int meterY = topY + 6;
        int meterH = chH - 12;

        if (meterX > 180 && meterH >= 12) {
            // メーター外枠と背景スロット
            dc.SetPen(wxPen(wxColour(50, 60, 75), 1, wxPENSTYLE_SOLID));
            dc.SetBrush(wxBrush(wxColour(12, 15, 20)));
            dc.DrawRectangle(meterX, meterY, meterW, meterH);

            // 最上部: クリップLEDインジケータ (2px)
            int clipH = 3;
            wxColour clipCol = (peakAbs >= 0.99f) ? wxColour(255, 30, 30) : wxColour(55, 15, 15);
            dc.SetPen(wxPen(clipCol, 1, wxPENSTYLE_SOLID));
            dc.SetBrush(wxBrush(clipCol));
            dc.DrawRectangle(meterX + 1, meterY + 1, meterW - 2, clipH);

            // LEDバーセグメント描画 (下から上へ)
            int barStartY = meterY + clipH + 2;
            int barEndY = meterY + meterH - 2;
            int barTotalH = barEndY - barStartY;

            if (barTotalH > 4) {
                int segStep = 3; // 2px bar + 1px gap
                for (int y = barEndY - 2; y >= barStartY; y -= segStep) {
                    float ratio = static_cast<float>(barEndY - y) / static_cast<float>(barTotalH);
                    bool lit = (ratio <= peakAbs);

                    wxColour segCol;
                    if (lit) {
                        if (ratio >= 0.85f) {
                            segCol = wxColour(255, 50, 50);   // 赤 (オーバーロード警告)
                        } else if (ratio >= 0.60f) {
                            segCol = wxColour(240, 190, 30);  // 黄 (適正高レベル)
                        } else {
                            segCol = wxColour(40, 200, 70);   // 緑 (適正レベル)
                        }
                    } else {
                        segCol = wxColour(20, 25, 32);
                    }

                    dc.SetPen(wxPen(segCol, 1, wxPENSTYLE_SOLID));
                    dc.SetBrush(wxBrush(segCol));
                    dc.DrawRectangle(meterX + 2, y, meterW - 4, 2);
                }
            }
        }
    }

    // ========================================================
    // 時間軸領域描画 (下部)
    // ========================================================
    if (timeAxisHeight > 0) {
        // 時間軸ベースライン
        dc.SetPen(wxPen(wxColour(70, 85, 105), 1, wxPENSTYLE_SOLID));
        dc.DrawLine(plotLeft, plotHeight, plotLeft + plotWidth, plotHeight);

        wxFont axisFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
        dc.SetFont(axisFont);
        dc.SetTextForeground(wxColour(160, 175, 195));

        // 11個の目盛線と相対時間ラベル
        for (int i = 0; i <= 10; ++i) {
            int x_i = plotLeft + static_cast<int>(std::round(static_cast<double>(i * plotWidth) / 10.0));
            // 目盛刻み線 (長さ4px)
            dc.SetPen(wxPen(wxColour(70, 85, 105), 1, wxPENSTYLE_SOLID));
            dc.DrawLine(x_i, plotHeight, x_i, plotHeight + 4);

            if (i < static_cast<int>(scaleInfo.divLabels.size())) {
                wxString lbl = wxString::FromUTF8(scaleInfo.divLabels[i].c_str());
                wxSize sz = dc.GetTextExtent(lbl);
                int tx = x_i - sz.GetWidth() / 2;
                if (i == 0) tx = std::max(plotLeft + 2, tx);
                if (i == 10) tx = std::min(plotLeft + plotWidth - sz.GetWidth() - 2, tx);
                dc.DrawText(lbl, tx, plotHeight + 6);
            }
        }

        // 中心時刻・時間スケール・目盛説明テキスト
        wxFont infoFont(8, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        dc.SetFont(infoFont);
        dc.SetTextForeground(wxColour(200, 215, 235));

        wxString centerLabel;
        wxString centerTimeStr;
        if (m_isPlaybackMode) {
            centerLabel = TrWx(StringId::CenterTimePlayback);
            if (m_playbackData.totalFrames > 0) {
                centerTimeStr = wxString::FromUTF8(FormatPlaybackTime(m_playbackData.centerTimeSec).c_str());
            } else {
                centerTimeStr = wxT("—");
            }
        } else {
            if (rangeInfo.hasData) {
                switch (m_timeRefMode) {
                case TimeReferenceMode::PreviewStart:
                    centerLabel = TrWx(StringId::CenterTimePreview);
                    break;
                case TimeReferenceMode::RecordStart:
                    centerLabel = TrWx(StringId::CenterTimeRecord);
                    break;
                default:
                    if (!m_timeReferenceLabel.IsEmpty()) {
                        centerLabel = wxString::Format(wxT("%s (%s)"), TrWx(StringId::CenterTimeDefault), m_timeReferenceLabel);
                    } else {
                        centerLabel = TrWx(StringId::CenterTimeDefault);
                    }
                    break;
                }
            } else {
                centerLabel = TrWx(StringId::CenterTimeDefault);
            }
            centerTimeStr = wxString::FromUTF8(rangeInfo.centerTimeString.c_str());
        }

        wxString scalePart = TrFormatWx(StringId::ScaleExplanationFormat, scaleInfo.scaleString);
        wxString infoStr = wxString::Format(wxT("%s: %s    %s"), centerLabel, centerTimeStr, scalePart);
        dc.DrawText(infoStr, plotLeft + 8, plotHeight + 25);
    }

    // 読み込み中バッジ (画面左上、再生モード時)
    if (m_isPlaybackMode && m_playbackData.isLoading) {
        wxString loadStr = TrWx(StringId::BadgeLoading);
        wxFont loadFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        dc.SetFont(loadFont);
        wxSize textSize = dc.GetTextExtent(loadStr);
        int badgeW = textSize.GetWidth() + 16;
        int badgeH = textSize.GetHeight() + 8;
        int badgeX = plotLeft + 8;
        int badgeY = 6;
        dc.SetBrush(wxBrush(wxColour(30, 80, 160)));
        dc.SetPen(wxPen(wxColour(70, 150, 255), 1));
        dc.DrawRoundedRectangle(badgeX, badgeY, badgeW, badgeH, 4);
        dc.SetTextForeground(*wxWHITE);
        dc.DrawText(loadStr, badgeX + 8, badgeY + 4);
    }

    // 波形ホールド中バッジ (画面右上、ライブ入力時のみ)
    if (m_isHold && !m_isPlaybackMode) {
        wxString holdStr = TrWx(StringId::BadgeHold);
        wxFont holdFont(9, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD);
        dc.SetFont(holdFont);
        wxSize textSize = dc.GetTextExtent(holdStr);
        int badgeW = textSize.GetWidth() + 16;
        int badgeH = textSize.GetHeight() + 8;
        int badgeX = width - badgeW - ((meterWidth > 0) ? (meterWidth + 12) : 28);
        int badgeY = 6;
        dc.SetBrush(wxBrush(wxColour(210, 100, 20)));
        dc.SetPen(wxPen(wxColour(255, 180, 50), 1));
        dc.DrawRoundedRectangle(badgeX, badgeY, badgeW, badgeH, 4);
        dc.SetTextForeground(*wxWHITE);
        dc.DrawText(holdStr, badgeX + 8, badgeY + 4);
    }
}

} // namespace dlog
