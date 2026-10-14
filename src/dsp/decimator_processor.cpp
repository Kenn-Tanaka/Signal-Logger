#include "dsp/decimator_processor.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace dlog {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

DecimatorProcessor::DecimatorProcessor() {
    Initialize(2, 1);
}

DecimatorProcessor::~DecimatorProcessor() = default;

void DecimatorProcessor::SetupFilter(int factor) {
    m_filterTaps.clear();
    if (factor <= 1) {
        // 間引きなしの場合はフィルタなし（1.0 のインパルス）
        m_filterTaps.push_back(1.0f);
        return;
    }

    // 奇数長のFIRローパスフィルタ設計（窓関数法: Sinc + Hamming window）
    // タップ数は低レイテンシかつ十分な阻止域減衰を確保するため 17 タップ
    const int numTaps = 17;
    m_filterTaps.resize(numTaps);
    const int mid = numTaps / 2;
    const double cutoff = 0.45 / static_cast<double>(factor); // 正規化カットオフ周波数 (0.0〜0.5)

    double sum = 0.0;
    for (int i = 0; i < numTaps; ++i) {
        int n = i - mid;
        double sinc;
        if (n == 0) {
            sinc = 2.0 * cutoff;
        } else {
            sinc = std::sin(2.0 * M_PI * cutoff * n) / (M_PI * n);
        }
        // ハミング窓
        double hamming = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / (numTaps - 1));
        double coeff = sinc * hamming;
        m_filterTaps[i] = static_cast<float>(coeff);
        sum += coeff;
    }

    // DCゲインを 1.0 に正規化
    if (sum > 0.0) {
        for (float& tap : m_filterTaps) {
            tap = static_cast<float>(tap / sum);
        }
    }
}

void DecimatorProcessor::Initialize(int channels, int factor) {
    m_channels = std::max(1, std::min(channels, static_cast<int>(MAX_AUDIO_CHANNELS)));
    m_factor = std::max(1, factor);
    SetupFilter(m_factor);

    // チャネルごとの遅延履歴バッファを初期化
    m_channelFilterHistory.clear();
    m_channelFilterHistory.resize(m_channels);
    size_t histSize = m_filterTaps.empty() ? 1 : m_filterTaps.size();
    for (int ch = 0; ch < m_channels; ++ch) {
        m_channelFilterHistory[ch].assign(histSize, 0.0f);
    }

    // 低周波用ボックスカーアキュムレータ初期化
    m_accumSum.assign(m_channels, 0.0);
    m_accumCount = 0;
}

void DecimatorProcessor::Process(const AudioBlock& inBlock, AudioBlock& outBlock) {
    outBlock.channels = m_channels;
    outBlock.sampleRate = (m_factor > 1) ? (inBlock.sampleRate / m_factor) : inBlock.sampleRate;
    outBlock.timestampUtcMs = inBlock.timestampUtcMs;
    outBlock.frameCount = 0;

    if (inBlock.channels != m_channels || inBlock.channels < 1 ||
        inBlock.channels > static_cast<int>(MAX_AUDIO_CHANNELS) ||
        inBlock.frameCount > MAX_AUDIO_FRAMES) {
        return;
    }

    if (m_factor <= 1 || inBlock.frameCount == 0) {
        // パススルー
        outBlock.frameCount = inBlock.frameCount;
        size_t totalSamples = outBlock.frameCount * static_cast<size_t>(outBlock.channels);
        std::memcpy(outBlock.samples, inBlock.samples, totalSamples * sizeof(float));
        return;
    }

    const int channels = m_channels;
    const size_t inFrames = inBlock.frameCount;

    if (m_factor > 8) {
        // ========================================================
        // 低周波電気信号ロガーモード (factor > 8: 1000Hz, 100Hz, 10Hz, 1Hz等)
        // ボックスカー積分平均（CIC / 移動平均デシメーション）
        // 高周波・商用電源ノイズを積分相殺し、直流平均電圧を正確に抽出
        // ========================================================
        size_t outFrames = 0;
        for (size_t f = 0; f < inFrames; ++f) {
            for (int ch = 0; ch < channels; ++ch) {
                m_accumSum[ch] += inBlock.samples[f * static_cast<size_t>(inBlock.channels) + ch];
            }
            m_accumCount++;

            if (m_accumCount >= static_cast<size_t>(m_factor)) {
                if (outFrames < MAX_AUDIO_FRAMES) {
                    for (int ch = 0; ch < channels; ++ch) {
                        float avgVal = static_cast<float>(m_accumSum[ch] / static_cast<double>(m_accumCount));
                        outBlock.samples[outFrames * static_cast<size_t>(channels) + ch] = avgVal;
                        m_accumSum[ch] = 0.0;
                    }
                    outFrames++;
                } else {
                    for (int ch = 0; ch < channels; ++ch) {
                        m_accumSum[ch] = 0.0;
                    }
                }
                m_accumCount = 0;
            }
        }
        outBlock.frameCount = outFrames;
        return;
    }

    // ========================================================
    // 高速オーディオレートモード (factor <= 8: 1/2, 1/4, 1/8)
    // 17タップ 線形位相FIRローパスフィルタ
    // ========================================================
    const size_t outFrames = inFrames / static_cast<size_t>(m_factor);
    outBlock.frameCount = outFrames;

    const size_t tapCount = m_filterTaps.size();

    // 各チャネルごとにフィルタリングと間引きを実行
    for (int ch = 0; ch < channels; ++ch) {
        std::vector<float>& history = m_channelFilterHistory[ch];

        for (size_t outIdx = 0; outIdx < outFrames; ++outIdx) {
            size_t inSampleIdx = outIdx * static_cast<size_t>(m_factor);

            // インパルス応答（畳み込み演算）
            float filteredSample = 0.0f;
            for (size_t tap = 0; tap < tapCount; ++tap) {
                float sampleVal = 0.0f;
                if (inSampleIdx >= tap) {
                    size_t pastFrame = inSampleIdx - tap;
                    sampleVal = inBlock.samples[pastFrame * static_cast<size_t>(inBlock.channels) + ch];
                } else {
                    // ブロック前の履歴バッファから参照
                    size_t histIdx = history.size() - (tap - inSampleIdx);
                    if (histIdx < history.size()) {
                        sampleVal = history[histIdx];
                    }
                }
                filteredSample += sampleVal * m_filterTaps[tap];
            }

            // インターリーブ形式で出力
            outBlock.samples[outIdx * static_cast<size_t>(channels) + ch] = filteredSample;
        }

        // 次のブロック用に末尾サンプルを履歴バッファへ退避
        for (size_t h = 0; h < history.size(); ++h) {
            if (inFrames >= history.size() - h) {
                size_t framePos = inFrames - (history.size() - h);
                history[h] = inBlock.samples[framePos * static_cast<size_t>(inBlock.channels) + ch];
            }
        }
    }
}

} // namespace dlog
