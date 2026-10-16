#include "interpolator.h"
#include <cmath>
#include <algorithm>

namespace dlog {

static constexpr double PI = 3.14159265358979323846;

Interpolator::Interpolator() = default;

void Interpolator::Initialize(int channels, uint32_t factor, InterpolationMethod method) {
    m_channels = std::max(1, channels);
    m_factor = std::max(1u, factor);
    m_method = method;

    if (m_factor == 1) {
        m_preMargin = 0;
        m_postMargin = 0;
        m_polyphaseTaps.clear();
        return;
    }

    switch (m_method) {
        case InterpolationMethod::ZeroOrderHold:
            m_preMargin = 0;
            m_postMargin = 0;
            m_polyphaseTaps.clear();
            break;
        case InterpolationMethod::Linear:
            m_preMargin = 0;
            m_postMargin = 1; // 1 frame lookahead for x[k + 1]
            m_polyphaseTaps.clear();
            break;
        case InterpolationMethod::BandlimitedFir:
            m_firSemiLength = 4;
            m_preMargin = 3;  // m in [-3, 4] -> 3 past samples
            m_postMargin = 4; // 4 future samples
            PrecomputeFirTaps();
            break;
    }
}

void Interpolator::PrecomputeFirTaps() {
    int P = m_firSemiLength;
    int numTaps = 2 * P; // 8 taps per phase (m = -3, -2, -1, 0, 1, 2, 3, 4)
    m_polyphaseTaps.assign(m_factor, std::vector<float>(numTaps, 0.0f));

    for (uint32_t s = 0; s < m_factor; ++s) {
        double tau = static_cast<double>(s) / static_cast<double>(m_factor);
        double sumWeights = 0.0;
        std::vector<double> raw(numTaps, 0.0);

        for (int tap = 0; tap < numTaps; ++tap) {
            int m = tap - (P - 1); // tap 0 -> m = -3, tap 3 -> m = 0, tap 7 -> m = 4
            double d = static_cast<double>(m) - tau;

            double sincVal = 0.0;
            if (std::abs(d) < 1e-9) {
                sincVal = 1.0;
            } else {
                sincVal = std::sin(PI * d) / (PI * d);
            }

            // Blackman 窓: w(d) = 0.42 + 0.5 * cos(pi * d / P) + 0.08 * cos(2 * pi * d / P) for |d| <= P
            double w = 0.0;
            if (std::abs(d) <= static_cast<double>(P)) {
                double angle = PI * d / static_cast<double>(P);
                w = 0.42 + 0.5 * std::cos(angle) + 0.08 * std::cos(2.0 * angle);
            }

            raw[tap] = sincVal * w;
            sumWeights += raw[tap];
        }

        // DCゲイン正規化 (sum of weights = 1.0)
        double invSum = (std::abs(sumWeights) > 1e-12) ? (1.0 / sumWeights) : 1.0;
        for (int tap = 0; tap < numTaps; ++tap) {
            m_polyphaseTaps[s][tap] = static_cast<float>(raw[tap] * invSum);
        }
    }
}

bool Interpolator::ProcessFrames(const float* inputWithMargins, size_t inputFrameCount,
                                 const std::function<bool(const float* outChunk, size_t outFrames)>& onOutputChunk,
                                 size_t maxOutputChunkFrames) {
    if (!inputWithMargins || inputFrameCount == 0) return true;

    if (maxOutputChunkFrames == 0) maxOutputChunkFrames = 4096;
    size_t outCapacityFloats = maxOutputChunkFrames * static_cast<size_t>(m_channels);
    if (m_outputBuffer.size() < outCapacityFloats) {
        m_outputBuffer.resize(outCapacityFloats);
    }

    size_t outFramesAccum = 0;

    for (size_t k = 0; k < inputFrameCount; ++k) {
        size_t centerOffset = (k + static_cast<size_t>(m_preMargin)) * static_cast<size_t>(m_channels);

        if (m_factor == 1) {
            // Factor 1: 直接コピー
            for (int c = 0; c < m_channels; ++c) {
                m_outputBuffer[outFramesAccum * m_channels + c] = inputWithMargins[centerOffset + c];
            }
            outFramesAccum++;
            if (outFramesAccum >= maxOutputChunkFrames) {
                if (!onOutputChunk(m_outputBuffer.data(), outFramesAccum)) return false;
                outFramesAccum = 0;
            }
        } else if (m_method == InterpolationMethod::ZeroOrderHold) {
            // ゼロ次ホールド: 入力サンプルを factor 回リピート
            for (uint32_t s = 0; s < m_factor; ++s) {
                for (int c = 0; c < m_channels; ++c) {
                    m_outputBuffer[outFramesAccum * m_channels + c] = inputWithMargins[centerOffset + c];
                }
                outFramesAccum++;
                if (outFramesAccum >= maxOutputChunkFrames) {
                    if (!onOutputChunk(m_outputBuffer.data(), outFramesAccum)) return false;
                    outFramesAccum = 0;
                }
            }
        } else if (m_method == InterpolationMethod::Linear) {
            // 線形補間
            size_t nextOffset = (k + static_cast<size_t>(m_preMargin) + 1) * static_cast<size_t>(m_channels);
            for (uint32_t s = 0; s < m_factor; ++s) {
                float alpha = static_cast<float>(s) / static_cast<float>(m_factor);
                float oneMinusAlpha = 1.0f - alpha;

                for (int c = 0; c < m_channels; ++c) {
                    float v0 = inputWithMargins[centerOffset + c];
                    float v1 = inputWithMargins[nextOffset + c];
                    m_outputBuffer[outFramesAccum * m_channels + c] = oneMinusAlpha * v0 + alpha * v1;
                }
                outFramesAccum++;
                if (outFramesAccum >= maxOutputChunkFrames) {
                    if (!onOutputChunk(m_outputBuffer.data(), outFramesAccum)) return false;
                    outFramesAccum = 0;
                }
            }
        } else if (m_method == InterpolationMethod::BandlimitedFir) {
            // 帯域制限補間 (FIR, 8 taps)
            int P = m_firSemiLength;
            int numTaps = 2 * P;

            for (uint32_t s = 0; s < m_factor; ++s) {
                const float* taps = m_polyphaseTaps[s].data();

                for (int c = 0; c < m_channels; ++c) {
                    float sum = 0.0f;
                    for (int tap = 0; tap < numTaps; ++tap) {
                        int m = tap - (P - 1); // -3 .. +4
                        size_t sampleOffset = (k + static_cast<size_t>(m_preMargin + m)) * static_cast<size_t>(m_channels) + c;
                        sum += inputWithMargins[sampleOffset] * taps[tap];
                    }
                    m_outputBuffer[outFramesAccum * m_channels + c] = sum;
                }
                outFramesAccum++;
                if (outFramesAccum >= maxOutputChunkFrames) {
                    if (!onOutputChunk(m_outputBuffer.data(), outFramesAccum)) return false;
                    outFramesAccum = 0;
                }
            }
        }
    }

    if (outFramesAccum > 0) {
        if (!onOutputChunk(m_outputBuffer.data(), outFramesAccum)) return false;
    }

    return true;
}

void Interpolator::InterpolateSingleFrame(const float* inputWithMargins, float* outSamples) {
    if (!inputWithMargins || !outSamples) return;
    ProcessFrames(inputWithMargins, 1, [&](const float* chunk, size_t frames) {
        std::copy(chunk, chunk + frames * m_channels, outSamples);
        return true;
    }, m_factor);
}

} // namespace dlog
