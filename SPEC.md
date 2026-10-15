[English](SPEC.en.md)

# 実装仕様書

基準日：2026-10-14。対象：現在のC++ソース、CMake、Python補助ツール。配布EXEの同一性や全機能の動的検証を示す文書ではありません。

## 目的・適用範囲

PCオーディオ入力を使い、最大8chを取得・表示・長時間記録する内部利用向けWindowsアプリケーションです。非保存プレビュー、オフライン波形再生、CSV／TSV変換を含みます。

要件は「欠落防止を最優先とし、検出した記録欠落を次回記録まで可視化する」です。無制限のストレージ停止吸収、校正済み計測器としての精度保証、厳密1000Hzへの有理数変換、8ch超、DirectSound／WDM-KS追加対応は範囲外です。

## 構成

| モジュール | 責務 |
|---|---|
| src/app.cpp、main.cpp | wxApp起動、スモークテスト入口 |
| src/ui/main_frame.* | 入力・記録・再生操作、状態、ステータス、メタデータ |
| src/ui/waveform_panel.* | 波形バッファ、Min/Max描画、レベル・時間軸 |
| src/ui/time_axis_calculator.* | 時間範囲・目盛計算 |
| src/audio/audio_device_mgr.* | PortAudio列挙、形式照会、ストリーム、Raw／UIキュー |
| src/dsp/decimator_processor.* | 整数比デシメーション |
| src/storage/data_logger_worker.* | 保存キュー、DLOG書込、確定・回収先 |
| src/storage/power_manager.h | Windows電源抑制 |
| src/playback/* | 時刻解析、ウィンドウ要求、バックグラウンド読込 |
| src_converter/* | DLOG検査・復旧、変換計画、CSV出力、バッチ・GUI |
| tools/read_dlog.py | memmap読込、情報表示、WAV書出し |
| tests/* | コア・変換テスト |

C++17、wxWidgets 3.2.5、PortAudio v19.8-preview、ReaderWriterQueue v1.0.6を使用します。依存ライブラリは `deps/dependencies.lock.json` に固定コミットSHAとして定義され、`scripts/build-deps.ps1` により公式リポジトリから取得・ソースビルドされて `.deps/install/` へ配置されます。CMakeLists.txt は imported target（`PortAudio::PortAudio`, `wxWidgets::wxWidgets`）を用いて `.deps/install/` を優先参照し、全コンポーネントを完全静的リンクします。実測データはすべて `private/` に隔離され非公開です。

## 取得・記録経路

```text
PortAudio callback → Raw SPSC → DSP worker → 保存SPSC → Disk worker → DLOG
                                         └→ UI SPSC → wxTimer → 波形
```

コールバックは固定サイズAudioBlockへコピーし、try_enqueueします。ロック、動的確保、ファイルI/O、ログ出力を追加してはいけません。

定数上のブロック上限は8ch×1024フレーム、通常設定は512フレームです。キュー要求容量はRaw 256、UI 64、保存512ブロックです。実ライブラリの確保容量と吸収秒数は同一概念ではなく、デシメーション等でも変化するため、固定秒数の保証はしません。

プレビューは取得・DSP・UI経路を使い、保存ワーカーを渡しません。GUIのタイマー周期は30msです。UIキューが満杯なら描画用ブロックを捨てます。現実装は最新表示へ常に即時追従する保証を持ちません。

入力オーバーフロー、Rawキュー失敗、保存キュー失敗を欠落イベントとして集計します。UIキュー失敗は別集計です。保存データには欠落区間の個別タイムスタンプや欠損マーカーを挿入しません。このため、欠落があった記録の再生時刻を連続実時間と同一視してはいけません。

## レート・DSP

候補入力レート：32000、44100、48000、88200、96000、192000Hz。選択チャネル数・デバイスのFloat32入力をPa_IsFormatSupportedで照会します。候補外の全周波数を列挙する機能ではありません。

記録レートを `Fs = nativeRate / N` とします。GUIはN=1、2、4、8、および約1000／100／10／1Hzに対応する `max(1, round(nativeRate / requestedRate))` を提供します。

- N=1：そのまま出力。
- 1<N<=8：アンチエイリアス有効時は17タップ、Hamming窓のsinc FIR（正規化カットオフ0.45/N、DCゲイン正規化）。無効時は単純間引き（ポイントサンプリング、フィルタ遅延・リンギングなし）。
- N>8：アンチエイリアス有効時はブロック間で加算状態を保持するボックスカー平均。無効時は単純間引き（Nサンプル周期のポイントサンプリング）。
- 通過帯域リップルや阻止域減衰を数値保証する仕様はありません。

表示電圧は正規化PCM×共通フルスケール電圧です。チャネル単位文字列の変更は数値変換ではありません。CALはCH1の既知Vpp／デジタルVppから共通係数を求めます。

## 状態と描画

ライブ側はStopped、Previewing、Recordingを扱います。プレビューから記録への切替時はストリームを停止して再開するため、連続取得を保証しません。記録停止後にプレビュー再開を試みる経路があります。

表示幅は1ms～10sの離散候補で、10divの中心相対目盛、開始基準の中心時刻、μs/div・ms/div・s/divを表示します。低密度ではサンプル、高密度ではMin/Maxを描画します。ホールド・垂直ゲインは保存データを変更しません。

要件として、記録結果の欠落数はプレビュー欠落とは分離し、次の記録開始まで保持します。異常終了時を含む表示の完全性はリリースごとの回帰試験対象です。

## DLOG v2

拡張子は.bin／.dat。Windows x64実装はリトルエンディアン、IEEE 754 float／doubleを使用します。ヘッダは64バイトです。

| Offset | 型 | フィールド | 内容 |
|---|---|---|---|
| 0x00 | char[4] | magic | DLOG |
| 0x04 | uint32 | version | 2 |
| 0x08 | uint32 | header_size | 64 |
| 0x0C | uint32 | channels | 1～8 |
| 0x10 | uint32 | sample_rate | 記録レートを丸めた表示値 |
| 0x14 | uint32 | sample_format | 0=Float32 |
| 0x18 | uint32 | native_rate | 入力レート |
| 0x1C | uint32 | decimation_factor | 整数N |
| 0x20 | int64 | start_timestamp_utc | Unix Epochミリ秒 |
| 0x28 | double | full_scale_voltage | 正規化±1.0に対応する電圧 |
| 0x30 | uint8[16] | reserved | 予約・0埋め |
| 0x40～ | float32[] | pcm_data | フレーム単位のチャネル交互配置 |

フレームiの開始オフセットは `64 + i × channels × 4`。フレームiの相対秒は `i × N / native_rate`。sample_rateの丸め値を正確な時間計算に使用しません。

データ部に圧縮、チェックサム、インデックス、個別時刻はありません。通常Readerはヘッダ、形式、フレーム境界、空データ等を検査します。v1の読込許容コードは残っていますが、後方互換は必須要件ではありません。

### 付随メタデータ

通常名は `recording.bin.meta`。UTF-8のテキストで、次のように保存します。

```ini
[DLOG_METADATA]
magic=DLOG
version=2
channels=2
start_timestamp_utc=1791510000000
full_scale_voltage=1
sample_rate=22050
native_rate=44100
decimation_factor=2
anti_aliasing=true

[CH1]
name=Signal1
unit=V

[CH2]
name=Signal2
unit=V
```

Readerは記載されたmagic、version、channels、native_rate、decimation_factor、start_timestamp_utcの不一致等を調べ、検出時はCH番号・Vへ戻します。旧形式名の.metaを探すフォールバックもあります。全必須キーの存在を厳密に保証するスキーマ検査ではなく、警告のGUI伝達も完全とは限りません。

DLOG本体と.metaは別々に確定されます。2ファイル一括のトランザクションではありません。

### 一時保存・復旧

記録中は最終名に.partialを付加し、既存一時ファイルがあれば枝番を使用します。正常な書込・停止後に最終名へ置換します。開始失敗時に既存最終ファイルを破壊しない方針です。書込／確定失敗時は回収先を通知します。

復旧は通常Readerと共通のヘッダ検査後、完全フレームだけを別一時ファイルへコピーして改名します。原本を切り詰めません。コンバータGUIに進捗・キャンセルの入口があります。復旧成功だけで欠落のない記録や内容の完全性を保証しません。

## 再生

DlogReaderによる64-bit位置指定、65,536フレーム単位のチャンク読込、ウィンドウ内のraw／Min-Max結果を使います。入力デバイスは不要です。

時刻はsteady_clockによる経過時間と倍率から算出します。倍率は0.25、0.5、1、2、4、8、16。最後のサンプル位置で終了します。要求世代による古い結果の破棄がありますが、遅いI/O時の進捗保証とは別問題です。

## 変換

ConversionPlanで時間範囲・間引き・拡張子・分割数を計画し、出力同士および入力との衝突を検査します。ファイルは順次変換します。CSV側の間引きはLPFなしの選択です。

相対秒は小数6桁、UTCはミリ秒、サンプル番号は元フレーム番号です。数値精度は4／6／8桁。選択チャネル、共通電圧換算、名称・単位を列へ反映します。文字列フィールドの区切り・引用符をエスケープします。

出力は一時ファイルから確定しますが、バッチ全体・全分割ファイル一括の原子的確定ではありません。既存出力の上書きは既定です。

## 既知の制約と未保証事項

- 日本語や空白を含むパス環境でもビルドが成立するよう、`build.ps1` および `scripts/build-deps.ps1` では一時仮想ドライブ（`subst X:` 等）のマッピングと安全な解放機構を備えています。
- 配布パッケージ生成（`scripts/package.ps1`）では、全自動テストの合格検証、PE DLL依存関係のOS/UCRT限定検査、マニフェスト（`MANIFEST.txt`）生成が行われます。
- 再生要求にはepochと要求番号を使います。同一epoch内の更新では読込を中断しません。ファイル切替時のReaderとヘッダの同一性、終了のI/O待ちは改善・試験対象です。
- 大きいライブ表示窓は全表示サンプルの走査コストを持ちます。
- メタデータの欠落キー、警告伝達、復旧後のメタデータコピー失敗等を完全に扱う保証はありません。
- 長時間高負荷、デバイス抜去、ディスク満杯、SSD長時間停止、電源断を網羅した保証はありません。
- 計測設定の永続化、CH別の独立換算、記録データ編集、音声再生は対象外です。
- ソース上の対策追加と、その配布EXEでの動的確認は区別します。過去版のテスト結果を現版の合格証明に使いません。

## 検証と変更管理

CTestにはTestDataLogger、TestConverter、および両アプリ×両言語のGUI起動スモークテスト（TestDataLoggerSmokeJa、TestDataLoggerSmokeEn、TestDlogToCsvSmokeJa、TestDlogToCsvSmokeEn）を登録しています。実機録音は既定で無効です。`DLOG_ENABLE_HARDWARE_TESTS=ON`（build.ps1では-HardwareTests）でTestAudioHardwareを追加します。機器なしでスキップされる試験の有無も結果に記録してください。スモークテストはCallAfterでイベントループ開始後に操作してCloseしますが、すべての描画・タイマーを網羅しません。

言語切替はUI表示のみを更新し、DLOGバイナリやCSV出力形式を変更しません。同一DLOGに対するCSV変換結果は100%バイト一致（完全一致）しますが、独立した実機録音セッション間の完全一致は物理クロック差等により保証されません。再生要求はReader・ヘッダを同一セッションオブジェクトへ固定し（PLAY-001）、v2メタデータは必須識別情報の厳格検証（DATA-001）を実施します。校正操作はプレビュー中または停止中に限定され、記録中の換算係数変更は禁止されています（FUNC-001）。

バイナリ配布には本書・開発規約を含めません。ビルダー配布はソースと固定依存定義のみを含み、公式ソースとsubmoduleを取得して再構築します。手順とクリーン検証の範囲はdocs/BUILDING.mdを参照してください。旧3rdpartyへのビルドフォールバックは使用しません。

公開前には、変換計画の組合せ、低速読込と連続シーク、開始失敗、記録停止時の最終欠落集計、破損.partialの非破壊復旧、日本語パスを確認してください。仕様変更時は本書、操作マニュアル、テストを同時に更新します。

