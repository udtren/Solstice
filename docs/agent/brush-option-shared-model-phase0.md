# ブラシオプション共有モデル化 フェーズ0調査結果

調査日: 2026年10月5日

状態: 調査完了(静的解析とプリセットデータの解析のみ)。コードの変更、ビルド、
実行による確認は行っていない。

計画書は `docs/agent/brush-option-shared-model-plan.md`。本書はそのフェーズ0の
成果物であり、計画書の設計判断の根拠となる。行番号は2026年10月5日時点の
`krita-sol-gpu` 系のソースによる。

## 調査方法と限界

- ソースコードの静的解析。オプションのクラス、データ型の `read()`/`write()`、
  通知経路、外部の書き込み経路を対象とした。
- 同梱プリセットの解析。リポジトリ内の `.kpp` と `krita/data/bundles/*.bundle`
  内の `.kpp`、計313件のプリセットXMLからキー集合を抽出した。
  - 内訳は Krita 3 バンドル131件、Krita 4 バンドル117件、
    `benchmarks/data` 25件、`plugins` 18件、`krita/data` 16件、
    RGBA_brushes バンドル6件。ほかに `benchmarks/data` の2件は抽出できなかった。
  - エンジン別では paintbrush 175件、colorsmudge 40件、sketch 14件、
    hairy 12件、spray 10件など。
- この環境にはQtとlagerがないため、ビルドもテストも実行していない。
  lagerはリポジトリに同梱されておらず、`find_package(Lager REQUIRED)` で
  外部から取り込まれる(`CMakeLists.txt:1089`)。lagerの挙動は上流
  `arximboldi/lager` のソースで確認した。開発環境に入っている版と一致するかは
  未確認である。

## 結論の要約

1. **計画の方式は成立する。** オプションの状態の大半はすでに値型の `Data` で、
   ウィジェットは外部の `lager::cursor<Data>` を受け取る。外部からの変更も、
   キー単位の取り込みで扱える。
2. **ただし「保存されている値」と「ウィジェットが持つ値」は一致しない。**
   多くのオプションは、他のオプションの状態に応じて値を加工してから書き込む
   (「焼き込み」)。共有モデルは加工前の値を持ち、加工はモデル側で行う必要が
   ある。
3. **キー差分だけでは古いキーを消しきれない。** 旧形式のキーは、読み込み時には
   参照されるが、書き込まれることはない。現在は `resetSettings()` がこれを
   消している。対策として、**プリセットを読み込んでから最初の書き込みだけは
   全書き込みにする**ことを推奨する(詳細は後述)。
4. **Pixel Brushは移行が最も難しいエンジンの一つである。** 計画書のフェーズ2で
   最初に移行する対象としたが、先に難易度の低いエンジンで基盤を検証するよう
   順序を見直す。
5. **既存のテストはほぼない。** `KisPaintopBox`、更新通知の圧縮と延期、
   `resetSettings`、ロックされた設定、Uniform Propertyの読み書きを確かめる
   テストは存在しない。フェーズ1でテストの整備から始める必要がある。

## 1. 共通の仕組み

| 項目 | 調査結果 | 場所 |
| --- | --- | --- |
| 状態の持ち主 | `createOptionWidget()` のラッパー(`detail::DataStorage`)が `lager::state<Data, automatic_tag>` を持つ。ウィジェットには、そのままか、`to_base` レンズで基底型に変換したカーソルを渡す | `plugins/paintops/libpaintop/KisPaintOpOptionWidgetUtils.h:24-76` |
| `to_base` レンズ | 設定時に `static_cast<Base&>(src) = value` で丸ごと代入する。接頭辞やコールバックも上書きされる | `libs/global/KisLager.h:97-101` |
| LOD制限 | `createOptionWidgetWithLodLimitations` は `Data::lodLimitations` をreaderとして合成する。使うのは hatching、sketch、particle、hairy、deform、MyPaint だけ | `KisPaintOpOptionWidgetUtils.h:90-106` |
| チェック状態 | `bool` を受け取るコンストラクタは、`Data` の外に `checkedFallback` という独自の状態を持つ。実害があるのはマスクブラシだけである。ほかのオプションは大半がチェック不可になっている | `libs/ui/kis_paintop_option.cpp:24`、`:42-80` |
| 書き込み | `writeConfiguration` が、ロック用のプロキシを通して全オプションを追加順に書く | `libs/ui/kis_paintop_settings_widget.cpp:138-144` |
| 読み込み | `setConfiguration` が、同じプロキシを通して全オプションを追加順に読む。マスクブラシはブラシ先端の後に読む必要があり、**読み込みの順序に意味がある** | `kis_paintop_settings_widget.cpp:118-136` |
| 全消去 | `resetSettings()` は `paintop` 以外のキーを消す。MyPaintは `MyPaint/json` も残す。呼び出し元はリポジトリ全体で `slotGuiChangedCurrentPreset` だけ。コメントにある `RequiredBrushFilesListTag` は現在存在しない | `libs/image/brushengine/kis_paintop_settings.cpp:274-291`、`plugins/paintops/mypaint/MyPaintPaintOpSettings.cpp:135-140`、`libs/ui/kis_paintop_box.cc:1432-1438` |
| オプション外のキー | `configuration()` が書く `paintop` と、F5画面自身が書くLOD設定(`lodUserAllowed`、`lodSizeThreshold`)だけ | `libs/ui/widgets/kis_paintop_presets_editor.cpp:494-510` |
| オプションのロック | ロック時は、オプションを空の設定に書き出して担当キーを求める。無効なテクスチャは何も書かないため、ロックしても何もロックされない | `kis_paintop_settings_widget.cpp:222-250` |

## 2. 共通オプション(libpaintop)の棚卸し

「外部カーソル」は、コンストラクタが `lager::cursor<Data>` を受け取るかどうか。
「書き込み」は、カーソルの値をそのまま書く(直接)か、加工して書く(焼き込み)か。

| オプション | 外部カーソル | 独自の状態 | 書き込み | 他オプションへの依存 | 主なキー |
| --- | --- | --- | --- | --- | --- |
| カーブ系の基底 `KisCurveOptionWidget` | あり | 選択中センサー(画面用、保存しない) | **焼き込み**: `isChecked &= enabledLink`、強さの範囲を `strengthRangeReader` から取り、値を範囲内に丸める | `enabledLink`、任意の範囲reader | `Pressure<id>`、`<id>Sensor`(XML)、`<id>UseCurve`、`<id>UseSameCurve`、`<id>Value`、`<id>curveMode`、`<id>commonCurve` |
| Opacity、Flow、Ratio、Softness、Rotation、Darken、Mix、Hue、Saturation、Value、Rate、Strength | あり | なし | 基底と同じ | なし | 同上(idはそれぞれ。Hue/Saturation/Valueは `h`/`s`/`v`) |
| Size | あり | なし | 基底と同じ | なし | 同上。LOD制限あり |
| LightnessStrength | あり | なし | 基底と同じ。`enabledLink` はブラシ先端の明度モード | ブラシ先端 | 同上。明度モードでないブラシで書くと無効として保存され、次に読むとオフになる(現在も同じ挙動) |
| Spacing、Mirror、Scatter、Sharpness | あり | なし | カーブを焼き込み、追加項目は直接 | なし | カーブ系のキーに加えて `Spacing/Isotropic`、`PaintOpSettings/updateSpacingBetweenDabs`、`HorizontalMirrorEnabled`、`VerticalMirrorEnabled`、`Scattering/AxisX/Y`、`Sharpness/alignoutline`、`Sharpness/softness` |
| CompositeOp | あり | なし | 直接 | なし | `CompositeOp`、`EraserMode` |
| ColorSource | あり | なし | 直接 | なし | `ColorSource/Type` |
| Airbrush | あり | なし | 直接 | なし | `PaintOpSettings/isAirbrushing`、`rate`、`ignoreSpacing` |
| PaintingMode | あり | なし | **焼き込み**: マスクブラシが有効ならWASHに固定 | マスクブラシ | `PaintOpAction` |
| Texture | あり | なし(ただしモデルが `maximumOffsetX/Y` を構築時にカーソルへ書き込む) | **焼き込み**: パターンをリソースから解決して埋め込む | なし(`resourcesInterface` が必要) | `Texture/Pattern/...` 一式。**無効のときは何も書かない**。LOD制限あり |
| Filter | あり | フィルタ設定の画面 | **焼き込み**: フィルタ未設定なら既定のフィルタを使う | なし(`setImage`/`setNode` が必要) | `Filter/id`、`Filter/configuration`、`Filter/smudgeMode` |
| Color | あり | なし | 直接 | なし | `ColorOption/...` |
| **ブラシ先端 `KisBrushOptionWidget`** | **なし** | **あり**: `brushData`、`brushPrecisionData`、`commonBrushSizeData` | **焼き込み**: 直径・倍率を共通サイズから決める | なし(他オプションへ `lightnessModeEnabled()`、`effectiveBrushSize()`、`bakedBrushData()` を提供) | `brush_definition`。精度設定(`KisPrecisionOption/...`)は `SupportsPrecision` のときだけ書く。LOD制限あり |
| **マスクブラシ `KisMaskingBrushOption`** | **なし** | **あり**: `maskingData`、サイズ、精度、保持モード、チェック状態(Qtシグナルで同期) | **焼き込み**: マスターサイズ連動時は係数を両方のサイズから再計算 | ブラシ先端の実効サイズ(読み書き両方) | `MaskingBrush/Enabled`、`MaskingCompositeOp`、`UseMasterSize`、`MasterSizeCoeff`、`MaskingBrush/Preset/brush_definition`。LOD制限は旧方式の仮想関数だけで、F5画面には反映されていない |

出典は `plugins/paintops/libpaintop/` の各 `*Widget.cpp`、`*Model.cpp`、`*Data.cpp`。
特に `KisCurveOptionModel.cpp:161-169`、`KisTextureOptionData.cpp:40-61`、
`KisTextureOptionModel.cpp:67-84`、`kis_brush_option_widget.cpp:40-155`、
`KisMaskingBrushOption.cpp:60-286`、`KisMaskingBrushOptionProperties.cpp:22-73`、
`KisKritaSensorPack.cpp:185-272` を参照。

## 3. エンジン別の棚卸し

| エンジン | オプション数(先端を含む) | 独自の状態を持つオプション | 焼き込み・条件付き書き込み | オプション間の依存 | 移行難易度 |
| --- | ---: | --- | --- | --- | --- |
| Pixel Brush (paintbrush) | 32 | ブラシ先端、マスクブラシ | 先端、マスク、PaintingMode、LightnessStrength、Texture | 明度モード→LightnessStrength。先端の実効サイズ→マスク(読み書き両方)。マスク有効→PaintingMode。マスク用カーブは接頭辞 `MaskingBrush/Preset/` 付き | **高** |
| Color Smudge | 22 | ブラシ先端。設定画面自体も `KisBrushPropertiesModel` を持ち、KisBrushを生成する | SmudgeLength(新エンジンの強制)、SmudgeRadius(範囲)、PaintThickness、OverlayMode、Texture | 先端の加工済みデータ→SmudgeLength→SmudgeRadiusの範囲。明度モード→PaintThickness、OverlayMode | **高** |
| MyPaint | 46 | なし。Basicが他の3オプションのカーソルを共有する | すべてのカーブが共有のJSONキー `MyPaint/json` を読んで変更して書く。旧形式のキーも書く | Basic↔Radius/Hardness/Opacity。実効サイズはRadiusから | **高** |
| Spray | 12 | 手動で追加したブラシ先端 | SprayOp(倍率・列挙値の変換) | SprayShapeが `lager::with` のレンズでSprayOpの直径・倍率に書き戻す | 中 |
| Duplicate | 9 | ブラシ先端 | 先端、Texture | なし。旧方式の `lodLimitations()` 上書きあり | 中 |
| Filter | 7 | ブラシ先端、フィルタ設定の画面 | Filter、先端 | Filterが `setImage`/`setNode` を必要とする | 中 |
| Hatching | 14 | ブラシ先端 | 先端、Texture。列挙値を5つのboolに変換 | なし | 中 |
| Tangent Normal | 17 | ブラシ先端 | 先端、Texture、PaintingMode | なし。専用の設定クラスもない | 中 |
| Hairy | 8 | ブラシ先端 | 先端 | なし | 中 |
| Sketch | 12 | ブラシ先端 | 先端 | なし。PaintingModeの既定値だけが異なる | 中 |
| Deform | 8 | なし | なし | なし | 低 |
| Curve | 6 | なし | なし | なし | 低 |
| Experiment | 2 | なし | なし | なし | 低 |
| Grid | 5 | なし | GridOpが書き込み時に1未満の値を1に丸める | なし | 低 |
| Particle | 5 | なし | なし | なし | 低 |
| Round Marker | 4 | なし | なし | なし | 低 |

出典は各エンジンの `*_settings_widget.cpp` と、エンジン固有オプションの
`*Data.cpp`/`*Model.cpp`。詳細は Pixel Brush が
`plugins/paintops/defaultpaintops/brush/kis_brushop_settings_widget.cpp:34-89`、
Color Smudge が `plugins/paintops/colorsmudge/kis_colorsmudgeop_settings_widget.cpp:33-110`、
MyPaint が `plugins/paintops/mypaint/MyPaintPaintOpSettingsWidget.cpp:41-218`
および `MyPaintSensorPack.cpp:339-445`、Spray が
`plugins/paintops/spray/kis_spray_paintop_settings_widget.cpp:28-51`
および `KisSprayShapeOptionModel.cpp:13-90`。

## 4. キーの担当範囲

### 複数の書き手があるキー

| キー | 書き手 |
| --- | --- |
| `CompositeOp`、`EraserMode` | CompositeOpオプション、`setPaintOpCompositeOp`/`setEraserMode`(ツールバーなど)、MyPaintのBasic(`EraserMode`) |
| `OpacityValue`、`FlowValue` | Opacity/Flowのカーブ、`setPaintOpOpacity`/`setPaintOpFlow`、`kis_paintop_box.cc:1237` |
| `ScatterValue`、`PressureScatter` | Scatterのカーブ、`setPaintOpScatter` |
| `brush_definition` | ブラシ先端、`setPaintOpSize/Angle/Spacing/AutoSpacing`(`BrushWriter` 経由)、`setPaintOpFade`、Brush Stroke Previewの複製 |
| `Texture/Pattern/Scale` | Textureオプション、パターンサイズのリソース変換、`kis_paintop_box.cc:1238` |
| `PressureSize`、`SizeUseCurve`、`OpacityUseCurve`、`FlowUseCurve`、`PressureRotation`、`RotationUseCurve` | 各カーブ、Quick Adjustのトグル(`plugins/dockers/quickaccess/QuickAdjustDock.cpp:679-711` が直接書く) |
| `MyPaint/json` | MyPaintの全カーブオプション(読んで変更して書く) |

接頭辞 `Texture/`(Textureの `Texture/Pattern/...` とStrengthの `Texture/Strength/...`)
と `MaskingBrush/Preset/` は複数のオプションが共有する。ただし、キーそのものの
衝突はない。

これらはすべて「オプション外からの変更」であり、取り込み側で担当オプションに
振り分ければよい。**同じキーを複数のオプションが担当する例は、MyPaintの
`MyPaint/json` 以外に見つからなかった。**

### 書き込まれないが読み込まれる旧形式のキー

| キー | 読み込む場所 | 危険性 |
| --- | --- | --- |
| `Custom<id>`、`Curve<id>` | `KisKritaSensorPack.cpp:190-202` | センサーXMLに `curve` 要素がない場合、`Custom<id>=true` なら `Curve<id>` の曲線で全センサーを上書きする。曲線が既定値のときは `curve` 要素を書かない(`KisSensorData.cpp:35-44`)。そのため、**消さずに残すと、曲線を既定値に戻したつもりでも、次に読み込んだときに古い曲線が復活する** |
| `Scattering/Amount` | `KisScatterOptionData.cpp:12,37-38` | 旧形式の散布量 |
| `Sharpness/factor` | `KisSharpnessOptionData.cpp:8,18-19,36-37` | 旧形式のシャープネス |

同梱プリセットでの実際の出現数は次のとおり。

- `Custom<id>=true` を含むもの: **313件中80件**(paintbrush 36件、hairy 11件、
  spray 6件など)
- `Scattering/Amount` または `Sharpness/factor` を含むもの: 60件
- 他エンジンのキー(`Spray/...`、`Experiment/...`、`ShapeDynamics/...`)を含む
  paintbrush/colorsmudgeプリセット: 13件(Krita 3バンドルなど)

### 条件付きで書かれるキー

- Textureは、無効のとき何も書かない(`KisTextureOptionData.cpp:42`)。
- 精度設定は `SupportsPrecision` のときだけ書く。
- MyPaintは、無効なセンサーをJSONから取り除く。
- Color Smudgeは、補正用のコールバックで `SmudgeRadiusVersion` を追加で書く
  (`colorsmudge/KisSmudgeRadiusOptionData.cpp:28`)。

## 5. 通知の仕組み

### 現在の流れ

- **変更の検出:** `KisPaintOpSettings::setProperty` は、値が変わっていれば
  プリセットを変更済み(dirty)にする。値が同じでも、**必ず** `onPropertyChanged()`
  を呼んで通知する(`libs/image/brushengine/kis_paintop_settings.cpp:573-602`)。
- **通知されない操作:** `removeProperty`、`clearProperties`、`setPropertyNotSaved`、
  `fromXML` は仮想関数ではなく、通知も出さない
  (`libs/image/kis_properties_configuration.cc:70, 301-316`)。
- **通知の順序:** `KisPaintOpPresetUpdateProxy::notifySettingsChanged()` は、
  EarlyWarning → Uncompressed の順にシグナルを出した後、100msの圧縮タイマー
  (FIRST_ACTIVE)を起動する。FIRST_ACTIVEは**初回だけ同期的に**
  `sigSettingsChanged` を出す
  (`libs/image/brushengine/KisPaintOpPresetUpdateProxy.cpp:16-77`)。
- **延期:** `UpdatedPostponer` は通知を延期し、解除時にまとめて直接出す
  (`libs/image/brushengine/kis_paintop_preset.cpp:638-651`)。
- **設定の差し替え:** `KisPaintOpPreset::setSettings` は設定オブジェクトを
  複製で**差し替え**、`sigUniformPropertiesChanged` と `sigSettingsChanged` を
  出す(`kis_paintop_preset.cpp:136-158`)。プリセットの読み込み、再読み込み、
  名前変更、libkisの `Preset::fromXML` がこれを呼ぶ。

### 変更されたキーは記録されていない

シグナルにも、`onPropertyChanged()` にも、`UpdateListener::notifySettingsChanged()`
にも引数がない。変更されたキーを記録する仕組みは存在しない。

### 記録の追加先(推奨)

1. **記録:** `KisPaintOpSettings::setProperty` で、値が変わったキーを記録する。
   オプションの書き込み、接頭辞付きの書き込み、ロック用プロキシ経由の書き込み、
   `BrushWriter`、Uniform Propertyは、すべてここを通る。
2. **削除の扱い:** `KisPaintOpSettings` に `removeProperty` の上書きを追加し、
   削除されたキーも記録する。
3. **全消去と差し替えの扱い:** `resetSettings`/`clearProperties` と `setSettings`
   は「全体が変わった」という印として扱う。
4. **集約と配信:** `KisPaintOpPresetUpdateProxy` は延期と圧縮の境界を知っている
   唯一の場所である。ここでキーを集約し、既存のシグナルと同時に配信する。

### 注意すべき既存の挙動

- **保存時の削除:** `KisPaintOpPreset::toXML` は、テクスチャが無効なとき、
  **現在のプリセットの設定から** `Texture*` キーを通知なしで削除する
  (`kis_paintop_preset.cpp:309-318`)。保存やlibkisの `Preset::toXML` で起きる。
- **部分的な状態の通知:** 延期なしで複数のキーを書くと、最初のキーを書いた
  時点で同期的に `sigSettingsChanged` が出る。受け取った側は書き込み途中の状態を
  見ることになる(Uniform Property、`setPaintOpScatter`、`BrushWriter`、
  Quick Adjustのトグルが該当)。

## 6. 外部の書き込み経路

特記がない限り、すべて**現在のプリセット**を直接変更する。

| 経路 | きっかけ | 変更するキー | 備考 |
| --- | --- | --- | --- |
| サイズのリソース変換 → `setPaintOpSize` | ツールバー、Quick Adjust、Quick Accessの項目、libkis | 先端系は `brush_definition`。ほかは各エンジンのオプションキー | ロック用プロキシを通らない |
| 回転 → `setPaintOpAngle` | ツールバー、Quick Adjust、ショートカット、libkis | `brush_definition` など | プロキシを通らない |
| 不透明度 → `setPaintOpOpacity` | ツールバー、`stepAlpha`、消しゴム切り替え、Quick Adjust、libkis | `OpacityValue` | プロキシ経由。ツールが「不透明度をプリセットに保存する」設定のときだけ |
| フロー → `setPaintOpFlow` | ツールバー、`stepFlow`、Quick Adjust、libkis | `FlowValue` | プロキシ経由 |
| ブレンドモード → `setPaintOpCompositeOp` | ツールバー、ノード切り替え、Quick Adjust、Quick Access、libkis | `CompositeOp` | プロキシ経由 |
| 消しゴムモード → `setEraserMode` | 消しゴムボタン、Quick Adjust、libkis | `EraserMode` | プロキシ経由 |
| パターンサイズ | ツールバー、libkis | `Texture/Pattern/Scale` | 直接書く |
| フェード、散布 | キャンバス操作の `stepFade`/`stepScatter` | `brush_definition`、`ScatterValue`、`PressureScatter` | 散布は延期なしで2回通知 |
| `KisPaintopBox::sliderChanged` | ツールバーのスライダー | 上記に加え `FlowValue`、`Texture/Pattern/Scale` | 書いた後、F5画面が非表示でも全オプションを読み直す |
| 消しゴムの切り替え | 消しゴムボタン | `SavedBrushSize`、`SavedEraserSize`、`SavedBrushOpacity`、`SavedEraserOpacity` | 保存されないキーだが、プリセットは変更済みになる |
| ロック設定の保存・破棄 | オプションのロック解除 | オプションのキーと `<key>_previous` | 延期あり |
| 再読み込み、プリセット切り替え | 再読み込みボタン、プリセット選択 | 設定オブジェクトごと差し替え | 変更済みフラグは解除される |
| Uniform Property | On-Canvas Brush Editor | 各エンジンの対象キー | 不透明度とフロー以外はプロキシを通らず、延期もない |
| キャンバス上でのサイズ変更、`[`/`]`、回転ショートカット | ツール操作 | `setPaintOpSize`/`setPaintOpAngle` と同じ | |
| Gridブラシの Ctrl+Alt クリック | Gridブラシ | `horizontal_offset`、`vertical_offset` | `kis_grid_paintop_settings.cpp:67-95` |
| libkis | Python | 上記の各setter、または設定全体の差し替え | `toXML` でもTextureキーを削除する |
| Quick Adjustのトグル | Solstice独自 | `PressureSize` ほかのカーブのキー | 直接書いた後、同じプリセットで `setPaintOpPreset` を呼ぶ。プロキシも延期も通らず、1回のトグルで通知が2回出る |
| Brush Stroke Preview | Solstice独自 | 複製のみ | 現在のプリセットは変更しない |

## 7. ロックされた設定

- **ロックの保存先:** `KisLockedPropertiesServer` が、全体で1つの設定を
  持っている(`libs/image/brushengine/kis_locked_properties_server.cpp`)。
- **読み込みでも書き込みが起きる:** `KisLockedPropertiesProxy::getProperty` は、
  読み込み時にプリセットへ書き込む
  (`libs/image/brushengine/kis_locked_properties_proxy.cpp:28-63`)。
  - ロックされたキーなら、元の値を `<key>_previous`(保存しないキー)に退避し、
    ロック値をプリセットに書き込む。
  - ロックされていないキーで `<key>_previous` が残っていれば、その値を戻して
    `_previous` を消す。
  - いずれも変更済みフラグは保たれるが、通知は出る。
- **更新リスナーがない場合:** `setProperty` は、プリセットに属していない設定
  オブジェクトに対しては何も書かない。
- **現在のF5画面との違い:** 全消去で `_previous` が消える。その後のプロキシ経由の
  書き込みで、`_previous` が**ロック値で**作り直される。そのため、F5画面で一度
  でも編集すると、ロック前の値は失われ、「破棄」してもロック値が残る。
  オプション単位の書き込みでは元の値が残る。これは改善だが、挙動の変更になる。

共有モデルが守るべき規則は次のとおり。

1. 書き込みと取り込みは、どちらもロック用プロキシを通す。
2. 自分が前回書いたキーだけを差分削除の対象にする。保存しないキー、
   `*_previous`、`Saved*`、`MyPaint/json` は対象にしない。
3. 各オプションの書き込みを `UpdatedPostponer` で囲む。
4. ロック設定の保存・破棄、再読み込み、プリセット切り替えは、全体の読み直しと
   して扱う。

## 8. 変更済みフラグと正規化

- **変更済みフラグの変化:** 現在は、F5画面で何か操作すると、値が変わらなくても
  必ずプリセットが変更済みになる。全消去後に `paintop` を書き直すと、空の状態
  からの変更とみなされるためである。オプション単位の書き込みでは、値が実際に
  変わったときだけ変更済みになる。これも挙動の変更(改善)になる。
- **キー集合が保存結果を決める:** MD5は保存ファイルのバイト列から計算される。
  `toXML` はキーを整列して書き、保存しないキーは除く。したがって、保存結果を
  決めるのはキーの**集合**である。
- **全消去による正規化:** 現在は、F5画面での編集が全消去によって旧形式のキーや
  他エンジンのキーを取り除き、キー集合を正規化している。
- **複製の問題:** `KisPaintOpSettings::clone()` は「保存しない」という印を
  複製しない(`kis_paintop_settings.cpp:252-272`)。そのため、複製では
  `*_previous` や `Saved*` が通常のキーとして保存されうる。現在は全消去が
  これらを消しているため表に出ていない。

## 9. lagerの通知の仕様

上流 `arximboldi/lager` のソースで確認した。開発環境に入っている版での確認が
必要である。

- **同値の場合は通知しない:** `state.set(x)`/`cursor.set(x)` は、
  `operator==` で同値なら値を保存せず、通知もしない(`lager/detail/nodes.hpp`
  の `has_changed`、`push_down`、`send_down`、`notify`)。部分カーソルへの
  書き込みも、最終的には `Data` 全体の `operator==` で比較される。
- **`operator==` が見ない値:** 次の値は比較の対象外なので、変えても**捨てられる**。
  - `hasPaintingModeProperty`
  - 接頭辞(`prefix`)
  - 補正用のコールバック
- **`qFuzzyCompare` の許容差:** 許容差以内の変化も捨てられる。対象は、
  エアブラシの速度、テクスチャの各値、ブラシのモデル、マスクの係数などである。
- **`automatic_tag` と `transactional_tag`:**
  - `automatic_tag` は、設定するとすぐに伝播して通知する。現在のコードは
    すべてこちらを使っている。
  - `transactional_tag` は、`lager::commit()` を呼ぶまで伝播しない。`commit()` は
    複数の状態をまとめて伝播してから通知するため、プリセット読み込みのように
    多くのオプションを一度に更新する場合に適している。

## 10. 既存のテスト

| テスト | 内容 | 本計画での扱い |
| --- | --- | --- |
| `libs/image/tests/kis_properties_configuration_test.cpp` | 設定の読み書き、保存しないキー、複製 | そのまま利用 |
| `libs/image/tests/KisPaintOpPresetTest.cpp` | 埋め込みリソースとMD5 | そのまま利用 |
| `libs/ui/tests/kis_derived_resources_test.cpp` | 派生リソースの通知と圧縮。唯一の通知経路のテスト | 変更キーの通知を追加する際の回帰確認に使う |
| `libs/ui/tests/KisBrushStrokePreviewTest.cpp` | Solstice独自。保存後の複製の独立性など | 回帰確認に使う |
| `plugins/paintops/libpaintop/tests` の `KisCurveOptionDataTest` | カーブの読み書きの往復 | 拡張の土台にする |
| `KisCurveOptionModelTest::test` | **中身が空** | — |
| `libs/brush/tests/KisBrushModelTest` | `brush_definition` の読み書き | ブラシ先端の移行に使う |
| 各エンジンの描画テスト(brushop、colorsmudge、mypaint) | 描画結果 | 回帰確認に使う |
| `plugins/dockers/brushhud/tests` | HUDの表示項目の設定だけ | — |

**存在しないテスト:** `KisPaintopBox`、`KisPaintOpPresetUpdateProxy` の延期と
圧縮、`resetSettings`、ロックされた設定、Uniform Propertyの値の読み書きは
テストされていない。

## 11. 計画への反映事項

計画書に反映する設計判断は次のとおり。

1. **モデルは加工前の値を持ち、焼き込みはモデル側で行う。**
   - 現在、加工はウィジェットのモデルにある(`KisCurveOptionModel::bakedOptionData`
     など)。これを、依存readerを引数に取る純粋な関数としてオプション記述子に
     移す。
   - ブラシ先端の `lightnessModeEnabled()`、`bakedBrushData()`、
     `effectiveBrushSize()` は、ウィジェットの状態ではなく `BrushData` から
     導出する関数に作り直す。
2. **最初の書き込みは全書き込みにする。**
   - プリセットの読み込み、切り替え、再読み込みの後、モデルが最初に書き込む
     ときだけ、現在と同じ全消去と全書き込みを行う。
   - これで旧形式のキーや他エンジンのキーが現在と同じ時点で取り除かれ、
     `Custom<id>` による曲線の復活も起きない。
   - 2回目以降は、オプション単位で書き込み、前回書いたキーとの差分を削除する。
     テクスチャの無効化もこの差分で処理できる。
3. **変更キーの記録を追加する。** 前述の「記録の追加先(推奨)」のとおり。
4. **書き込みと取り込みはロック用プロキシを通す。** 前述の規則に従う。
5. **プリセット読み込み時は `transactional_tag` と `commit()` を使う。**
   複数オプションをまとめて更新し、依存readerが途中の状態を見ないようにする。
6. **移行の順序を見直す。**
   - 先に難易度の低いエンジン(Deform、Curve、Experiment、Grid、Particle、
     Round Marker)のどれかで基盤を検証する。
   - 次に、ブラシ先端とマスクブラシを独立した作業として移行する。
   - そのうえでPixel Brushを移行する。
   - MyPaintは、共有のJSONキーを扱う方式が決まるまで従来の経路のままにする。
7. **ユーザーの判断が必要な挙動の変更**
   - オプション単位の書き込みでは、ロック前の値が `_previous` に残るようになる。
   - 値が変わらない操作では、プリセットが変更済みにならなくなる。
8. **テストの整備をフェーズ1の最初に行う。**
   - 更新通知の延期と圧縮、ロックされた設定、Uniform Propertyの読み書きの
     テストを追加する。
   - 同梱プリセット313件を、新旧の書き込み結果を比べる一致テストの入力として
     使う。

## 12. 実行環境で確認すべき事項

この調査では実行できなかったため、フェーズ1の開始前に開発環境で確認する。

- 開発環境に入っているlagerの版で、同値の場合に通知しない挙動が同じか。
- 同梱プリセットそれぞれについて、F5画面の全書き込みで、どのキーが削除・追加
  されるか(静的解析の結果を実データで確認する)。
- `benchmarks/data` の2件(`filterOp_gauss.kpp`、`sumi70radius.kpp`)が
  抽出できなかった理由。
