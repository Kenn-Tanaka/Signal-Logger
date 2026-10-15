# ライセンス一覧・第三者通知

本プロジェクト固有のコード・文書はルートの `LICENSE` に定めるMIT Licenseです。
第三者ソフトウェアにはそれぞれのライセンスが適用されます。

## 配布物の区別

- バイナリ配布：実際にビルドした依存ソースから収集した通知原文を `licenses/` に同梱します。
- ビルダー配布：第三者ライブラリ・コンパイラ・測定データは同梱しません。初回ビルドで公式配布元から取得します。
- ソース作業ツリー内の旧 `3rdparty/` は新ビルダーでは使用しません。これを別途配布する場合は各フォルダの通知を保持してください。

## 使用する依存物

| 依存物 | 用途・許諾 | 配布時の通知所在 |
|---|---|---|
| wxWidgets 3.2.5 | GUI。wxWindows Library Licence（LGPL本文＋例外） | `licenses/wxWidgets/` |
| PortAudio v19.8-preview | MME/WASAPI入力。MIT形式 | `licenses/portaudio/` |
| ReaderWriterQueue 1.0.6 | SPSCキュー。Simplified BSD、追加ヘッダ通知 | `licenses/readerwriterqueue/` |
| ConcurrentQueue 1.0.4 | 現在未使用。取得セットに含む。BSD／Boost等 | `licenses/concurrentqueue/` |
| libpng、IJG JPEG、libtiff、zlib、PCRE2、NanoSVG | wxWidgetsの固定submodule。各許諾条件による | `licenses/wxWidgets/` |
| GCC 13.2.0 libgcc／libstdc++ | GPLv3＋GCC Runtime Library Exception 3.1 | `licenses/runtime/gcc-gpl3.txt`、`gcc-runtime.txt` |
| MinGW-w64 11.0.1、winpthreads | 複数の許諾・著作権表示。個別原文参照 | `licenses/runtime/mingw-runtime.txt`、`winpthreads.txt` |

This software is based in part on the work of the Independent JPEG Group.

固定revisionはビルダーの `deps/dependencies.lock.json`、ランタイム通知の公式URL・SHA-256は
`deps/runtime-notices.lock.json` に記録します。wxWidgetsのsubmoduleは親commitが指定するcommitを使用します。
バイナリの `build_provenance.json` と `MANIFEST.txt` は実際の取得revision、ツール情報、ファイルハッシュを記録します。
GCC例外の適用には原文の条件があります。静的リンクだけで無条件の免除になるとは扱いません。
ツールチェーン変更時には通知定義・実際のリンク対象を再確認してください。

Python補助ツールのNumPyは別途利用者が導入するもので、この配布物には含みません。
WindowsシステムDLL・UCRT・デバイスドライバはOS／各提供元の条件に従います。

ライセンス原文は要約・改変せず取得・コピーします。依存通知の欠落・ハッシュ不一致はビルド／配布を失敗にします。
