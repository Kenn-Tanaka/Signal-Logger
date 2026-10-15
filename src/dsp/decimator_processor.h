#pragma once

#include "audio/audio_types.h"
#include <vector>
#include <memory>

namespace dlog {

/**
 * @brief アンチエイリアス・ローパスフィルタ付きデシメーション処理クラス
 * ネイティブレートから目標低レートへの高品質ダウンサンプリングを実施
 */
class DecimatorProcessor {
public:
    DecimatorProcessor();
    ~DecimatorProcessor();

    /**
     * @brief デシメータの初期化
     * @param channels チャネル数
     * @param factor デシメーション比（1 = 間引きなし, 2 = 1/2間引き, 4 = 1/4間引き等）
     * @param enableAntiAliasing アンチエイリアスLPF有効/無効 (falseの場合は単純間引き)
     */
    void Initialize(int channels, int factor, bool enableAntiAliasing = true);

    /**
     * @brief 入力ブロックを処理し、デシメーション済みブロックを出力
     * @param inBlock 入力オーディオブロック（ネイティブレート）
     * @param outBlock 出力オーディオブロック（デシメーション後）
     */
    void Process(const AudioBlock& inBlock, AudioBlock& outBlock);

    /**
     * @brief 現在のデシメーション係数を取得
     */
    int GetFactor() const { return m_factor; }

    /**
     * @brief アンチエイリアスLPFが有効か取得
     */
    bool IsAntiAliasingEnabled() const { return m_enableAntiAliasing; }

private:
    int m_channels = 2;
    int m_factor = 1;
    bool m_enableAntiAliasing = true;

    // 各チャネルごとのFIRフィルタ遅延バッファ
    std::vector<std::vector<float>> m_channelFilterHistory;

    // FIR フィルタ係数（線形位相ローパス）
    std::vector<float> m_filterTaps;

    // 低周波ロギング用ボックスカー積分累積バッファ (factor > 8 時)
    std::vector<double> m_accumSum;
    size_t m_accumCount = 0;

    void SetupFilter(int factor);
};

} // namespace dlog
