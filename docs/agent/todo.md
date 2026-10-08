# Solstice タスク候補(Todo)

ユーザーが登録した、未着手の作業候補。着手したら該当する機能文書に
計画を移し、ここから消す。

## Photoshopのブラシ(ABR)インポート処理の改善

- 登録: 2026年10月8日
- 参考: https://github.com/storytold/photocraft
- 内容: 参考リポジトリのABR読み込みを調べ、Solsticeの読み込み
  (`libs/brush/kis_abr_brush_collection.cpp`、`KisAbrStorage.cpp` など)で読めない形式や
  失われる設定を洗い出して改善する。参考リポジトリのライセンスを確認し、
  コードを取り込む場合はGPL-2.0-or-laterと両立するものに限る。
- 状態: 未調査

## Solsticeの配布物にKra サムネイル用シェル拡張を同梱する

- 登録: 2026年10月8日
- 背景: Windowsのエクスプローラー(およびXYplorerなどシェルのサムネイルを
  使うファイラー)は、`.kra` のサムネイルをKritaのシェル拡張
  (`kritashellex`、`.kra` 内の `preview.png` を読む)で作る。Kritaの
  インストーラー(`packaging/windows/installer/installer_krita.nsi` の
  `SEC_shellex`)が登録するが、Solsticeは開発ビルドから起動しており
  インストーラーがないため未登録。Kritaをアンインストールした環境では、
  Solsticeで保存した新しい `.kra` にサムネイルが出ない(古いファイルは
  Windowsのサムネイルキャッシュで表示されているだけ)。保存内容は正常
  (2026年10月8日に確認)。
- 内容: Windowsの配布物(`docs/agent/github-actions.md` の試験ビルドなど)に
  シェル拡張を含め、インストール時またはスクリプトで登録・解除できるように
  する。Kritaと同居する環境での `.kra` の関連付けやシェル拡張の二重登録の
  扱いを決める。シェル拡張の入手元(Kritaの依存ビルド
  `_installer/krita-nsis`、`build-tools/ci-scripts/build-windows-package.py`
  の `KRITA_SHELLEX`)とライセンスを確認する。
- 状態: 未調査
