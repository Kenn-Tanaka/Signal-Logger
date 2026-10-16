# AGENTS.md - Data-Logger Project Guidelines & Architecture Specification

## 1. プロジェクト概要 (Project Overview)
本プロジェクトは、PC用ディジタルオーディオインターフェース（A/Dコンバータ）をハードウェアフロントエンドとし、最大8チャネルのアナログ信号を長期間連続してデジタル記録・リアルタイム描画するデスクトップアプリケーション（C++ / wxWidgets）です。

### 最優先事項 (Top Priority)
**「音声およびデータの取りこぼし・欠落の防止 (Zero Dropouts)」**
高CPU負荷時やUI描画遅延時にもドロップアウトゼロを目標とします（UI描画の間引き・破棄は許容）。ただし、高負荷やSSDの一時停止が既定バッファの吸収時間を超えた場合まで無欠落を保証するものではありません。PortAudio入力オーバーフローおよび記録経路のキュー溢れを検出し、録音ごとにリセットされる累積回数をUIへ表示することを必須とします。

---

## 2. システム構成・技術スタック (Technology Stack)
* **プログラミング言語**: C++ (C++17以上)
* **GUIフレームワーク**: wxWidgets 3.2.x (スタティックリンク)
* **オーディオI/O**: PortAudio v19 (スタティックリンク, WASAPI / MME)
* **キュー**: moodycamel::ConcurrentQueue / ReaderWriterQueue (ロックフリー, ヘッダオンリー)
* **コンパイラ・ビルドシステム**:
  * g++ (MinGW-w64 UCRT 13.2.0)
  * CMake (3.26+) / Ninja
  * リンク方式: 全ライブラリおよびC/C++ランタイムをスタティックリンク (`-static`, `-static-libgcc`, `-static-libstdc++`)

---

## 3. スレッド・アーキテクチャ設計 (Thread Architecture)

```text
 [A/Dコンバータ / オーディオI/F]
        │ (ハードウェア割込み)
        ▼
 ┌──────────────────────────────────────────────┐
 [1. 音声受信用スレッド (PortAudio Callback)]   │ ※ロック禁止・動的メモリ確保禁止
 └──────────────────────┬───────────────────────┘
                        │ (ロックフリー SPSC キュー: RawAudioQueue)
                        ▼
 ┌──────────────────────────────────────────────┐
 [2. デシメーション・処理スレッド]             │ ※ダウンサンプリング + LPF
 └──────────────┬───────────────────────────────┘
                │
         ┌──────┴──────────────────────┐
         │ (ロックフリー SPSC キュー)  │ (ロックフリー UI キュー: ドロップ許容)
         ▼                             ▼
 ┌───────────────────────────┐ ┌───────────────────────────┐
 [3. ファイル保存スレッド]    │ [4. GUI / メインスレッド]     │
 └───────────┬───────────────┘ └───────────┬───────────────┘
             │ (高速バイナリI/O)           │ (wxBufferedPaintDC)
             ▼                             ▼
 ┌───────────────────────────┐ ┌───────────────────────────┐
 [ローカルストレージ (SSD)]  │ [リアルタイム波形描画画面]    │
 └───────────────────────────┘ └───────────────────────────┘
```

### スレッドごとの責務と厳格な制約

1. **音声受信用スレッド (PortAudio Callback - Real-time Priority)**
   * **責務**: ハードウェアから渡されるマルチチャネルPCMストリームを受信。
   * **厳格な制約**:
     * ミューテックスのロック禁止
     * 動的メモリ確保 (`malloc`, `new`) 禁止
     * ファイルI/O、システムコール、ログ出力禁止
   * **処理**: 事前アロケートされた固定サイズバッファを用い、ロックフリー・キューへプッシュ。

2. **デシメーション・処理スレッド (Background Worker)**
   * **責務**: ネイティブレート（例: 44.1 kHz）から目標レート（例: 22.05 kHz）への間引き処理。
   * **アンチエイリアシング**: 移動平均またはFIR/IIRデジタルローパスフィルタにより折り返し雑音を除去。
   * **分配**: デシメーション済みPCMブロックを、ファイル保存キューおよびUI波形描画キューへプッシュ。

3. **ファイル保存スレッド (Disk I/O Worker)**
   * **責務**: デシメーション済みマルチチャネルPCMデータをストレージへ連続書き込み。
   * **電源保護**: `SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED)` によるWindows自動スリープ/休止状態の抑制。
   * **バッファリング**: チャンク単位（数十KB〜数百KB）で大容量バッファリングし、SSD書き込みオーバーヘッドを極小化。

4. **GUI / メインスレッド (wxWidgets UI Thread)**
   * **責務**: ユーザー入力、パラメータ設定、ステータス更新、リアルタイム波形描画。
   * **波形描画**:
     * `wxTimer` (30ms周期) で描画トリガー。
     * Min/Max エンベロープアルゴリズムにより描画点数を画面ピクセル幅に圧縮し高速描画。
     * UIキューが溢れた場合は古いデータを安全にドロップし、最新波形のみを描画。

---

## 4. バイナリファイルフォーマット仕様 (File Format Specification)

拡張子: `.dat` または `.bin`
ヘッダ長: 固定 64 バイト

```text
Offset  Type      Field Name          Description
0x00    char[4]   magic               "DLOG" (Data Logger Magic)
0x04    uint32_t  version             フォーマットバージョン (2)
0x08    uint32_t  header_size         ヘッダ全体のサイズ (64 bytes)
0x0C    uint32_t  channels            チャネル数 (例: 2, 4, 8)
0x10    uint32_t  sample_rate         表示用の丸め済みレート（正確値は native_rate / decimation_factor）
0x14    uint32_t  sample_format       0: Float32（v2ではFloat32のみ）
0x18    uint32_t  native_rate         変換前ネイティブレート (例: 44100)
0x1C    uint32_t  decimation_factor   デシメーション間引き率 (例: 2)
0x20    int64_t   start_timestamp_utc 記録開始時刻 (UNIX Epoch ミリ秒)
0x28    double    full_scale_voltage  デジタル ±1.0 に対応するフルスケール電圧 [V]
0x30    uint8_t[16] reserved          将来用予約領域 (0x00 埋め)
0x40    Binary    pcm_data            インターリーブ形式 PCM データ列
```

データ部:
`[Ch0_Sample0][Ch1_Sample0]...[Ch(N-1)_Sample0][Ch0_Sample1][Ch1_Sample1]...`

---

## 5. モジュール構成とディレクトリ構造 (Directory Structure)

```text
2026-10-09_Data-Logger/
├── LICENSE                    # プロジェクトライセンス (MIT)
├── LICENSES.md                # 第三者ライセンス通知・固定revision定義
├── README.md                  # プロジェクト概要・ビルド手順
├── SPEC.md                    # アーキテクチャ・詳細仕様書
├── AGENTS.md                  # 本設計・開発運用ガイドライン
├── CMakeLists.txt             # プロジェクト全体のビルド設定 (Static Link)
├── CMakePresets.json          # Ninja / Release CMakeプリセット
├── build.ps1 / build.bat      # アプリケーションビルド・テスト実行スクリプト
├── deps/                      # 依存関係定義
│   └── dependencies.lock.json # 公式リポジトリ・固定コミットSHA定義
├── scripts/                   # 自動化スクリプト
│   ├── build-deps.ps1         # 依存ライブラリ取得・ソースビルド
│   └── package.ps1            # 配布パッケージ・マニフェスト生成
├── docs/                      # ユーザー向けドキュメント
│   ├── QUICKSTART.md          # クイックスタートガイド
│   └── REFERENCE_MANUAL.md    # リファレンスマニュアル
├── src/                       # データロガー本体 (DataLogger.exe)
│   ├── main.cpp               # wxWidgets エントリーポイント
│   ├── app.h / app.cpp        # wxApp 実装
│   ├── common/                # 日英ローカライズ・言語設定永続化
│   ├── ui/                    # UI実装 (MainFrame, WaveformPanel, CalibrationDialog, WavExportDialog)
│   ├── audio/                 # PortAudio デバイス列挙・ストリーム管理
│   ├── dsp/                   # アンチエイリアスFIR LPF・デシメーション・校正計算
│   ├── storage/               # ディスク非同期I/O・ファイル出力・電源制御
│   ├── playback/              # DLOG波形再生バックグラウンド読込
│   └── export/                # WAV/RF64 エクスポート・アップサンプリング補間
├── src_converter/             # DLOG to CSV バッチ変換ツール (DlogToCsv.exe)
│   ├── main_converter.cpp     # エントリーポイント
│   ├── converter_app.h / .cpp # wxApp 実装
│   ├── converter_types.h      # 変換オプション・バッチ定義
│   ├── dlog_reader.h / .cpp   # DLOG バイナリ解析・ストリーミング読出
│   ├── csv_writer.h / .cpp    # 高速CSV出力・行数自動分割
│   ├── batch_converter_worker.h / .cpp # バックグラウンド変換スレッド
│   └── ui/converter_frame.h / .cpp     # バッチ変換GUI
├── tools/                     # オフライン解析スクリプト
│   └── read_dlog.py           # Python 簡易確認・WAV変換ツール
├── tests/                     # ユニット・統合テストコード
│   ├── test_dlog.cpp          # ロガーコアテスト
│   ├── test_converter.cpp     # コンバータコアテスト
│   ├── test_wav_export.cpp    # WAVエクスポート・補間・RF64テスト
│   └── fixtures/              # 公開用合成テスト信号 (純粋な数学的正弦波)
├── .deps/                     # [ローカル/非公開] 依存ソース取得・ビルド・インストール先
├── build/                     # [ローカル/非公開] ビルド生成物
├── dist/                      # [ローカル/非公開] 配布zipパッケージ
└── private/                   # [ローカル/非公開] 実測データ隔離フォルダ
```

---

## 6. エージェント開発規約 (Agent Guidelines)

1. **リアルタイム安全性の厳守**:
   * `audio/` 内のコールバック経路ではいかなるブロッキング処理（I/O、ミューテックス、メモリ割り当て）も行わない。
2. **スタティックリンクの維持**:
   * 配布時の単一ポータブル実行ファイルを目指し、依存ライブラリはすべてスタティックリンクすること。
3. **エラーハンドリング**:
   * USBケーブル抜去やデバイス切断が発生した場合でもクラッシュせず、ファイルを正常にフラッシュして終了すること。
   * バッファオーバーフロー時はUI描画側を優先破棄し、ファイル保存側のデータ整合性を死守すること。
   * PortAudio入力オーバーフロー、Rawキュー溢れ、保存キュー溢れをUI上のドロップアウト回数へ反映し、次の録音開始時に0へリセットすること。
   * 録音中は最終出力と同じディレクトリの一時ファイルへ書き、正常停止時のみ最終名へ置換すること。開始失敗時に既存DLOGを破壊しないこと。
4. **実測データの完全非公開と機密保護**:
   * 実機測定データはすべて `private/` へ隔離し、Git追跡および配布パッケージに含めないこと。
   * 実測波形データ、チャネル名、記録日時、個人名等をログや完了報告に転記・開示しないこと。
   * テスト用データには `tests/fixtures/` の合成データを使用すること。
5. **コード品質**:
   * モダンC++（RAII、スマートポインタ、明示的な型安全）を遵守する。
