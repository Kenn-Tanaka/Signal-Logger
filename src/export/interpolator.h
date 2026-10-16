#pragma once

#include "wav_export_types.h"
#include <vector>
#include <functional>
#include <cstddef>
#include <cstdint>

namespace dlog {

/**
 * @brief WAVエクスポート用ストリーミング補間器
 * ゼロ次ホールド、線形補間、ポリフェーズ帯域制限FIR補間をサポート
 */
class Interpolator {
public:
    Interpolator();
    ~Interpolator() = default;

    /**
     * @brief 補間器を初期化
     * @param channels 出力チャネル数
     * @param factor アップサンプリング間引き倍率 N (>= 1)
     * @param method 補間方式
     */
    void Initialize(int channels, uint32_t factor, InterpolationMethod method);

    /**
     * @brief 補間に必要な前マージン (過去サンプル数)
     */
    int GetPreMargin() const { return m_preMargin; }

    /**
     * @brief 補間に必要な後マージン (未来・先読みサンプル数)
     */
    int GetPostMargin() const { return m_postMargin; }

    /**
     * @brief チャネル数
     */
    int GetChannels() const { return m_channels; }

    /**
     * @brief アップサンプリング倍率
     */
    uint32_t GetFactor() const { return m_factor; }

    /**
     * @brief 補間方式
     */
    InterpolationMethod GetMethod() const { return m_method; }

    /**
     * @brief チャンク単位のサンプル補間処理
     * @param inputWithMargins 前後マージンを含む入力サンプル配列 (インターリーブ)
     *                         要素数: (inputFrameCount + preMargin + postMargin) * channels
     * @param inputFrameCount 補間対象とする有効入力フレーム数
     * @param onOutputChunk 出力チャンク受け取りコールバック (返り値 false で中断)
     * @param maxOutputChunkFrames 1回に生成・出力する最大フレーム数 (既定 4096)
     * @return 処理完了なら true、コールバックが false を返して中断した場合は false
     */
    bool ProcessFrames(const float* inputWithMargins, size_t inputFrameCount,
                       const std::function<bool(const float* outChunk, size_t outFrames)>& onOutputChunk,
                       size_t maxOutputChunkFrames = 4096);

    /**
     * @brief 単一の入力フレーム区間 [k, k+1) に対する補間サンプル生成 (テスト・検証用)
     * @param inputWithMargins 前後マージンを含む入力サンプル配列 (フレーム数: 1 + preMargin + postMargin)
     * @param outSamples 出力バッファ (要素数: factor * channels)
     */
    void InterpolateSingleFrame(const float* inputWithMargins, float* outSamples);

private:
    void PrecomputeFirTaps();

    int m_channels = 1;
    uint32_t m_factor = 1;
    InterpolationMethod m_method = InterpolationMethod::Linear;

    int m_preMargin = 0;
    int m_postMargin = 0;
    int m_firSemiLength = 4; // P = 4 (8 taps per phase)

    // ポリフェーズフィルタ係数テーブル: [phase s][tapIndex m]
    // s in [0, factor - 1], m in [0, 2*P - 1]
    std::vector<std::vector<float>> m_polyphaseTaps;

    // 出力用再利用バッファ
    std::vector<float> m_outputBuffer;
};

} // namespace dlog
