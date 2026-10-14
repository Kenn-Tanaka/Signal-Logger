#pragma once

#include <string>
#include <cstdint>

namespace dlog {

/**
 * @brief 再生時刻文字列の解析とサンプル点へのスナップ
 * 
 * 対応書式:
 *  - HH:MM:SS (例: 01:23:45)
 *  - HH:MM:SS.ffffff (例: 00:12:34.500)
 *  - MM:SS.ffffff (例: 12:34.5)
 *  - 秒数 (例: 123.456, 10)
 * 
 * 負値、非有限値、範囲外（> maxPosition）、不正書式はエラーを返します。
 * 入力値を黙って丸めません。
 * 成功時は最も近いサンプル点へ合わせ、実際に移動した時刻を outAlignedSec に格納します。
 */
bool ParsePlaybackTimeString(const std::string& input,
                             double maxPosition,
                             double Fs,
                             double& outAlignedSec,
                             std::string& errorMsg);

/**
 * @brief 秒数を HH:MM:SS.mmm 形式の文字列へ整形
 */
std::string FormatPlaybackTime(double seconds, bool includeMicros = false);

/**
 * @brief 移動可能な最大位置（最後のサンプル時刻: (frameCount - 1) / Fs）を計算
 */
double CalculateMaxPosition(uint64_t totalFrames, double Fs);

/**
 * @brief データの総時間（frameCount / Fs）を計算
 */
double CalculateTotalDuration(uint64_t totalFrames, double Fs);

/**
 * @brief 指定時刻をサンプルフレーム番号に変換（最寄り丸め、[0, totalFrames - 1] にクリップ）
 */
uint64_t TimeToFrameIndex(double timeSec, double Fs, uint64_t totalFrames);

/**
 * @brief フレーム番号から正確な時刻を計算
 */
double FrameIndexToTime(uint64_t frameIndex, double Fs);

/**
 * @brief フレーム番号からファイル内バイトオフセットを計算
 */
uint64_t FrameIndexToFileOffset(uint64_t frameIndex, uint32_t headerSize, int channels, size_t bytesPerSample = sizeof(float));

} // namespace dlog
