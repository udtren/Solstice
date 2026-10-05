# ブラシオプション共有モデル化 計画書

作成日: 2026年10月5日

状態: 計画のみ。実装・設定変更・ビルド・インストールは未実施。

ブラシエディタ(F5)が持つオプションの状態を画面から切り離し、プリセットごとの
1つのモデルに集約する。F5画面とTool Optionsドッカーなど複数の画面が同じ状態を
参照し、1つのパラメータの変更がそのオプション分の書き込みだけで済むようにする。

本書の最終目的は、F5画面の設定項目をTool Optionsドッカーへ外だしすること、
ブラシの切り替えに追従させること、外だしする項目を設定で管理できるようにする
ことである。共有モデル化はその前提となる基盤である。以下のクラス名・設定キー・
フェーズ分割は提案であり、確定済みの仕様ではない。

## 背景と現状

### 現在のデータの流れ

| 方向 | 処理 | 場所 |
| --- | --- | --- |
| 画面→プリセット | どれかのオプションが `sigSettingChanged` を出すと、`KisConfigWidget` の圧縮タイマーを経て `sigConfigurationUpdated` になる。この時点で、どのオプションが変わったかは失われる | `libs/ui/kis_paintop_settings_widget.cpp:109`、`libs/image/kis_config_widget.cpp:15` |
| 画面→プリセット | `KisPaintopBox::slotGuiChangedCurrentPreset()` が `resetSettings()` で `paintop` 以外のキーを消し、全オプションを書き直す | `libs/ui/kis_paintop_box.cc:1419` |
| プリセット→画面 | `sigSettingsChangedUncompressed` を圧縮した後、F5画面が表示中なら全オプションを `readOptionSetting()` で読み直す | `libs/ui/kis_paintop_box.cc:709`、`:750` |
| 循環防止 | `KisAcyclicSignalConnector` と `KisPaintOpConfigWidget` の `m_isInsideUpdateCall` | `kis_paintop_box.cc:707`、`libs/image/brushengine/kis_paintop_config_widget.cpp` |

`resetSettings()` の直前のコメントは、全消去の理由に
`KisPaintOpUtils::RequiredBrushFilesListTag` の積み増しを挙げている。
しかし、このシンボルは現在のコードに存在しない。実質的に残っている理由は、
条件付きで書かれるキーが古い値のまま残ることだと考えられる。これは
フェーズ0で確認する。

### 移行を可能にする既存の構造

- **データ構造:** オプションの状態は大半が `KisXxxOptionData` という値型として
  定義され、`read()`/`write()` で `KisPropertiesConfiguration` と相互変換できる。
- **lagerのカーソル:** オプションのウィジェットは、コンストラクタで
  `lager::cursor<Data>` を受け取る(例: `KisSizeOptionWidget`、
  `KisCompositeOpOptionWidget`、`KisFilterOptionWidget`)。
- **状態の持ち主:** 現在は `KisPaintOpOptionWidgetUtils::createOptionWidget()` が
  作るラッパー(`detail::DataStorage::m_data`)が、`lager::state` を
  **ウィジェットごとに**持っている
  (`plugins/paintops/libpaintop/KisPaintOpOptionWidgetUtils.h`)。
  この状態を外部に移せば、同じカーソルを複数のウィジェットに渡せる。
- **部分書き込みの前例:** `KisCurveOptionDataUniformProperty::writeValueImpl()` は、
  1つのオプションのデータだけを読み、値を変えて書き戻している。
- **設定画面の構成:** `KisPaintOpOptionWidgetUtils` を使う設定画面は16個ある
  (`plugins/paintops/*/…settings_widget.cpp`)。

### lagerに移行していない、または特殊なオプション

| オプション | 特徴 | 場所 |
| --- | --- | --- |
| ブラシ先端 `KisBrushOptionWidget` | `lager::state` を3つ(`brushData`、`brushPrecisionData`、`commonBrushSizeData`)内部に持ち、外部からカーソルを受け取らない。書き込み時は、内部モデルの `bakedOptionData()` で値を合成する。読み込みには `resourcesInterface` が必要 | `plugins/paintops/libpaintop/kis_brush_option_widget.cpp:40`、`:90`、`:106` |
| マスクブラシ `KisMaskingBrushOption` | 入れ子のブラシ設定を持ち、メインブラシの実効サイズ(`effectiveBrushSize`)に依存する | `plugins/paintops/libpaintop/KisMaskingBrushOption.cpp`、`kis_brushop_settings_widget.cpp:74` |
| カーブ系 `KisCurveOptionWidget` | 書き込みは、内部モデルの `bakedOptionData()` を使う | `plugins/paintops/libpaintop/KisCurveOptionWidget.cpp:234` |
| オプション間の依存 | `lightnessModeEnabled()` や `maskingBrushEnabledReader()` のように、他のオプションの状態をlagerのreaderとして参照するものがある | `kis_brushop_settings_widget.cpp:59`、`:75` |

### F5画面以外でブラシ設定を書き換える経路

共有モデルは、これらの経路による変更も取り込む必要がある。

- Uniform Property(On-Canvas Brush Editorドッカー、`plugins/dockers/brushhud/`)
- ツールバーとキャンバスリソース(サイズ、不透明度、フロー、ブレンドモード)と、
  `kis_derived_resources.cpp`
- ロックされた設定の保存・破棄(`KisPaintopBox::slotSaveLockedOptionToPreset`、
  `slotDropLockedOption`)
- プリセットの再読み込みと切り替え
- Python スクリプト(`libkis`)
- Quick Accessの各パネル(`docs/agent/quick-access.md`)

## 目標アーキテクチャ

```
                     ┌─────────────────────────────┐
  F5画面(ビュー) ──┐ │ KisPaintOpOptionsModel      │
                   ├─▶│  オプションID → lager::state │──書き込み(該当オプションのみ)──▶ KisPaintOpSettings
  Tool Options    ─┘ │  依存関係のreader            │◀─取り込み(変更キーに対応する分)── 外部経路
  (ビュー)           └─────────────────────────────┘
```

### 構成要素(名称は仮)

- **`KisPaintOpOptionsModel`**: 現在のプリセット1つ分のオプション状態を持つ。
  オプションIDごとに `lager::state<Data>` を保持し、ビューには
  `lager::cursor<Data>` を渡す。`KisPaintopBox` が、ブラシエンジンごとに
  1つ所有する(現在の `m_paintopOptionWidgets` と同じ単位)。
- **オプション記述子**: 各ブラシエンジンが、ウィジェットとは別にオプションの
  一覧を宣言する。項目は、オプションID、データ型、表示名、カテゴリ、
  カーソルからウィジェットを作る関数、依存するオプションである。現在は
  `kis_brushop_settings_widget.cpp:44-80` のように、データとウィジェットを
  同時に作っている。これを分離する。
- **ビュー**: F5画面の `KisPaintOpSettingsWidget` と、新しいTool Optionsの
  ビューである。どちらも記述子からウィジェットを作り、モデルのカーソルに
  結び付ける。同じオプションのウィジェットが2つ同時に存在してよい。
- **書き込み**: モデルが各状態を `lager::watch` で監視し、変わったオプションの
  データだけを `Data::write()` でプリセットに書く。そのオプションが前回書いた
  キーのうち、今回書かなかったものは削除する(キー集合の差分による)。
  通知は、既存の `KisPaintOpPreset::UpdatedPostponer` でまとめる。
- **取り込み**: `KisPaintOpSettings` 側で変更されたキー名を記録し、通知と一緒に
  渡す。モデルは、キーと担当オプションの対応表から該当するオプションだけを
  `Data::read()` で読み直し、値が異なる場合だけ状態を更新する。
- **自己反響の防止**: モデル自身の書き込みによる通知は、発生元の印を付けて
  取り込みの対象から外す。

### 保持する規則

- `.kpp` の設定キーと値の形式は変えない(`docs/agent/extension-points.md`)。
  新しい書き込み方式は、従来の全書き込みと**キー単位で同一の結果**を出すこと。
- ロックされた設定(`KisLockedPropertiesProxy`)は、読み込みと書き込みの両方で
  従来と同じ上書き規則を適用する(`kis_paintop_settings_widget.cpp:121`、`:140`)。
- LOD制限と実効ブラシサイズのreaderは、ウィジェットではなくモデルの状態から
  導出する。結果は変えない。
- プリセット切り替え、再読み込み、ロック設定の保存・破棄の後は、従来どおり
  全キーを作り直す経路を残す。部分書き込みは、通常のパラメータ編集にだけ使う。
- 未移行のブラシエンジンは、従来の全書き込み経路で動作し続けること。

## フェーズ

### フェーズ0: 調査と棚卸し

コードは変更しない。成果物は、本書への追記と調査結果の表である。

- **オプションの棚卸し:** 16個の設定画面に含まれる全オプションについて、
  次の項目を表にする。
  - データ型と、カーソルを受け取るかどうか
  - `bakedOptionData()` を使うかどうか
  - 依存するreader
  - 必要なリソース(`resourcesInterface`、`canvasResourcesInterface`、
    `setImage`/`setNode`)
- **キーの担当範囲:** 標準プリセット一式を使い、各オプションの書き込み結果から
  キーと担当オプションの対応を取る。次の点を確認する。
  - 複数のオプションが同じキーを書いていないか(`CompositeOp` など)
  - 条件によって書かれたり書かれなかったりするキーがあるか
  - `resetSettings()` が必要になる実際のケースがあるか
- **lagerの比較:** 値が同じときに `lager::state::set()` が通知を出さないか、
  データ型に `operator==` があるか、`automatic_tag` と `transactional_tag` の
  どちらが適切かを確認する。
- **外部経路:** 上の「F5画面以外でブラシ設定を書き換える経路」を網羅し、
  それぞれの通知経路を記録する。
- **既存テスト:** paintop系の既存テストと、F5画面に関係するテストを確認する。

完了条件: 棚卸し表と対応表が記録され、フェーズ1の設計で未決の点がない。

### フェーズ1: 基盤

- `KisPaintOpOptionsModel` と、オプション記述子の型を追加する。
- `KisPaintOpOptionWidgetUtils` に、外部の `lager::cursor<Data>` を受け取って
  ウィジェットを作る関数を追加する。既存の、状態を内部に持つ関数は残す。
- オプション単位の書き込み、キー差分による削除、変更キーの記録と取り込み、
  自己反響の防止を実装する。
- `KisPaintOpSettings` に変更キーの記録を加える。既存の通知とその順序は変えない。

完了条件:
- テスト用の小さなオプションを使った単体テストが通る。確認する項目は、
  部分書き込み、キー差分による削除、外部変更の取り込み、自己反響が起きないこと、
  `UpdatedPostponer` による通知のまとめである。
- 既存のブラシエンジンの動作が変わらない。

### フェーズ2: Pixel Brushの移行

最も使われ、特殊なオプション(ブラシ先端、マスクブラシ、テクスチャ)を多く含む
Pixel Brush(`plugins/paintops/defaultpaintops/brush/`)から移行する。

- オプション記述子を定義し、F5画面をモデルのビューに置き換える。
- `KisBrushOptionWidget` は、内部の3つの状態をモデルに移す。
  `bakedOptionData()` は、モデル側で合成した値を保存するか、ビューが内部モデルを
  共有するかを、フェーズ0の結果で決める。
- マスクブラシとの依存(実効サイズ、有効状態)は、モデルのreaderとして渡す。
- `KisPaintopBox` は、移行済みのブラシエンジンにだけ新しい経路を使う。

完了条件:
- **パリティテスト:** 標準のPixel Brushプリセットすべてについて、従来の
  全書き込みと新方式の結果が、キーと値の両方で一致する。
- 1つのパラメータを変えたとき、そのオプションのキーだけが書き換わることを
  テストで確認する。
- ロックされた設定、プリセットの再読み込み、汚れ表示(dirty)、Uniform
  Property、ツールバーのサイズ・不透明度との相互作用を手動で確認する。

### フェーズ3: Tool Optionsへの外だし

本書の最終目的である3つの要件を実現する。

- **外だし:** Tool Optionsドッカーに、モデルのビューを追加する。対象は、F5画面の
  オプションのページ単位とする。`KisToolPaint::createOptionWidget()`
  (`libs/ui/tool/kis_tool_paint.cc:385`)のレイアウトに、ブラシ系ツールで
  共通の領域として置くことを想定する。
- **ブラシへの追従:** `CurrentPaintOpPreset` の変更を受けて、ビューを新しいモデルに
  結び直す。ブラシエンジンが変わったら、ウィジェットを作り直す。
- **設定による管理:** ブラシエンジンごとに、外だしするオプションIDの一覧を
  kritarcに保存する(キー名の例: `Solstice/ToolOptionsBrushOptions`)。
  On-Canvas Brush Editorの `brushHudSetting` とは別に管理する。設定画面は、
  `KisDlgConfigureBrushHud` の2列リスト形式を参考にする。
- Quick Accessの浮遊パッドは `sharedtooldocker` の中身を借りて表示する
  (`plugins/dockers/quickaccess/QuickAdjustDock.cpp:439`)。内容の高さが変わる
  ことに対する追従と、借りたウィジェットの返却を確認する。

完了条件:
- 選んだオプションがTool OptionsとF5画面の両方で同時に操作でき、互いに即座に
  反映される。
- ブラシを切り替えると、表示が新しいプリセットの値に更新される。
- 設定の変更がkritarcに保存され、再起動後も保たれる。
- ユーザーによる実アプリでの手動確認が完了している。

### フェーズ4: 残りのブラシエンジンの移行

Color Smudge、Hairy、Spray、Filter、MyPaintなど、残り15個の設定画面を順に移行する。
各エンジンで、フェーズ2と同じパリティテストを行う。移行していないエンジンでは、
Tool Optionsへの外だしを無効にする。

### フェーズ5: 旧経路の整理

全エンジンの移行後に、`slotGuiChangedCurrentPreset()` の全消去による通常編集を
廃止する。プリセット切り替えなどの全書き込み経路は残す。Uniform Propertyを
モデルのカーソルの上に作り直すかどうかは、この時点で判断する。

## リスク

| リスク | 影響 | 対策 |
| --- | --- | --- |
| キーの重複や条件付きの書き込み | 部分書き込みで古いキーが残る、または他のオプションのキーを消す | フェーズ0の対応表。キー差分による削除。パリティテスト |
| `bakedOptionData()` を使うオプション | 2つのビューで内部モデルの状態がずれる | フェーズ0で方式を決め、該当オプションを個別に検証する |
| オプション間の依存 | 依存先の変更が依存元に伝わらない | 依存関係を記述子に明示し、モデルのreaderで接続する |
| 外部経路の取りこぼし | Tool OptionsやF5画面の表示が古くなる | フェーズ0で経路を網羅し、取り込みのテストを用意する |
| 通知の循環や過剰な更新 | 無限ループ、スライダー操作中の負荷 | 発生元の印、値の比較、既存の圧縮タイマーの維持 |
| ロックされた設定 | 保存・破棄の動作が変わる | 全書き込み経路を残し、手動確認の項目に含める |
| 変更範囲の広さ | 16個の設定画面と共通部品に影響する | エンジン単位で移行し、未移行のエンジンは旧経路のまま動かす |
| Brush Stroke Preview | プリセットの読み込み経路に依存する | `docs/agent/brush-stroke-preview.md` の手動確認を併せて行う |

## 対象外

- 個々のスライダーなど、オプションのページより細かい単位での外だし。
  これにはオプションのウィジェットの分割が必要で、本計画の後に検討する。
- `.kpp` の設定キーの名前や形式の変更。
- ブラシエンジンの描画処理の変更。

## 手動確認の項目(フェーズ2以降)

- F5画面で各オプションを変更し、プレビュー、ブラシのアウトライン、
  汚れ表示(dirty)が従来どおり更新される。
- ツールバーのサイズ・不透明度・フロー・ブレンドモードの変更が、F5画面と
  Tool Optionsに反映される。
- On-Canvas Brush Editorでの変更が、F5画面とTool Optionsに反映される。
- プリセットの切り替え、再読み込み、上書き保存、新規保存で、値が正しく保たれる。
- ロックされた設定の保存・破棄が、従来どおりに動作する。
- マスクブラシとテクスチャを有効にしたプリセットで、値が失われない。
- Quick Accessの浮遊パッドが、Tool Optionsの高さの変化に追従する。

## 未決事項

- 外だしの単位をオプションのページにするか、ページ内の一部にするか
  (初回はページ単位を推奨)。
- Tool Options内での配置(ツール固有の設定の上か下か、折りたたみを付けるか)。
- 外だしの設定をブラシエンジンごとにするか、全エンジン共通にするか。
- `lager::state` の伝播方式(`automatic_tag` か `transactional_tag` か)。
