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
