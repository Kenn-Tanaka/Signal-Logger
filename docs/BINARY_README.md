[English](README.en.md)

# Data-Logger Suite — Windowsバイナリ配布

Windows 10／11（64-bit）用の最大8チャネル信号ロガーです。

- `DataLogger.exe`：保存しないプレビュー、DLOG記録、保存波形の倍率再生
- `DlogToCsv.exe`：CSV／TSV変換、範囲指定、分割、停止済み部分ファイルの復旧

導入・操作は[クイックスタート](docs/QUICKSTART.md)と[リファレンスマニュアル](docs/REFERENCE_MANUAL.md)を参照してください。
利用条件は[LICENSE](LICENSE)と[第三者通知](LICENSES.md)、原文は `licenses/` にあります。

インストールは不要です。ZIPを展開し、EXE・文書・通知を同じ構成で保管してください。
入力機器のドライバとWindows標準DLL／UCRTは必要です。MME／WASAPIを使用します。
記録先は書き込み可能なローカルディスクを選び、測定データは別途バックアップしてください。
本ソフトウェアは過電圧保護を行いません。機器の入力定格を守ってください。

SPEC・開発規約・ソース・ビルドツール・実測データはこのバイナリ配布に含みません。
再ビルドする場合は別配布のBuilder ZIPを使用してください。Python補助ツールもBuilder ZIP側にあります。
長時間記録・ストレージ停止時の無欠落を無条件には保証しません。
