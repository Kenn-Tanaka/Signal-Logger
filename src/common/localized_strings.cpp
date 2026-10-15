#include "common/localized_strings.h"
#include <unordered_map>
#include <sstream>
#include <set>
#include <regex>

namespace dlog {

namespace {

struct TranslationEntry {
    const char* ja;
    const char* en;
};

// 全StringIdに対する翻訳テーブル
const std::unordered_map<StringId, TranslationEntry> g_translations = {
    // --- アプリケーション全般・メニュー ---
    { StringId::AppTitleLogger, { "シグナル・ロガー", "Signal Logger" } },
    { StringId::AppTitleConverter, { "シグナル・コンバータ", "Signal Converter" } },
    { StringId::MenuLanguage, { "言語 / Language", "言語 / Language" } },
    { StringId::MenuLanguageJapanese, { "日本語", "日本語" } },
    { StringId::MenuLanguageEnglish, { "English", "English" } },

    // --- ノートブック タブ ---
    { StringId::TabLiveInput, { "ライブ入力", "Live Input" } },
    { StringId::TabPlayback, { "記録データ再生", "Recorded Waveform Playback" } },

    // --- ライブ入力 設定 & コントロール ---
    { StringId::BoxSettingsAndControls, { " 設定・コントロール ", " Settings & Controls " } },
    { StringId::LabelInputDevice, { "入力デバイス:", "Input Device:" } },
    { StringId::ButtonRefresh, { "更新", "Refresh" } },
    { StringId::DeviceNotFound, { "入力デバイスが見つかりません", "No input devices found" } },
    { StringId::LabelChannels, { "チャネル数:", "Channels:" } },
    { StringId::ButtonChannelConfig, { "チャネル設定...", "Channel Settings..." } },
    { StringId::TipChannelConfig, { "各チャネルの信号名称および物理単位を設定します", "Configure signal name and physical unit for each channel" } },
    { StringId::LabelInputRate, { "入力レート:", "Input Rate:" } },
    { StringId::LabelDecimation, { "デシメーション:", "Decimation:" } },
    { StringId::CheckAntiAliasing, { "アンチエイリアス", "Anti-Aliasing" } },
    { StringId::TipAntiAliasing, { "デシメーション時のローパスフィルタ（FIR／移動平均）を適用します。オフにすると単純間引き（ポイントサンプリング）を行い、フィルタ遅延やリンギングを排除します。", "Apply low-pass filter (FIR / moving average) during decimation. When turned off, point sampling is used to eliminate filter delay and ringing." } },
    { StringId::TargetRateFormat, { "目標レート: {0} Hz", "Target Rate: {0} Hz" } },
    { StringId::LabelFullScaleVoltage, { "フルスケール電圧[V]:", "Full-Scale Voltage [V]:" } },
    { StringId::ButtonCalibrate, { "校正...", "Calibrate..." } },
    { StringId::ButtonAcGuide, { "AC結合について...", "About AC Coupling..." } },
    { StringId::TipAcGuide, { "PCオーディオI/Fのハードウェア回路特性（直流遮断・AC結合）に関するガイダンスを表示します", "Display guidance regarding hardware circuitry (DC blocking / AC coupling) of PC audio interfaces" } },
    { StringId::LabelSavePath, { "保存先:", "Save Path:" } },
    { StringId::ButtonBrowse, { "参照...", "Browse..." } },
    { StringId::ButtonPreviewStart, { "プレビュー開始", "Start Preview" } },
    { StringId::ButtonPreviewStop, { "プレビュー停止", "Stop Preview" } },
    { StringId::ButtonRecordStart, { "記録開始", "Start Recording" } },
    { StringId::ButtonRecordStop, { "記録停止", "Stop Recording" } },

    // --- 再生コントロール ---
    { StringId::BoxPlaybackControl, { " 再生コントロール ", " Playback Controls " } },
    { StringId::LabelPlaybackFile, { "ファイル:", "File:" } },
    { StringId::ButtonOpenFile, { "開く...", "Open..." } },
    { StringId::ButtonPlayFirst, { "⏮ 先頭", "⏮ First" } },
    { StringId::ButtonPlayStart, { "▶ 再生", "▶ Play" } },
    { StringId::ButtonPlayPause, { "⏸ 一時停止", "⏸ Pause" } },
    { StringId::ButtonPlayLast, { "⏭ 末尾", "⏭ Last" } },
    { StringId::LabelPlaySpeed, { "再生速度:", "Speed:" } },
    { StringId::LabelPlayPosition, { "再生位置:", "Position:" } },
    { StringId::TipPlayPosition, { "指定書式: HH:MM:SS.ffffff または 秒数。\n※元データのサンプリング周期以上の時間分解能を意味するものではありません（直近サンプル点へ自動調整されます）。", "Format: HH:MM:SS.ffffff or seconds.\n* Does not imply time resolution higher than raw sampling period (snaps to nearest sample point)." } },
    { StringId::ButtonSeek, { "移動", "Seek" } },

    // --- 波形描画オプション & バッジ ---
    { StringId::LabelWaveformSettings, { "波形表示設定:  時間幅:", "Waveform Settings:  Time Window:" } },
    { StringId::LabelVerticalGain, { "垂直ゲイン:", "Vertical Gain:" } },
    { StringId::ButtonHoldWaveform, { "波形ホールド", "Hold Waveform" } },
    { StringId::ButtonResumeWaveform, { "ホールド解除", "Resume Waveform" } },
    { StringId::TipHoldWaveform, { "ディスクへのデータ記録は継続したまま、画面上の波形描画のみを一時停止（静止）します", "Pauses on-screen waveform rendering while data recording to disk continues uninterrupted" } },
    { StringId::LabelChangeDuringRecordHint, { "(※記録中も変更可能)", "(* Can be changed during recording)" } },
    { StringId::BadgeLoading, { "● 読み込み中...", "● Loading..." } },
    { StringId::BadgeHold, { "● HOLD (波形静止中 - 記録継続)", "● HOLD (Waveform paused - recording continues)" } },
    { StringId::CenterTimePlayback, { "中心時刻 (再生位置)", "Center Time (Playback Pos)" } },
    { StringId::CenterTimePreview, { "中心時刻 (プレビュー開始から)", "Center Time (From Preview Start)" } },
    { StringId::CenterTimeRecord, { "中心時刻 (記録開始から)", "Center Time (From Record Start)" } },
    { StringId::CenterTimeDefault, { "中心時刻", "Center Time" } },
    { StringId::ScaleExplanationFormat, { "時間スケール: {0}    目盛: 中心からの相対時間", "Time Scale: {0}    Scale: relative to center" } },
    { StringId::MeterDbfs, { "dBFS", "dBFS" } },
    { StringId::GainStandard, { "1x (±1.0 標準)", "1x (±1.0 Standard)" } },

    // --- 記録ステータス ---
    { StringId::BoxRecordingStatus, { " 記録ステータス ", " Recording Status " } },
    { StringId::StatusStopped, { "状態: 停止中", "Status: Stopped" } },
    { StringId::StatusPreviewing, { "状態: プレビュー中", "Status: Previewing" } },
    { StringId::StatusRecording, { "状態: 記録中", "Status: Recording" } },
    { StringId::StatusErrorFormat, { "状態: エラー ({0})", "Status: Error ({0})" } },
    { StringId::DurationLiveFormat, { "記録時間: {0}", "Duration: {0}" } },
    { StringId::DurationPrevFormat, { "前回記録時間: {0}", "Previous Duration: {0}" } },
    { StringId::FileSizeFormatMb, { "ファイルサイズ: {0} MB", "File Size: {0} MB" } },
    { StringId::FileSizeFormatGb, { "ファイルサイズ: {0} GB", "File Size: {0} GB" } },
    { StringId::OverrunsNormal, { "データ欠落: 0 件 (正常)", "Data Dropouts: 0 (Normal)" } },
    { StringId::OverrunsWarningFormat, { "データ欠落: {0} 件 [警告]", "Data Dropouts: {0} [Warning]" } },
    { StringId::UiDropsFormat, { "UI描画スキップ: {0} 件", "UI Rendering Skips: {0}" } },

    // --- ダイアログ: チャネル設定 ---
    { StringId::ChannelConfigTitle, { "チャネル名称・単位設定", "Channel Name & Unit Settings" } },
    { StringId::ChannelConfigDesc, { "各チャネルの計測信号名（例: V_bat, 歪みゲージ, 負荷電流）および物理単位（例: V, mV, A, kPa）を設定します。\n設定内容はエクスポートされるCSVの列名（例: V_bat[V]）に反映されます。", "Configure signal name (e.g. V_bat, Strain, Current) and unit (e.g. V, mV, A, kPa) for each channel.\nThese are reflected in exported CSV headers (e.g. V_bat[V])." } },
    { StringId::ChannelColChannel, { "チャネル", "Channel" } },
    { StringId::ChannelColLabel, { "信号名称 (Label)", "Signal Name (Label)" } },
    { StringId::ChannelColUnit, { "物理単位 (Unit)", "Physical Unit" } },

    // --- ダイアログ: 電圧校正 ---
    { StringId::CalibratePrompt, { "入力端子に接続している基準信号の Peak-to-Peak 電圧 [Vpp] を入力してください:\n(例: 1.000 Vpp の交流信号)", "Enter the Peak-to-Peak voltage [Vpp] of the reference signal connected to the input:\n(e.g., 1.000 Vpp AC signal)" } },
    { StringId::CalibrateTitle, { "基準信号による電圧校正", "Voltage Calibration by Reference Signal" } },
    { StringId::CalibrateInvalidInput, { "正の電圧値を入力してください。", "Please enter a positive voltage value." } },
    { StringId::CalibrateNoSignalFormat, { "入力信号が検出できません (振幅: {0})。\nオーディオI/Fの接続、信号入力および入力ゲインを確認してください。", "Input signal not detected (amplitude: {0}).\nPlease check audio interface connection, signal input, and input gain." } },
    { StringId::CalibrateErrorTitle, { "校正エラー", "Calibration Error" } },
    { StringId::CalibrateSuccessFormat, { "チャネル1の測定結果に基づきフルスケール電圧を校正しました。\n\n測定 Peak-to-Peak: {0} (デジタル値)\n目標 Peak-to-Peak: {1} Vpp\n計算フルスケール電圧: ±{2} V", "Full-scale voltage has been calibrated based on Channel 1 measurement.\n\nMeasured Peak-to-Peak: {0} (digital)\nTarget Peak-to-Peak: {1} Vpp\nCalculated Full-Scale Voltage: ±{2} V" } },
    { StringId::CalibrateSuccessTitle, { "校正完了", "Calibration Complete" } },
    { StringId::CalibrateDialogTitle, { "電圧校正", "Voltage Calibration" } },
    { StringId::CalibrateHeaderDesc, { "入力端子に接続した基準交流信号の Peak-to-Peak 電圧 [Vpp] を指定して、フルスケール電圧を校正します。", "Calibrate the full-scale voltage by specifying the Peak-to-Peak voltage [Vpp] of the reference AC signal connected to the input." } },
    { StringId::CalibrateChannelLabel, { "対象チャネル:", "Target Channel:" } },
    { StringId::CalibrateTargetVppLabel, { "基準信号 Vpp [V]:", "Reference Signal Vpp [V]:" } },
    { StringId::CalibrateMonitorGroup, { "リアルタイム信号監視", "Live Signal Monitor" } },
    { StringId::CalibrateMeasuredVpp, { "実測デジタル Vpp:", "Measured Digital Vpp:" } },
    { StringId::CalibratePeakValue, { "ピーク振幅 (|V|max):", "Peak Amplitude (|V|max):" } },
    { StringId::CalibrateCalculatedVfs, { "計算フルスケール電圧:", "Calculated Full-Scale Voltage:" } },
    { StringId::CalibrateStatusGood, { "正常・安定（校正可能）", "Good / Stable (Ready to calibrate)" } },
    { StringId::CalibrateStatusLow, { "小信号（ゲイン不足。校正誤差注意）", "Low Signal (Increase input gain)" } },
    { StringId::CalibrateStatusClipping, { "警告: クリップ発生（入力過大。ゲインを下げてください）", "Warning: Clipping (Input overloaded. Reduce gain)" } },
    { StringId::CalibrateStatusNoSignal, { "信号なし（結線または信号出力を確認してください）", "No Signal (Check input connection/output)" } },
    { StringId::CalibrateStartPromptConfirm, { "電圧校正には入力信号のリアルタイム測定が必要です。\nプレビューを開始して校正を行いますか？", "Live signal measurement is required for calibration.\nStart preview and proceed with calibration?" } },
    { StringId::CalibrateBtnApply, { "適用", "Apply" } },
    { StringId::ButtonCancel, { "キャンセル", "Cancel" } },
    { StringId::ErrCannotCalibrateWhileRecording, { "記録中は校正を実行できません。", "Cannot calibrate while recording." } },
    { StringId::CalibrateSuccessWithChFormat, { "チャネル {0} の測定結果に基づきフルスケール電圧を校正しました。\n\n測定 Peak-to-Peak: {1} (デジタル値)\n目標 Peak-to-Peak: {2} Vpp\n計算フルスケール電圧: ±{3} V", "Full-scale voltage has been calibrated based on Channel {0} measurement.\n\nMeasured Peak-to-Peak: {1} (digital)\nTarget Peak-to-Peak: {2} Vpp\nCalculated Full-Scale Voltage: ±{3} V" } },

    // --- ダイアログ: AC結合ガイダンス ---
    { StringId::AcGuideTitle, { "オーディオI/Fのハードウェア特性について（AC結合・DC遮断）", "About Audio Interface Hardware (AC Coupling / DC Blocking)" } },
    { StringId::AcGuideContent, {
        "【PCオーディオインターフェースの入力回路（AC結合・DC遮断）について】\n\n"
        "一般的なPC用オーディオインターフェース（A/Dコンバータ）の入力回路は音声信号用に最適化されており、入力端子にコンデンサ（ハイパスフィルタ）が挿入された「AC結合（交流結合）」回路となっています。\n\n"
        "■ 直流(DC)入力時に観察される挙動:\n"
        "・直流電圧（乾電池や直流電源の一定電圧）を入力した場合、接続した瞬間に電圧値が立ち上がりますが、回路の時定数（通常カットオフ周波数 10 Hz 〜 20 Hz 程度）に従って、指数関数的に 0 V へ減衰して戻ります。\n"
        "・これは本ソフトウェアの不具合ではなく、ハードウェアの物理的な高域通過フィルタによるものです。\n\n"
        "■ 低周波（1 Hz等）測定時の推奨:\n"
        "・交流信号（AC）の変動、あるいは商用電源周波数以上の信号は正常に連続記録可能です。\n"
        "・完全な静的電圧（DCレベル）を長期間継続して測定されたい場合は、DC結合に対応した計測用フロントエンド（計測用A/Dボードや計測用オペアンプバッファ）をご利用ください。",
        "[About PC Audio Interface Hardware Circuitry (AC Coupling / DC Blocking)]\n\n"
        "Standard PC audio interfaces (A/D converters) are designed for audio signals and feature \"AC coupling\" with series capacitors (high-pass filters) on the inputs.\n\n"
        "■ Behavior Observed with DC Input:\n"
        "- When a DC voltage (such as from a battery or DC power supply) is connected, the input signal initially registers the step voltage, but then decays exponentially back to 0 V according to the hardware time constant (typically 10 Hz to 20 Hz cutoff).\n"
        "- This is an intrinsic physical characteristic of the hardware high-pass filter, not a defect in this software.\n\n"
        "■ Recommendations for Low-Frequency (e.g. 1 Hz) Measurement:\n"
        "- AC signal variations and signals at or above utility power frequencies can be measured and recorded continuously.\n"
        "- To measure true static DC voltage levels over long durations, please use a DC-coupled measurement frontend (such as an instrumentation A/D board or instrumentation amplifier buffer)."
    } },

    // --- ダイアログ: 共通確認・警告 ---
    { StringId::ConfirmCloseRecording, { "記録を実行中です。停止して終了しますか？", "Recording is currently in progress. Stop recording and exit?" } },
    { StringId::ConfirmCloseConverting, { "変換を実行中です。中断して終了しますか？", "Conversion is currently in progress. Cancel conversion and exit?" } },
    { StringId::ConfirmDialogTitle, { "確認", "Confirmation" } },
    { StringId::WarningDialogTitle, { "警告", "Warning" } },
    { StringId::ErrorDialogTitle, { "エラー", "Error" } },
    { StringId::InfoDialogTitle, { "情報", "Information" } },

    // --- コンバータ UI ---
    { StringId::BoxTargetFileList, { " 変換対象ファイル一覧 ", " Files to Convert " } },
    { StringId::ColStatus, { "状態", "Status" } },
    { StringId::ColFileName, { "ファイル名", "File Name" } },
    { StringId::ColSampling, { "サンプリング", "Sampling" } },
    { StringId::ColChannels, { "チャネル", "Channels" } },
    { StringId::ColDuration, { "記録時間", "Duration" } },
    { StringId::ColSize, { "サイズ", "Size" } },
    { StringId::ColPath, { "パス", "Path" } },
    { StringId::ButtonAddFiles, { "ファイル追加...", "Add Files..." } },
    { StringId::ButtonAddFolder, { "フォルダ追加...", "Add Folder..." } },
    { StringId::ButtonRemoveSelected, { "選択項目を削除", "Remove Selected" } },
    { StringId::ButtonClearList, { "一覧をクリア", "Clear List" } },
    { StringId::ButtonRecoverPartial, { "不完全ファイル復旧...", "Recover Incomplete File..." } },
    { StringId::BoxConversionOptions, { " 変換オプション ", " Conversion Options " } },
    { StringId::LabelOutputDir, { "出力先:", "Output Directory:" } },
    { StringId::RadioOutSameDir, { "入力ファイルと同じフォルダ", "Same folder as input file" } },
    { StringId::RadioOutCustomDir, { "指定フォルダ:", "Custom folder:" } },
    { StringId::LabelTimeColumnFormat, { "時間列形式:", "Time Column Format:" } },
    { StringId::RadioTimeRelSec, { "相対時間(秒)", "Relative Time (sec)" } },
    { StringId::RadioTimeAbs, { "絶対日時 (UTC)", "Absolute Time (UTC)" } },
    { StringId::RadioTimeIndex, { "サンプル番号", "Sample Index" } },
    { StringId::RadioTimeNone, { "なし", "None" } },
    { StringId::LabelDecimationStep, { "間引き:", "Decimation:" } },
    { StringId::LabelDelimiter, { "区切り:", "Delimiter:" } },
    { StringId::LabelPrecision, { "数値精度:", "Precision:" } },
    { StringId::CheckApplyVoltageScale, { "電圧値[V]に換算して出力", "Convert to voltage [V]" } },
    { StringId::TipApplyVoltageScale, { "ファイルヘッダに記録されたフルスケール電圧を適用して、デジタル値(-1.0〜+1.0)を実電圧[V]に換算します", "Applies full-scale voltage recorded in file header to convert digital values (-1.0 to +1.0) into actual voltage [V]" } },
    { StringId::CheckSplitLines, { "指定行数でファイルを自動分割", "Auto-split file at line count" } },
    { StringId::ButtonExcelLimit, { "Excel上限 (1,048,575行)", "Excel Limit (1,048,575 rows)" } },
    { StringId::CheckAllChannels, { "全チャネル", "All Channels" } },
    { StringId::CheckTimeRange, { "時間範囲を指定して出力 (秒)", "Trim time range (seconds)" } },
    { StringId::LabelTimeRangeFrom, { "開始:", "Start:" } },
    { StringId::LabelTimeRangeTo, { "終了:", "End:" } },
    { StringId::LabelCurrentFileProgress, { "ファイル進捗 ({0}%):", "File Progress ({0}%):" } },
    { StringId::LabelOverallProgress, { "全体進捗 ({0}/{1} - {2}%):", "Overall Progress ({0}/{1} - {2}%):" } },
    { StringId::LabelSpeedAndEtaFormat, { "速度: {0} MB/s | 残り推定時間: {1}", "Speed: {0} MB/s | ETA: {1}" } },
    { StringId::ButtonStartConversion, { "変換開始", "Start Conversion" } },
    { StringId::ButtonCancelConversion, { "中断", "Cancel" } },
    { StringId::ButtonOpenOutputDir, { "出力フォルダを開く", "Open Output Folder" } },

    // --- コンバータ 項目ステータス・進捗・確認 ---
    { StringId::StatusPending, { "待機中", "Pending" } },
    { StringId::StatusConverting, { "● 変換中", "● Converting" } },
    { StringId::StatusConvertingProgressFormat, { "変換中 ({0}%)", "Converting ({0}%)" } },
    { StringId::StatusCompletedFormat, { "完了 ({0}個)", "Completed ({0} parts)" } },
    { StringId::StatusCanceled, { "中断", "Canceled" } },
    { StringId::StatusError, { "エラー", "Error" } },
    { StringId::StatusErrorWithDetail, { "エラー: {0}", "Error: {0}" } },
    { StringId::StartingConversion, { "変換を開始中...", "Starting conversion..." } },
    { StringId::CancelingConversion, { "中断中...", "Canceling..." } },
    { StringId::NoFilesToAddPrompt, { "変換するファイルを追加してください。", "Please add files to convert." } },
    { StringId::InvalidTimeRangePrompt, { "開始時間と終了時間には0以上の数値を入力してください。", "Please enter positive numbers for start and end times." } },
    { StringId::InvalidTimeRangeStartGteEnd, { "時間範囲の指定が不正です。開始時間は終了時間より小さく設定してください。", "Invalid time range. Start time must be less than end time." } },
    { StringId::SpecifyOutputDirPrompt, { "指定フォルダのパスを入力してください。", "Please enter the path for the custom output folder." } },
    { StringId::CollisionTitle, { "出力ファイル名の衝突", "Output Filename Collision" } },
    { StringId::FailedToStartWorker, { "変換ワーカースレッドを開始できませんでした。", "Failed to start converter worker thread." } },
    { StringId::OutputDirNotFound, { "出力先フォルダが見つかりません。", "Output folder not found." } },
    { StringId::RecoverSelectTitle, { "復旧する .partial ファイルを選択", "Select .partial file to recover" } },
    { StringId::RecoverProgressTitle, { "ファイル復旧中", "Recovering File" } },
    { StringId::RecoverProgressMessage, { "不完全な一時ファイルから有効データを復旧しています...", "Recovering valid data from incomplete file..." } },
    { StringId::RecoverCanceled, { "ファイルの復旧処理がキャンセルされました。", "File recovery canceled." } },
    { StringId::RecoverFailedFormat, { "不完全ファイルの復旧に失敗しました:\n{0}", "Failed to recover incomplete file:\n{0}" } },
    { StringId::RecoverSuccessFormat, { "不完全ファイルを正常に復旧し、変換一覧に追加しました:\n{0}\n\n※元の .partial ファイルは安全に保持されています。", "Successfully recovered incomplete file and added to list:\n{0}\n\n* Original .partial file is safely preserved." } },

    // --- コア処理・エラーメッセージ (DlogReader / Worker) ---
    { StringId::ErrFileNotFound, { "対象ファイルが見つかりません: {0}", "Target file not found: {0}" } },
    { StringId::ErrFileOpenFailed, { "ファイルを開けません (アクセス権またはパスを確認してください): {0}", "Failed to open file (check permissions or path): {0}" } },
    { StringId::ErrInvalidMagic, { "マジックナンバーが 'DLOG' ではありません", "Magic number is not 'DLOG'" } },
    { StringId::ErrUnsupportedVersion, { "未対応のDLOGバージョンです (version={0})", "Unsupported DLOG version (version={0})" } },
    { StringId::ErrInvalidHeaderSize, { "DLOGヘッダサイズが64バイトではありません", "DLOG header size is not 64 bytes" } },
    { StringId::ErrInvalidChannels, { "チャネル数が1〜8の範囲外です (channels={0})", "Channel count out of range 1..8 (channels={0})" } },
    { StringId::ErrUnsupportedSampleFormat, { "Float32以外のサンプル形式には対応していません", "Sample formats other than Float32 are not supported" } },
    { StringId::ErrInvalidSampleRateOrDecimation, { "サンプリングレートまたはデシメーション率が不正です", "Sample rate or decimation factor is invalid" } },
    { StringId::ErrInvalidFullScaleVoltage, { "フルスケール電圧が不正です (正の有限値である必要があります)", "Full-scale voltage is invalid (must be positive finite value)" } },
    { StringId::ErrFileSmallerThanHeader, { "ファイルサイズがDLOGヘッダ (64バイト) 未満です", "File size is smaller than DLOG header (64 bytes)" } },
    { StringId::ErrHeaderReadFailed, { "ヘッダの読み込みに失敗しました", "Failed to read header" } },
    { StringId::ErrZeroFramesInFile, { "有効なPCMフレームが含まれていません (データ長0フレーム)", "No valid PCM frames contained in file (0 frames)" } },
    { StringId::ErrPartialFrameDiscarded, { "末尾の不完全なフレーム ({0} バイト) を検出しました", "Incomplete trailing frame ({0} bytes) detected" } },
    { StringId::ErrTargetFileExists, { "復旧先ファイルが既に存在します: {0}", "Target recovery file already exists: {0}" } },
    { StringId::ErrTempFileCreationFailed, { "復旧用一時ファイルを作成できません (書き込み権限またはパスを確認してください): {0}", "Failed to create temporary recovery file (check permissions or path): {0}" } },
    { StringId::ErrHeaderWriteFailed, { "ヘッダの書き込みに失敗しました", "Failed to write header" } },
    { StringId::ErrDataWriteFailed, { "データの書き込み中にI/Oエラーが発生しました", "I/O error occurred while writing data" } },
    { StringId::ErrDiskWriteError, { "ディスク書き込みエラーが発生しました（ディスク容量不足またはドライブ切断）。\n記録を安全停止しました。", "Disk write error occurred (disk full or drive disconnected).\nRecording safely stopped." } },
    { StringId::ErrFlushFailed, { "データのフラッシュに失敗しました", "Failed to flush data to disk" } },
    { StringId::ErrRenameFailed, { "確定ファイルへのリネームに失敗しました: {0}", "Failed to rename to final file: {0}" } },
    { StringId::ErrVerificationFailed, { "修復ファイルの自己検証に失敗しました: {0}", "Self-verification of recovered file failed: {0}" } },
    { StringId::ErrChunkSizeTooLarge, { "読込要求チャンクサイズが大きすぎます", "Requested chunk size too large" } },
    { StringId::ErrReadPastEnd, { "読込要求サイズがストリーム長を超えています", "Requested read exceeds stream size" } },
    { StringId::ErrPcmReadIncomplete, { "PCMデータを最後まで読み込めませんでした", "Failed to read complete PCM data" } },
    { StringId::ErrSeekFailed, { "指定した開始位置へシークできません", "Failed to seek to specified start position" } },
    { StringId::ErrCsvCreateFailed, { "CSV出力ファイルを作成できません: {0}", "Failed to create CSV output file: {0}" } },
    { StringId::ErrConversionException, { "変換中の例外: {0}", "Exception during conversion: {0}" } },
    { StringId::ErrUserCanceled, { "ユーザーにより中断されました", "Canceled by user" } },
    { StringId::ErrPrematureEof, { "DLOGデータが想定フレーム数より前に終了しました", "DLOG data ended earlier than expected frame count" } },
    { StringId::ErrUnknown, { "不明なエラーが発生しました", "An unknown error occurred" } },

    // --- 間引き・区切り・数値精度 選択肢 ---
    { StringId::Decimation1to1, { "1/1 (全サンプル)", "1/1 (Full rate)" } },
    { StringId::Decimation1to2, { "1/2 (2-to-1)", "1/2 (2-to-1)" } },
    { StringId::Decimation1to4, { "1/4 (4-to-1)", "1/4 (4-to-1)" } },
    { StringId::Decimation1to5, { "1/5 (5-to-1)", "1/5 (5-to-1)" } },
    { StringId::Decimation1to8, { "1/8 (8-to-1)", "1/8 (8-to-1)" } },
    { StringId::Decimation1to10, { "1/10 (10-to-1)", "1/10 (10-to-1)" } },
    { StringId::Decimation1to16, { "1/16 (16-to-1)", "1/16 (16-to-1)" } },
    { StringId::Decimation1to20, { "1/20 (20-to-1)", "1/20 (20-to-1)" } },
    { StringId::Decimation1to32, { "1/32 (32-to-1)", "1/32 (32-to-1)" } },
    { StringId::Decimation1to50, { "1/50 (50-to-1)", "1/50 (50-to-1)" } },
    { StringId::Decimation1to64, { "1/64 (64-to-1)", "1/64 (64-to-1)" } },
    { StringId::Decimation1to100, { "1/100 (100-to-1)", "1/100 (100-to-1)" } },
    { StringId::DelimComma, { "カンマ (CSV)", "Comma (CSV)" } },
    { StringId::DelimTab, { "タブ (TSV)", "Tab (TSV)" } },
    { StringId::Prec4Digits, { "4 桁", "4 digits" } },
    { StringId::Prec6Digits, { "6 桁 (推奨)", "6 digits (Recommended)" } },
    { StringId::Prec8Digits, { "8 桁", "8 digits" } },
    { StringId::FileCollisionWithInput, { "出力ファイルパスが入力ファイルと一致しています: {0}", "Output file matches input file path: {0}" } },
    { StringId::FileCollisionDuplicateOutput, { "同名の出力先が重複しています: {0}", "Duplicate output file detected: {0}" } },

    // --- フロー・エラー・ステータス追加 (I18N-001, UI-001) ---
    { StringId::PreviewDropoutsNormal, { "プレビュー欠落: 0 件 (正常)", "Preview Dropouts: 0 (Normal)" } },
    { StringId::PreviewDropoutsNormalWithPrev, { "プレビュー欠落: 0 件 (正常) [前回記録欠落: {0} 件]", "Preview Dropouts: 0 (Normal) [Prev Dropouts: {0}]" } },
    { StringId::PreviewDropoutsWarning, { "プレビュー欠落: {0} 件 [警告]", "Preview Dropouts: {0} [Warning]" } },
    { StringId::PreviewDropoutsWarningWithPrev, { "プレビュー欠落: {0} 件 [警告] [前回記録欠落: {1} 件]", "Preview Dropouts: {0} [Warning] [Prev Dropouts: {1}]" } },
    { StringId::WarnSaveLanguageFailed, { "言語設定の保存に失敗しました: {0}", "Failed to save language settings: {0}" } },
    { StringId::ErrNoInputDevice, { "利用可能な入力デバイスがありません。", "No available input audio device found." } },
    { StringId::ErrNoCompatibleRate, { "選択デバイスとチャネル数で使用可能なサンプリング周波数がありません。", "No supported sample rate found for the selected device and channel count." } },
    { StringId::ErrAudioStreamOpen, { "オーディオストリームのオープンに失敗しました。デバイス設定を確認してください。", "Failed to open audio stream. Please verify audio device configuration." } },
    { StringId::ErrAudioStreamStart, { "オーディオストリームの開始に失敗しました。", "Failed to start audio stream." } },
    { StringId::PromptSpecifySavePath, { "保存ファイルパスを指定してください。", "Please specify a destination file path." } },
    { StringId::ErrSaveFileCreate, { "保存ファイルの作成に失敗しました。パスとアクセス権限を確認してください。", "Failed to create destination file. Please check file path and permissions." } },
    { StringId::ErrCannotSwitchModeWhileRecording, { "記録実行中はモードを切り替えることはできません。\n先に記録を停止してください。", "Cannot switch modes while recording is in progress.\nPlease stop recording first." } },
    { StringId::StatusPausedEndOfFile, { "状態: 一時停止中 (ファイル終端)", "Status: Paused (End of file)" } },
    { StringId::ErrFileOpenTitle, { "ファイルオープンエラー", "File Open Error" } },
    { StringId::ErrPlaybackSeekTitle, { "再生位置エラー", "Playback Seek Error" } },
    { StringId::StatusPlaying, { "状態: 再生中", "Status: Playing" } },
    { StringId::StatusPlaybackFinished, { "状態: 再生終了", "Status: Playback Finished" } },
    { StringId::StatusPaused, { "状態: 一時停止中", "Status: Paused" } },
    { StringId::StatusStoppedError, { "状態: エラー停止", "Status: Stopped (Error)" } },
    { StringId::StatusRecordingComplete, { "状態: 記録完了", "Status: Recording Complete" } },
    { StringId::StatusAbortedPartialKept, { "ファイル: エラー中断 (.partial保持)", "File: Aborted on error (.partial kept)" } },
    { StringId::FileSizeNone, { "ファイルサイズ: 保存なし", "File Size: None" } },
    { StringId::ConfirmOverwritePrompt, { "ファイル '{0}' は既に存在します。\n上書きしてよろしいですか？", "File '{0}' already exists.\nDo you want to overwrite it?" } },
    { StringId::ErrCalibratePrompt, { "CH1に信号が検出されませんでした (Vpp={0})。\nプレビューを開始し、CH1に既知の交流信号を入力した状態で校正を実行してください。", "No signal detected on CH1 (Vpp={0}).\nPlease start preview with an AC reference signal before calibrating." } },
    { StringId::ErrRecordingGeneral, { "記録エラー", "Recording Error" } },
    { StringId::ConfigErrorTitle, { "設定エラー", "Configuration Error" } },
    { StringId::SelectOutputFolder, { "出力先フォルダを選択", "Select Output Folder" } },
    { StringId::NoAudioFilesFoundInFolder, { "指定フォルダ内に .bin または .dat ファイルが見つかりませんでした。", "No .bin or .dat files found in the specified folder." } },
    { StringId::ConversionCompletedAll, { "全変換完了", "All conversions completed" } },
    { StringId::ConversionFinishedWithErrors, { "変換終了（エラーあり）", "Conversion finished with errors" } },
    { StringId::ConversionCompletedTitle, { "変換完了", "Conversion Completed" } },
    { StringId::ConversionErrorsTitle, { "変換終了 (エラー)", "Conversion Finished (Errors)" } },
    { StringId::ConversionSummarySuccess, { "全 {0} 件の変換が完了しました。", "All {0} conversions completed successfully." } },
    { StringId::ConversionSummaryWithErrors, { "変換完了: 成功 {0} 件 / 失敗 {1} 件", "Conversions completed: {0} succeeded / {1} failed" } },
    { StringId::LabelMaxLinesPerFile, { "1ファイルの最大行数:", "Max lines per file:" } },
    { StringId::LabelOutputChannels, { "出力チャネル:", "Output Channels:" } },
    { StringId::BoxProgressAndActions, { " 進捗・実行 ", " Progress & Actions " } },
    { StringId::TipExcelLimit, { "Excelの最大行数 (1,048,576行) に合わせて1,000,000行に設定", "Set to 1,000,000 lines (compatible with Excel 1,048,576 limit)" } },
    { StringId::ErrFileNotOpen, { "ファイルが開かれていません。", "File is not open." } },
    { StringId::ErrReadFrameFailed, { "波形データの読み出しに失敗しました。", "Failed to read waveform data." } },
    { StringId::DialogAddFilesTitle, { "変換する DLOG ファイルを選択（複数選択可）", "Select DLOG Files to Convert (Multiple selection allowed)" } },
    { StringId::DialogAddFolderTitle, { "DLOGファイルが含まれるフォルダを選択", "Select Folder Containing DLOG Files" } },
    { StringId::FilterDlogFiles, { "DLOGファイル (*.bin;*.dat)|*.bin;*.dat|すべてのファイル (*.*)|*.*", "DLOG Files (*.bin;*.dat)|*.bin;*.dat|All Files (*.*)|*.*" } },
    { StringId::FilterPartialFiles, { "DLOG一時ファイル (*.partial;*.partial.*)|*.partial;*.partial.*|すべてのファイル (*.*)|*.*", "DLOG Partial Files (*.partial;*.partial.*)|*.partial;*.partial.*|All Files (*.*)|*.*" } },
    { StringId::ConversionCanceledSummaryFormat, { "変換処理を中断しました。\n\n変換完了: {0} 件\n未完了: {1} 件", "Conversion was canceled.\n\nCompleted: {0}\nRemaining: {1}" } },
    { StringId::ConversionErrorSummaryFormat, { "変換処理が終了しましたが、エラーが発生したファイルがあります。\n\n成功: {0} 件\nエラー: {1} 件", "Conversion finished with errors on some files.\n\nSucceeded: {0}\nFailed: {1}" } },
    { StringId::ConversionSuccessSummaryFormat, { "変換が正常に完了しました。\n\n成功: {0} 件 / 全 {1} 件", "Conversion completed successfully.\n\nSucceeded: {0} / {1}" } }
};

} // namespace

std::string GetString(StringId id, Language lang) {
    auto it = g_translations.find(id);
    if (it == g_translations.end()) {
        return "";
    }
    return (lang == Language::Japanese) ? it->second.ja : it->second.en;
}

std::string Tr(StringId id) {
    return GetString(id, LanguagePreferences::GetCurrentLanguage());
}

std::string FormatString(StringId id, const std::vector<std::string>& args, Language lang) {
    std::string text = GetString(id, lang);
    for (size_t i = 0; i < args.size(); ++i) {
        std::string token = "{" + std::to_string(i) + "}";
        size_t pos = 0;
        while ((pos = text.find(token, pos)) != std::string::npos) {
            text.replace(pos, token.length(), args[i]);
            pos += args[i].length();
        }
    }
    return text;
}

std::string TrFormat(StringId id, const std::vector<std::string>& args) {
    return FormatString(id, args, LanguagePreferences::GetCurrentLanguage());
}

bool VerifyLocalizationIntegrity(std::string* outError) {
    if (outError) outError->clear();

    const size_t totalExpected = static_cast<size_t>(StringId::COUNT) - 1; // 1 〜 COUNT-1
    if (g_translations.size() != totalExpected) {
        if (outError) {
            *outError = "Translation count mismatch: defined " + std::to_string(g_translations.size()) +
                        " but enum has " + std::to_string(totalExpected);
        }
        return false;
    }

    std::regex placeholderRegex(R"(\{([0-9]+)\})");

    for (int i = 1; i < static_cast<int>(StringId::COUNT); ++i) {
        StringId id = static_cast<StringId>(i);
        auto it = g_translations.find(id);
        if (it == g_translations.end()) {
            if (outError) *outError = "Missing translation entry for StringId #" + std::to_string(i);
            return false;
        }

        std::string ja = it->second.ja ? it->second.ja : "";
        std::string en = it->second.en ? it->second.en : "";

        if (ja.empty()) {
            if (outError) *outError = "Empty Japanese translation for StringId #" + std::to_string(i);
            return false;
        }
        if (en.empty()) {
            if (outError) *outError = "Empty English translation for StringId #" + std::to_string(i);
            return false;
        }

        // プレースホルダトークン {0}, {1} の抽出と整合性照合
        std::set<std::string> jaTokens;
        std::set<std::string> enTokens;

        auto jaBegin = std::sregex_iterator(ja.begin(), ja.end(), placeholderRegex);
        auto jaEnd = std::sregex_iterator();
        for (auto m = jaBegin; m != jaEnd; ++m) {
            jaTokens.insert(m->str());
        }

        auto enBegin = std::sregex_iterator(en.begin(), en.end(), placeholderRegex);
        auto enEnd = std::sregex_iterator();
        for (auto m = enBegin; m != enEnd; ++m) {
            enTokens.insert(m->str());
        }

        if (jaTokens != enTokens) {
            if (outError) {
                *outError = "Placeholder mismatch for StringId #" + std::to_string(i) +
                            ": JA has " + std::to_string(jaTokens.size()) +
                            ", EN has " + std::to_string(enTokens.size());
            }
            return false;
        }
    }

    return true;
}

} // namespace dlog
