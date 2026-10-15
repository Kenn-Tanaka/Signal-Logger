# Data-Logger Suite

Windows用の最大8チャネル信号ロガーです。PC用オーディオインターフェースを入力フロントエンドとし、波形リアルタイム表示、DLOGバイナリ記録、記録データの波形再生、CSV／TSV変換を提供します。

---

## ドキュメント

- [クイックスタート](docs/QUICKSTART.md)：初回の入力確認、記録、再生、変換
- [リファレンスマニュアル](docs/REFERENCE_MANUAL.md)：設定、操作、校正、障害対応
- [実装仕様書](SPEC.md)：構成、処理方式、DLOG形式、制約
- [ライセンス一覧・通知](LICENSES.md)：第三者ソフトウェア許諾・例外条項通知
- [開発規約](AGENTS.md)：実装時の制約・スレッド設計
- [ビルダー配布・クリーンビルド](docs/BUILDING.md)：前提環境、配布物の違い、検証手順

---

## 構成と機能

| 対象 | 用途 |
|---|---|
| `DataLogger.exe` | 保存しないプレビュー、最大8ch記録、時間軸付き波形表示、記録データ再生 |
| `DlogToCsv.exe` | 複数DLOGのCSV／TSV変換、範囲指定、行分割、部分ファイルのコピー復旧 |
| `tools/read_dlog.py` | ヘッダ確認、16-bit PCM WAV書出し（PythonとNumPyが別途必要） |

- **入力レート候補**: 32 / 44.1 / 48 / 88.2 / 96 / 192 kHz（選択デバイス・チャネル数に対するPortAudioの形式照会で動的絞り込み）
- **記録レート**: 入力ネイティブレートの整数分の1（デシメーション処理 + アンチエイリアシング）
- **再生機能**: 音声再生ではなく、DLOGバイナリから直接シークして波形を可視化（再生倍率: 0.25×〜16×）
- **データ保護**: 記録中は隣接の一時ファイル（`.partial`）へ書き込み、正常停止時にアトミック置換

---

## 動作環境

- **OS**: Windows 10 / 11 (64-bit)
- **オーディオAPI**: MME または WASAPI（PortAudio経由）
- **ランタイム**: 完全静的リンク（`-static`, `-static-libgcc`, `-static-libstdc++`）。外部DLL不要（Windows標準システムDLLおよびUCRTのみ使用）

---

## ビルド・テスト・パッケージ化手順

### 必須ツールチェーン
- C++17対応コンパイラ: GCC 13.2.0 / MinGW-w64 11.0.1 (UCRT x86_64)
- CMake 3.26以上
- Ninja 1.11以上
- PowerShell 5.1以上 / Git for Windows

### 1. 依存ライブラリの自動ビルド（公式リポジトリから取得）
依存ライブラリの定義は `deps/dependencies.lock.json` に固定コミットSHAとして集約されています。

```powershell
.\scripts\build-deps.ps1
```
※初回実行時に公式リポジトリから固定revisionを取得し、静的ライブラリおよびヘッダを `.deps/install/` へローカル配置します。以降はキャッシュにより再実行が高速化されます（`-Offline` 指定可能）。

### 2. アプリケーションのビルドと自動テスト
```powershell
.\build.ps1 -Test
```
（または `build.bat -Test`）

- 実行ファイルは `build/` ディレクトリに生成されます（`build/DataLogger.exe`, `build/DlogToCsv.exe`）。
- CTestにより3つのテストスイート（コアロジック、CSVコンバータ、GUI起動スモークテスト）が実行されます。
- 実機録音試験は通常実行しません。必要な場合のみ `-HardwareTests -Test` を指定します。

### 3. リリースパッケージの生成
```powershell
.\scripts\package.ps1
.\scripts\package-builder.ps1
```

- テスト実行・合格の自動検証
- 静的リンク検証（PE DLL依存検査により非OS DLLの混入を遮断）
- バイナリ配布はEXE・利用者向け文書・通知・来歴のみ（SPEC・開発規約は含めない）
- 依存revisionおよびSHA-256ハッシュを記録した `MANIFEST.txt` の自動生成
- `dist/DataLogger-v<Version>-win64.zip` アーカイブの出力
- 別配布の `dist/DataLogger-v<Version>-builder.zip` はソース・スクリプト・開発文書を含み、ライブラリとコンパイラは含まない
- 同名ZIPは上書きせず停止。別出力先または版番号を指定する

Builder ZIPの検証は `scripts/test-clean-build.ps1 -BuilderZip <ZIPのパス>` で行います。
毎回新規展開し、既存の依存ソース・ライブラリ・ビルドキャッシュを使いません。
ホストのOS／コンパイラ等は使用するため、新規Windows VMの検証とは区別します。

---

## 実測データおよび公開に関する方針

- **実測データの完全非公開**: 実機による測定データ（`.bin`, `.dat`, `.meta`, `.partial`、実測CSV等）はすべて `private/` ディレクトリに隔離され、Git管理および公開アーカイブから完全に除外されています。
- **公開用テストデータ**: `tests/fixtures/` にある合成テスト信号（純粋な正弦波数学モデルから自動生成）のみが公開対象です。
- **機密保護**: 作業ログ、コミットメッセージ、報告書への実測値・チャネル名・記録日時・個人情報の記載を禁止しています。

---

## ライセンス

本プロジェクト固有のソースコードおよびドキュメントは、**MIT License** に基づき公開されます（ルートディレクトリの [LICENSE](LICENSE) 参照）。
著作権表示：`Copyright (c) 2026 Kenichiro Tanaka`

サードパーティライブラリ（wxWidgets, PortAudio, readerwriterqueue, concurrentqueue, GCCランタイム）のライセンス、著作権表示および例外条項については [LICENSES.md](LICENSES.md) および各ライセンスファイルを参照してください。
