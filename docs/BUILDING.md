# ビルダー配布とクリーンビルド

Builder ZIPはプロジェクトのソース、テスト、固定依存定義、ビルド／配布スクリプトを含みます。
第三者ソース、ビルド済みライブラリ、コンパイラ、実測データ、`.git` は含みません。
初回はネット接続が必要です。GitHubの公式リポジトリから固定commitとsubmoduleを取得します。

## 前提

- Windows 10／11 x64、PowerShell 5.1以上
- Git for Windows、CMake 3.26以上、Ninja 1.11以上をPATHに設定
- GCC 13.2.0 x86_64 UCRT、MinGW-w64 11.0.1（gcc／g++／objdumpが同じツールチェーン）
- 数GBの空き容量。同期フォルダ外への展開を推奨

ツールのインストールやグローバル設定変更は行いません。別版のツールチェーンを使う場合は
`deps/runtime-notices.lock.json` と検証条件、配布通知を対応させてください。

## 利用

```powershell
.\build.ps1 -Test
.\scripts\package.ps1
```

依存取得・ビルド・ハッシュ検査、アプリビルド、CTestを実行します。
通常テストは実機録音しません。録音を伴う追加試験は `-HardwareTests -Test` を明示します。
全テスト済みの来歴と一致するEXEだけをパッケージにできます。
`-Offline` はソース・submodule・通知が揃ったキャッシュに限り利用可能です。
`build.ps1 -Clean` は旧アプリビルドを退避します。`scripts/build-deps.ps1 -Clean` は新しい依存ビルド世代を作成します。
既存データ・旧依存インストールを再帰削除しません。保持されたバックアップの整理は利用者が行ってください。

## 二つの配布物

```powershell
.\scripts\package-builder.ps1 -Version 2.0.0
.\scripts\package.ps1 -Version 2.0.0
```

- `DataLogger-v2.0.0-builder.zip`：開発・ビルド用。SPECと開発規約を含む
- `DataLogger-v2.0.0-win64.zip`：EXE、利用者文書、ライセンス通知、来歴のみ。SPECと開発規約は含まない

同名ZIPは上書きせず停止します。再生成時は別の `-OutputDir` または版番号を指定してください。

## 配布Builder ZIPのクリーン検証

```powershell
.\scripts\test-clean-build.ps1 -BuilderZip .\dist\DataLogger-v2.0.0-builder.zip
```

毎回新規ディレクトリへ展開し、依存キャッシュなしで取得・ビルド・テスト・バイナリ配布生成まで実行します。
結果JSON・ログ・生成物はそのディレクトリに残します。既存ホストのOS／ツールチェーンを使用するため、
新規Windows VMでのテストや別PCでの動作保証とは異なります。
