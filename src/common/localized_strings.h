#pragma once

#include "common/ui_language.h"
#include "common/language_preferences.h"
#include <string>
#include <vector>

namespace dlog {

enum class StringId {
    None = 0,

    // --- アプリケーション全般・メニュー ---
    AppTitleLogger,
    AppTitleConverter,
    MenuLanguage,
    MenuLanguageJapanese,
    MenuLanguageEnglish,

    // --- ノートブック タブ ---
    TabLiveInput,
    TabPlayback,

    // --- ライブ入力 設定 & コントロール ---
    BoxSettingsAndControls,
    LabelInputDevice,
    ButtonRefresh,
    DeviceNotFound,
    LabelChannels,
    ButtonChannelConfig,
    TipChannelConfig,
    LabelInputRate,
    LabelDecimation,
    CheckAntiAliasing,
    TipAntiAliasing,
    TargetRateFormat,
    LabelFullScaleVoltage,
    ButtonCalibrate,
    ButtonAcGuide,
    TipAcGuide,
    LabelSavePath,
    ButtonBrowse,
    ButtonPreviewStart,
    ButtonPreviewStop,
    ButtonRecordStart,
    ButtonRecordStop,

    // --- 再生コントロール ---
    BoxPlaybackControl,
    LabelPlaybackFile,
    ButtonOpenFile,
    ButtonPlayFirst,
    ButtonPlayStart,
    ButtonPlayPause,
    ButtonPlayLast,
    LabelPlaySpeed,
    LabelPlayPosition,
    TipPlayPosition,
    ButtonSeek,
    ButtonExportWav,
    TipExportWav,

    // --- 波形描画オプション & バッジ ---
    LabelWaveformSettings,
    LabelVerticalGain,
    ButtonHoldWaveform,
    ButtonResumeWaveform,
    TipHoldWaveform,
    LabelChangeDuringRecordHint,
    BadgeLoading,
    BadgeHold,
    CenterTimePlayback,
    CenterTimePreview,
    CenterTimeRecord,
    CenterTimeDefault,
    ScaleExplanationFormat,
    MeterDbfs,
    GainStandard,

    // --- 記録ステータス ---
    BoxRecordingStatus,
    StatusStopped,
    StatusPreviewing,
    StatusRecording,
    StatusErrorFormat,
    DurationLiveFormat,
    DurationPrevFormat,
    FileSizeFormatMb,
    FileSizeFormatGb,
    OverrunsNormal,
    OverrunsWarningFormat,
    UiDropsFormat,

    // --- ダイアログ: チャネル設定 ---
    ChannelConfigTitle,
    ChannelConfigDesc,
    ChannelColChannel,
    ChannelColLabel,
    ChannelColUnit,

    // --- ダイアログ: 電圧校正 ---
    CalibratePrompt,
    CalibrateTitle,
    CalibrateInvalidInput,
    CalibrateNoSignalFormat,
    CalibrateErrorTitle,
    CalibrateSuccessFormat,
    CalibrateSuccessTitle,
    CalibrateDialogTitle,
    CalibrateHeaderDesc,
    CalibrateChannelLabel,
    CalibrateTargetVppLabel,
    CalibrateMonitorGroup,
    CalibrateMeasuredVpp,
    CalibratePeakValue,
    CalibrateCalculatedVfs,
    CalibrateStatusGood,
    CalibrateStatusLow,
    CalibrateStatusClipping,
    CalibrateStatusNoSignal,
    CalibrateStartPromptConfirm,
    CalibrateBtnApply,
    ButtonCancel,
    ErrCannotCalibrateWhileRecording,
    CalibrateSuccessWithChFormat,

    // --- ダイアログ: AC結合ガイダンス ---
    AcGuideTitle,
    AcGuideContent,

    // --- ダイアログ: 共通確認・警告 ---
    ConfirmCloseRecording,
    ConfirmCloseConverting,
    ConfirmDialogTitle,
    WarningDialogTitle,
    ErrorDialogTitle,
    InfoDialogTitle,

    // --- コンバータ UI ---
    BoxTargetFileList,
    ColStatus,
    ColFileName,
    ColSampling,
    ColChannels,
    ColDuration,
    ColSize,
    ColPath,
    ButtonAddFiles,
    ButtonAddFolder,
    ButtonRemoveSelected,
    ButtonClearList,
    ButtonRecoverPartial,
    BoxConversionOptions,
    LabelOutputDir,
    RadioOutSameDir,
    RadioOutCustomDir,
    LabelTimeColumnFormat,
    RadioTimeRelSec,
    RadioTimeAbs,
    RadioTimeIndex,
    RadioTimeNone,
    LabelDecimationStep,
    LabelDelimiter,
    LabelPrecision,
    CheckApplyVoltageScale,
    TipApplyVoltageScale,
    CheckSplitLines,
    ButtonExcelLimit,
    CheckAllChannels,
    CheckTimeRange,
    LabelTimeRangeFrom,
    LabelTimeRangeTo,
    LabelCurrentFileProgress,
    LabelOverallProgress,
    LabelSpeedAndEtaFormat,
    ButtonStartConversion,
    ButtonCancelConversion,
    ButtonOpenOutputDir,

    // --- コンバータ 項目ステータス・進捗・確認 ---
    StatusPending,
    StatusConverting,
    StatusConvertingProgressFormat,
    StatusCompletedFormat,
    StatusCanceled,
    StatusError,
    StatusErrorWithDetail,
    StartingConversion,
    CancelingConversion,
    NoFilesToAddPrompt,
    InvalidTimeRangePrompt,
    InvalidTimeRangeStartGteEnd,
    SpecifyOutputDirPrompt,
    CollisionTitle,
    FailedToStartWorker,
    OutputDirNotFound,
    RecoverSelectTitle,
    RecoverProgressTitle,
    RecoverProgressMessage,
    RecoverCanceled,
    RecoverFailedFormat,
    RecoverSuccessFormat,

    // --- コア処理・エラーメッセージ (DlogReader / Worker) ---
    ErrFileNotFound,
    ErrFileOpenFailed,
    ErrInvalidMagic,
    ErrUnsupportedVersion,
    ErrInvalidHeaderSize,
    ErrInvalidChannels,
    ErrUnsupportedSampleFormat,
    ErrInvalidSampleRateOrDecimation,
    ErrInvalidFullScaleVoltage,
    ErrFileSmallerThanHeader,
    ErrHeaderReadFailed,
    ErrZeroFramesInFile,
    ErrPartialFrameDiscarded,
    ErrTargetFileExists,
    ErrTempFileCreationFailed,
    ErrHeaderWriteFailed,
    ErrDataWriteFailed,
    ErrDiskWriteError,
    ErrFlushFailed,
    ErrRenameFailed,
    ErrVerificationFailed,
    ErrChunkSizeTooLarge,
    ErrReadPastEnd,
    ErrPcmReadIncomplete,
    ErrSeekFailed,
    ErrCsvCreateFailed,
    ErrConversionException,
    ErrUserCanceled,
    ErrPrematureEof,
    ErrUnknown,

    // --- 間引き・区切り・数値精度 選択肢 ---
    Decimation1to1,
    Decimation1to2,
    Decimation1to4,
    Decimation1to5,
    Decimation1to8,
    Decimation1to10,
    Decimation1to16,
    Decimation1to20,
    Decimation1to32,
    Decimation1to50,
    Decimation1to64,
    Decimation1to100,
    DelimComma,
    DelimTab,
    Prec4Digits,
    Prec6Digits,
    Prec8Digits,
    FileCollisionWithInput,
    FileCollisionDuplicateOutput,

    // --- フロー・エラー・ステータス追加 (I18N-001, UI-001) ---
    PreviewDropoutsNormal,
    PreviewDropoutsNormalWithPrev,
    PreviewDropoutsWarning,
    PreviewDropoutsWarningWithPrev,
    WarnSaveLanguageFailed,
    ErrNoInputDevice,
    ErrNoCompatibleRate,
    ErrAudioStreamOpen,
    ErrAudioStreamStart,
    PromptSpecifySavePath,
    ErrSaveFileCreate,
    ErrCannotSwitchModeWhileRecording,
    StatusPausedEndOfFile,
    ErrFileOpenTitle,
    ErrPlaybackSeekTitle,
    StatusPlaying,
    StatusPlaybackFinished,
    StatusPaused,
    StatusStoppedError,
    StatusRecordingComplete,
    StatusAbortedPartialKept,
    FileSizeNone,
    ConfirmOverwritePrompt,
    ErrCalibratePrompt,
    ErrRecordingGeneral,
    ConfigErrorTitle,
    SelectOutputFolder,
    NoAudioFilesFoundInFolder,
    ConversionCompletedAll,
    ConversionFinishedWithErrors,
    ConversionCompletedTitle,
    ConversionErrorsTitle,
    ConversionSummarySuccess,
    ConversionSummaryWithErrors,
    LabelMaxLinesPerFile,
    LabelOutputChannels,
    BoxProgressAndActions,
    TipExcelLimit,
    ErrFileNotOpen,
    ErrReadFrameFailed,
    DialogAddFilesTitle,
    DialogAddFolderTitle,
    FilterDlogFiles,
    FilterPartialFiles,
    ConversionCanceledSummaryFormat,
    ConversionErrorSummaryFormat,
    ConversionSuccessSummaryFormat,

    // --- WAV エクスポート (WavExportDialog / Worker) ---
    WavExportDialogTitle,
    WavExportGroupSource,
    WavExportSourceFormat,
    WavExportNoticeInterpolation,
    WavExportGroupRange,
    WavExportRangeAll,
    WavExportRangeCustom,
    WavExportTimeFrom,
    WavExportTimeTo,
    WavExportGroupChannels,
    WavExportAllChannels,
    WavExportGroupMethod,
    WavExportMethodZOH,
    WavExportMethodLinear,
    WavExportMethodFIR,
    WavExportMethodNoInterpolation,
    WavExportNoticeFIR,
    WavExportGroupFormat,
    WavExportFormatFloat32,
    WavExportFormatPcm16,
    WavExportNoticePcm16,
    WavExportCheckLimit4GiB,
    WavExportTipLimit4GiB,
    WavExportGroupOutput,
    WavExportLabelOutputFile,
    WavExportGroupEstimate,
    WavExportEstimateFormat,
    WavExportButtonExport,
    WavExportProgressTitle,
    WavExportProgressMsgFormat,
    WavExportSuccessTitle,
    WavExportSuccessFormat,
    WavExportSuccessWithClippingFormat,
    WavExportErrorTitle,
    WavExportConfirmCancel,
    WavExportCanceled,
    ErrWavGeneral,
    ErrWavOutputSizeExceeds4GiB,
    FilterWavFiles,
    ErrWavInvalidHeader,
    ErrWavNoChannelsSelected,
    ErrWavInvalidChannelIndexFormat,
    ErrWavInvalidFrameRange,
    ErrWavDestEmpty,
    ErrWavExtMustBeWav,
    ErrWavSameAsSource,
    ErrWavOverwriteMeta,
    ErrChannelsExceedMaxFormat,
    ErrDiskWriteRecoveryFormat,
    ErrMetaWriteFailed,
    ErrDeviceDisconnectedRecord,
    ErrDeviceDisconnectedPreview,
    ErrDiskWriteDuringRecord,

    // 項目数番兵
    COUNT
};

/**
 * @brief 言語指定で文字列を取得
 */
std::string GetString(StringId id, Language lang);

/**
 * @brief 現在の言語で文字列を取得
 */
std::string Tr(StringId id);

/**
 * @brief 言語指定で可変引数置換済み文字列を取得 (プレースホルダ {0}, {1}, ...)
 */
std::string FormatString(StringId id, const std::vector<std::string>& args, Language lang);

/**
 * @brief 現在の言語で可変引数置換済み文字列を取得 (プレースホルダ {0}, {1}, ...)
 */
std::string TrFormat(StringId id, const std::vector<std::string>& args);

inline std::string TrFormat(StringId id, const std::string& arg0) {
    return TrFormat(id, std::vector<std::string>{arg0});
}
inline std::string TrFormat(StringId id, const std::string& arg0, const std::string& arg1) {
    return TrFormat(id, std::vector<std::string>{arg0, arg1});
}
inline std::string TrFormat(StringId id, const std::string& arg0, const std::string& arg1, const std::string& arg2) {
    return TrFormat(id, std::vector<std::string>{arg0, arg1, arg2});
}
inline std::string TrFormat(StringId id, const std::string& arg0, const std::string& arg1, const std::string& arg2, const std::string& arg3) {
    return TrFormat(id, std::vector<std::string>{arg0, arg1, arg2, arg3});
}

inline Language GetUILanguage() {
    return LanguagePreferences::GetCurrentLanguage();
}

inline void SetUILanguage(Language lang) {
    LanguagePreferences::SetCurrentLanguage(lang);
}

/**
 * @brief 翻訳テーブルの自動整合性検証（全IDの定義有無・引数プレースホルダの一致）
 * @param outError エラー理由の格納先
 * @return 整合性が保たれていれば true
 */
bool VerifyLocalizationIntegrity(std::string* outError = nullptr);

/**
 * @brief 構造化されたローカライズ対応エラー情報
 */
struct LocalizedError {
    StringId code = StringId::None;
    std::vector<std::string> args;
    std::string rawDetail;

    bool HasError() const { return code != StringId::None || !rawDetail.empty(); }
    bool hasError() const { return HasError(); }
    void Clear() {
        code = StringId::None;
        args.clear();
        rawDetail.clear();
    }

    std::string Format(Language lang) const {
        if (code == StringId::None) {
            return rawDetail;
        }
        std::string base = FormatString(code, args, lang);
        if (!rawDetail.empty() && base.find(rawDetail) == std::string::npos) {
            base += " (" + rawDetail + ")";
        }
        return base;
    }

    std::string FormatCurrent() const {
        return Format(LanguagePreferences::GetCurrentLanguage());
    }
};

inline LocalizedError MakeError(StringId code) {
    LocalizedError err;
    err.code = code;
    return err;
}

inline LocalizedError MakeError(StringId code, const std::string& arg0) {
    LocalizedError err;
    err.code = code;
    err.args.push_back(arg0);
    return err;
}

inline LocalizedError MakeError(StringId code, const std::string& arg0, const std::string& arg1) {
    LocalizedError err;
    err.code = code;
    err.args.push_back(arg0);
    err.args.push_back(arg1);
    return err;
}

} // namespace dlog

// wxWidgets が利用可能な環境でのラッパーヘルパー
#if defined(__WXMSW__) || defined(wxUSE_GUI) || defined(WXSTRING_H) || defined(_WX_DEFS_H_)
#include <wx/string.h>
namespace dlog {
inline wxString TrWx(StringId id) {
    return wxString::FromUTF8(Tr(id).c_str());
}
inline wxString TrFormatWx(StringId id, const std::string& arg0) {
    return wxString::FromUTF8(TrFormat(id, arg0).c_str());
}
inline wxString TrFormatWx(StringId id, const char* arg0) {
    return wxString::FromUTF8(TrFormat(id, std::string(arg0 ? arg0 : "")).c_str());
}
inline wxString TrFormatWx(StringId id, const wxString& arg0) {
    return wxString::FromUTF8(TrFormat(id, std::string(arg0.utf8_str())).c_str());
}
inline wxString TrFormatWx(StringId id, const std::string& arg0, const std::string& arg1) {
    return wxString::FromUTF8(TrFormat(id, arg0, arg1).c_str());
}
inline wxString TrFormatWx(StringId id, const char* arg0, const char* arg1) {
    return wxString::FromUTF8(TrFormat(id, std::string(arg0 ? arg0 : ""), std::string(arg1 ? arg1 : "")).c_str());
}
inline wxString TrFormatWx(StringId id, const wxString& arg0, const wxString& arg1) {
    return wxString::FromUTF8(TrFormat(id, std::string(arg0.utf8_str()), std::string(arg1.utf8_str())).c_str());
}
inline wxString TrFormatWx(StringId id, const std::string& arg0, const std::string& arg1, const std::string& arg2) {
    return wxString::FromUTF8(TrFormat(id, arg0, arg1, arg2).c_str());
}
inline wxString TrFormatWx(StringId id, const wxString& arg0, const wxString& arg1, const wxString& arg2) {
    return wxString::FromUTF8(TrFormat(id, std::string(arg0.utf8_str()), std::string(arg1.utf8_str()), std::string(arg2.utf8_str())).c_str());
}
inline wxString TrFormatWx(StringId id, const std::string& arg0, const wxString& arg1, const wxString& arg2, const wxString& arg3) {
    return wxString::FromUTF8(TrFormat(id, arg0, std::string(arg1.utf8_str()), std::string(arg2.utf8_str()), std::string(arg3.utf8_str())).c_str());
}
inline wxString TrFormatWx(StringId id, const wxString& arg0, const wxString& arg1, const wxString& arg2, const wxString& arg3) {
    return wxString::FromUTF8(TrFormat(id, std::string(arg0.utf8_str()), std::string(arg1.utf8_str()), std::string(arg2.utf8_str()), std::string(arg3.utf8_str())).c_str());
}
inline wxString TrFormatWx(StringId id, const std::vector<std::string>& args) {
    return wxString::FromUTF8(TrFormat(id, args).c_str());
}
} // namespace dlog
#endif
