# Photoshopのブラシ(ABR)読み込みの改善 計画書

Todo(2026年10月8日登録)から、2026年10月10日にユーザーの依頼で着手した。
参考: photocraft(https://github.com/storytold/photocraft)。

## 参考リポジトリ

- Photoshopのクリーンルーム再実装(Rust)。ライセンスはMITかApache-2.0の
  選択式。MITを選べばGPL-2.0-or-laterと両立する。言語が違うので、コードは
  取り込まず、形式の理解と対応表の参考にする。
- ABRの解析: `crates/psd/src/abr.rs`。版1・2と、版6〜10(副版1・2)。
  `8BIM` の区切りで `samp`(先端)、`patt`(パターン)、`desc`(プリセットの
  記述子)、`phry`(フォルダ構成)を読む。先端は8/16ビット、無圧縮/RLE。
  上限(1辺8192、全体512MiB、名前4096文字、フォルダの深さ32)と範囲の
  確認で、壊れたファイルでも落ちない。
- 設定の対応: `crates/io/src/abr_map.rs`。`desc` の `brushPreset` から、
  名前(`Nm  `)、先端(`Brsh`: `sampledData` で先端のIDを指す、または
  計算の丸先端)、直径 `Dmtr`、硬さ `Hrdn`、角度 `Angl`、丸み `Rndn`、
  間隔 `Spcn`、反転、形状のダイナミクス(`useTipDynamics`: `szVr`、
  `angleDynamics`、`roundnessDynamics`。制御 `bVTy`: 0 なし、1 フェード、
  2 筆圧、3 傾き、4 ホイール、5 最初の向き、6 向き、8 回転)、散布
  (`useScatter`)、テクスチャ(`useTexture`、`Txtr` はIDか名前でパターンを
  指す)、デュアルブラシ(`dualBrush`)、カラーダイナミクス、伝達(不透明度と
  流量)、ツールのオプション(`Opct`、`flow`)を読む。

## Solsticeの現状(2026年10月10日の調査)

- `libs/brush/kis_abr_brush_collection.cpp` と `KisAbrStorage`:
  版1・2と版6(副版1・2)の先端の画像だけを読む。版7〜10は読まない。
- 名前は `ファイル名_番号`。`desc` のプリセット名を使わない。
- 16ビットの先端を正しく読めない(`convertToQImage()` が8ビットとして
  幅×高さバイトだけ読む。データの半分を、上位と下位のバイトが混ざった
  画像にする)。
- 版1・2の計算の先端は読み飛ばす。版6のプリセットの計算の丸先端も無視。
- `desc`(プリセット)、`patt`(パターン)、`phry`(フォルダ)を読まない。
- 範囲の確認が少ない(`malloc(width*height*depth)` の前に上限がない、
  先端のIDを37バイト固定で読み飛ばす)。

ユーザーの手元のABR(すべて版6.2、8ビット):

| ファイル | プリセット(概数) | 先端 | パターン |
| --- | --- | --- | --- |
| 手绘漫画.abr | 80 | 19 | 21MB |
| 蚂蚁老师的笔刷-PS.abr | 38 | 23 | 1.3MB |
| TWOciyuan 画笔.abr | 2 | 1 | なし |
| `libs/brush/tests/data/brushes_by_mar_ka_d338ela.abr` | 36 | 31 | なし |

先端の画像は読めるが、プリセットの設定(大きさ、間隔、筆圧の効き、散布、
テクスチャ、デュアルブラシ)とパターンが失われる。

## 段階

1. **ブラシストロークレイヤーでのABRの先端の保存(2026年10月10日、
   実装済み、手動確認で問題なし):** `docs/agent/brush-stroke-layer-plan.md` の
   「ABRの先端の保存」。
2. **読み込みの堅牢化と名前:** 16ビットの先端、版7〜10、先端のIDの
   読み方、大きさと全体量の上限、範囲の確認。`desc` と `sampledData` で
   先端をプリセット名で呼ぶ。壊れたファイルのテスト。
3. **パターン:** `patt` を読み、パターンのリソースとして出す
   (`KisAbrStorage` がパターンも返す)。
4. **プリセット:** `desc` の記述子を読み(`.asl` の記述子の読み込み
   `KisAslReader` が使えるか調べる)、Pixel Brushのプリセットを作る。
   先端・大きさ・間隔・角度・丸み・硬さ、形状のダイナミクス(筆圧・傾き・
   向き)、散布、テクスチャ(段階3のパターン)、デュアルブラシ(マスク
   ブラシ)、カラーダイナミクス、不透明度と流量。対応できない項目は
   一覧にして警告する。
   ユーザーの要望(2026年10月10日): ABRファイルを1つのバンドルのように
   扱い、ブラシプリセットのバンドルの絞り込み(`KisPresetDockerFilters`)にも
   出す。ABRはすでに保管場所(`KisResourceStorage::StorageType::AdobeBrushLibrary`)
   として登録され、リソースライブラリの管理画面(`dlg_bundle_manager.cpp`)の
   表示対象にも入っている。絞り込みはプリセットを持つ保管場所だけを出すので、
   `KisAbrStorage` がプリセットを返すようになれば出る見込み。段階4で確認する。
5. **フォルダ:** `phry` をタグにする。2026年10月10日に実装(下記)。

追加(ユーザーの要望、2026年10月10日): ブラシエディタのブラシ先端
(Predefined)の一覧に、ブラシプリセットと同じく、所属するバンドル(保管
場所)で絞り込むオプションを付ける。`KisPresetDockerFilters` はエンジンと
バンドルの2つの絞り込みを `KisTagFilterResourceProxyModel` の上に作っている
ので、エンジンを外してバンドルだけにした形を、先端の一覧
(`plugins/paintops/libpaintop/kis_predefined_brush_chooser.cpp` の
`KisResourceItemChooser`)の下に付ける。2026年10月10日に実装
(`docs/agent/brush-preset-grouping.md` の「Brush tips」)。ABRの保管場所は、
プリセットと先端の両方の絞り込みでバンドルとして扱う。

段階2〜5はすべて実施する(ユーザー、2026年10月10日)。2から順に進める。

## 段階2の実装結果(2026年10月10日)

| 場所 | 内容 |
| --- | --- |
| `libs/brush/KisAbrParser.{h,cpp}`(新規) | ABRの解析。境界を確認する読み手(`Reader`)。版1・2(一覧、版2の名前、計算の先端は数えて読み飛ばす)、版6〜10(副版1・2、`8BIM` の区画)。先端は8/16ビット(16ビットは上位バイト)、無圧縮/RLE。上限: 1辺8192画素、全体512MiB、名前4096文字。区画の4バイト境界がないファイルも読む。`patt`・`desc`・`phry` は中身のまま返す(段階3〜5)。各先端のファイル内の番号(`index`)を持つ |
| `libs/brush/kis_abr_brush_collection.{h,cpp}` | 旧解析(`abr_brush_load*`、`AbrInfo`、RLE、`convertToQImage`)を削除し、`KisAbrParser` を使う。先端の識別子(ファイル名、保管場所とDBのキー)は従来どおり `ファイル名_番号`(版2は先端の名前)。表示名は、`desc` を `KisAslReader::readFillLayer()`(版16の記述子をXMLにする)で読み、`sampledData` で先端を参照する最初のプリセットの名前(`Nm  `) |
| `libs/brush/CMakeLists.txt` | `kritapsdutils` をリンク(循環しない) |

互換性: 8ビットの先端の画像は旧解析と同じ(md5も同じ)なので、先端を参照する
既存のプリセットとタグはそのまま。16ビットの先端は旧解析では壊れていた
ので画像とmd5が変わる。

既知の制限: 登録済みのABRは、DBの同期がファイルとmd5の同じ先端を更新しない
ので、表示名が古いまま。ライブラリを外して取り込み直すと新しい名前になる。

テスト `TestAbrParser`(6件、通過): 同梱の `brushes_by_mar_ka_d338ela.abr`
(版6.2、先端31、警告なし、31中30にプリセット名)、合成した版6と版9(16ビット
RLEと8ビット無圧縮の画素値)、版2(名前、計算の先端の読み飛ばし)、壊れた
ファイル(切り詰め400か所、4バイトの書き換え400回、巨大な大きさの宣言)。
`TestAbrStorage` も通過。ユーザーの3ファイルも警告なしで読め、手绘漫画は
19中13、蚂蚁老师的笔刷-PS は23中13の先端にプリセット名が付いた。

手動確認の項目(段階2、2026年10月10日に問題なし):

1. ABRをリソースライブラリの管理画面で取り込み直すと、ブラシエディタの
   先端の一覧に、プリセット名の付いた先端が出る。
2. 以前に取り込んだABRの先端を使うプリセットが、これまでどおり描ける。
3. ABRの先端で描いたブラシストロークレイヤーの保存と拡大(段階1)が変わらない。

## 段階3の実装結果(2026年10月10日)

| 場所 | 内容 |
| --- | --- |
| `libs/brush/kis_abr_brush_collection.{h,cpp}` | `readPatterns()`: `patt` 区画のパターンを、長さで1つずつ切り出して `KisAslReader::readPsdSectionPattern()` に渡す(1つ読めなくても後ろを読む。CMYKやLabなど読めない色モードは飛ばし、数をログに出す)。結果のGIMP形式のデータから `KoPattern` を作る。ファイル名は `<ID>.pat`(PSDやASLのパターンと同じ付け方)、名前は `Nm  `、md5はデータのハッシュ。`patternsMap()`、`patternByName()` |
| `libs/brush/KisAbrStorage.cpp` | 種類ごとの一覧を作る反復子にし、パターン(`ResourceType::Patterns`)も返す。`resourceItem()` と `resource()` は `.pat` で終わるURLをパターンとして扱う。タグ(ファイル名)はパターンにも付ける |

テスト `TestAbrParser::testPatterns`: 合成した版6のファイルの3つのパターン
(RGB、CMYK、RGB)から、CMYKを飛ばして2つを読み、名前・大きさ・画素・md5、
保管場所の反復子と `resourceItem()`・`resource()` が合う。先端も2つ返る。
ユーザーのファイルでは、手绘漫画 から21(読み込み約1.1秒)、蚂蚁老师的笔刷-PS
から7のパターンを警告なしで読めた。

手動確認の項目(段階3、2026年10月10日に問題なし):

1. ABRの入ったバンドル(ABRファイル)のパターンが、パターンの一覧
   (塗りつぶしのパターンやブラシのテクスチャ)に出る。リソースライブラリの
   管理画面で、そのABRにパターンが含まれる。
2. パターンで塗りつぶしやテクスチャに使える。
3. Solsticeの起動の重さが大きく変わらない。

## 段階4の実装結果(2026年10月10日)

| 場所 | 内容 |
| --- | --- |
| `libs/brush/KisAbrPresetConverter.{h,cpp}`(新規) | `desc` の `brushPreset` から Pixel Brush のプリセットを作る。先端は `KisBrushModel::BrushData::write()`(画像の先端は `abr_brush` でファイルの先端を md5・ファイル名・名前で参照し、大きさは直径を先端の長辺で割った倍率。計算の先端は自動先端の円で、フェード = 1 − 硬さ)。カーブのオプションと、マスクブラシ・テクスチャの項目は、`.kpp` と同じ項目名で書く(それらのデータ型は `kritalibpaintop` にあり、`kritalibbrush` から使うと循環する)。センサーは `KisKritaSensorPack::write()` と同じXML。センサーのないオプションは、筆圧に水平のカーブ(`0,1;1,1;`)を付ける(Kritaのオプションはセンサーを1つは持つ前提) |
| `libs/brush/kis_abr_brush_collection.{h,cpp}` | 記述子を一度だけXMLにし、先端の名前と変換の両方に使う。先端をIDで、パターンを `Idnt` で引ける表を渡す。`presetsMap()`、`presetByName()`。対応できない設定は、種類ごとの件数をログに出す |
| `libs/brush/KisAbrStorage.cpp` | プリセット(`ResourceType::PaintOpPresets`、`<ファイル名>_preset_<番号>.kpp`)も返す |

対応表:

| Photoshop | Pixel Brush |
| --- | --- |
| `Brsh`(`sampledBrush` / `computedBrush`)、`Dmtr`、`Angl`(度→ラジアン)、`Spcn`(%→割合) | 先端、大きさ、角度、間隔 |
| 画像の先端の `Rndn` | 比率のオプション(一定値) |
| 計算の先端の `Rndn`、`Hrdn` | 自動先端の比率、フェード |
| `useTipDynamics`: `szVr`+`minimumDiameter`、`angleDynamics`、`roundnessDynamics`+`minimumRoundness` | 大きさ、回転、比率のオプション |
| 制御 `bVTy`: 1 フェード(`fStp`)、2 筆圧、3 傾き、4 ホイール、5 最初の向き、6 向き、7・8 回転 | センサー `fade`(長さ)、`pressure`、`declination`、`tangentialpressure`、`drawingangle`(角度を固定)、`drawingangle`、`rotation`。最小値はカーブの下端 |
| ゆらぎ `jitter` | `fuzzy` センサー(カーブの下端 1 − ゆらぎ)。角度のゆらぎは回転の強さ |
| `useScatter`: `scatterDynamics` のゆらぎ、`bothAxes` | 散布の量(%→倍、上限5)、Y軸 |
| `usePaintDynamics`: `opVr`、`prVr`、ツールの `Opct`、`flow` | 不透明度と流量のオプション(強さと最小値) |
| ツールの `Md  ` | `CompositeOp`(レイヤースタイルと同じ変換表) |
| `useTexture`: `Txtr`(`Idnt`)、`textureScale`、`textureBrightness`、`textureContrast`、`textureBlendMode`、`textureDepth`+`textureDepthDynamics`+`minimumDepth`、`InvT` | テクスチャ(ファイルのパターン、倍率、明るさ = −値/150、コントラスト = 1+値/50、Photoshop互換のモード、強さのオプション、反転) |
| `dualBrush` | マスクブラシ(合成モード、大きさの比 = 副の直径/主の直径、副の間隔、散布) |
| 描画モード | 常に Wash(不透明度で上限、流量で重ねる) |

記述子の整数は符号なしで読まれるので、符号付き32ビットに戻す(例: 明るさ −9 が 4294967287)。

テクスチャの向き: Kritaのマスクの値はパターンの明るさで、明るさの設定は引かれる
(`KisTextureMaskInfo`)。Photoshop の明るさは足されるので符号を反転した。
「高さ」「線形の高さ」「減算」は、Photoshop では暗い所ほど絵の具が減る
(「高さ」の深さは凹凸のどこまで届くか)ので、Kritaの式(高さは
「点の濃さ × 10 × 強さ − マスク」)に合わせてパターンを反転する。深さが小さく
パターンが暗いプリセットは、普通の筆圧ではほとんど描かれない(Photoshop でも
同じと考えるが、未確認)。デュアルブラシの比較(暗)や焼き込みカラーも、小さな
副ブラシでは薄くなる。

対応しないもの(件数をログに出す): 散布の数 `Cnt `(Pixel Brushに相当なし)、
デュアルブラシの散布の数と反転、先端の反転と反転のゆらぎ、カラーダイナミクス、
ウェットエッジ、ノイズ、ブラシのポーズ、テクスチャの保護、`tiltScale`、
ブラシのグループ(`brushGroup`)、スムージング。

テスト `KisAbrPresetTest`(新規、2件、通過): 同梱の `brushes_by_mar_ka_d338ela.abr`
から36のプリセット(ABRの先端31、自動先端5、筆圧で大きさ22)。全プリセットの設定を
Pixel Brushのオプションのデータ型(`KisSizeOptionData`、`KisOpacityOptionData`)で
読み戻せる。12のプリセットで実際に線を描ける(先端とパターンをプリセットの
リソースに入れて)。ユーザーの3ファイルでは、手绘漫画 40、蚂蚁老师的笔刷-PS 38、
TWOciyuan 1 のプリセットができた。中くらいの筆圧の試し描きで何も描かなかったのは、
手绘漫画 で8(上の「高さ」とデュアルブラシのもの)、蚂蚁老师的笔刷-PS で1。

リロード(2026年10月10日、手動確認で見つかった不具合): 保管場所はプリセットを
そのまま渡していたので、ブラシエディタでの編集がファイルの版まで書き換え、
プリセットのリロード(`KisPaintopBox::slotReloadPreset()` →
`KisResourceLocator::reloadResource()` → `loadVersionedResource()`)は常に失敗して
`couldn't reload preset` のアサートが出た。`KisAbrStorage::resource()` はプリセットの
複製を返し、`loadVersionedResource()` は変換で作った版の設定(複製)・名前・画像を
書き戻す。テスト `KisAbrPresetTest::testReload`。

プリセットにmd5を付けてはいけない: md5が空のとき、DBは自分で計算した値を記録する。
ストロークプレビュー(`docs/agent/brush-stroke-preview.md`)は、保管場所から
読み直したプリセットのmd5がDBの値と同じときだけ描くので、別の値を付けると
ABRのプリセットのプレビューが出なくなった(手動確認で判明し、取り消した)。

手動確認の項目(段階4、2026年10月10日に問題なし):

1. ブラシプリセットの一覧に、ABRファイル名のバンドルとしてプリセットが出る
   (Bundles の絞り込みで選べる)。
2. ABRのプリセットで描ける。大きさ・不透明度・流量の筆圧、散布、テクスチャ、
   デュアルブラシが、Photoshop での描き味に近い。
3. 薄すぎる、または何も描かないプリセットがあれば、その名前を伝えてもらう。
4. 起動の重さが大きく変わらない。

## 段階5の実装結果(2026年10月10日)

| 場所 | 内容 |
| --- | --- |
| `libs/brush/KisAbrPresetConverter.{h,cpp}` | `presetFolders()`: `phry` の記述子(`desc` と同じく `KisAslReader::readFillLayer()` でXMLにする)の `hierarchy` の一覧を読み、プリセットごとのフォルダの道筋を返す |
| `libs/brush/kis_abr_brush_collection.{h,cpp}` | `presetFolders()`: プリセットのファイル名 → フォルダ名の一覧(最上位のプリセットは入れない) |
| `libs/brush/KisAbrStorage.cpp` | プリセットのタグに、ファイルのタグに続けてフォルダごとのタグを出す。`tags()` はファイルを読み込んでから返す |

`hierarchy` は字句の並び: フォルダの始まり `Grup`(名前 `Nm  `、識別子
`zuid`)、終わり `groupEnd`、プリセット `preset`。字句は記述子のクラス、
項目が1つだけの記述子ならその項目のキー、列挙や文字列ならその値で見分け、
どれでもないものはプリセットとする。中身を一覧として持つフォルダ(入れ子の
形)も読み、一覧の終わりで閉じる。プリセットの字句は `desc` の
`brushPreset` の順(段階4のファイル名の番号、飛ばしたものも数える)に
対応させる。字句が多ければ余りは捨て、少なければ残りは最上位。対応する
プリセットがない(先端がなく飛ばした)ものは無視する。余分な `groupEnd` は
無視し、最後まで閉じないフォルダはそこで閉じる。32段より深いフォルダは
32段目に入れる。読めない `phry` は、フォルダなし(最上位)になる。

この形式は公開仕様がない。手元のABR(ユーザーの3ファイル、同梱、Krenz)は
`phry` がないか、`hierarchy` が空なので、実物のフォルダでは未確認。
字句の名前は他の実装の説明(PhotoCraft の ABR 読み込み、
https://github.com/storytold/photocraft/pull/2001)と同じにした。

タグ: URL は `<ファイル名>/<フォルダ>/<サブフォルダ>`、名前は
`<ファイル名の拡張子なし> / <フォルダ> / <サブフォルダ>`(別のファイルの
同名のフォルダと区別する)。フォルダのタグは、そのサブフォルダのプリセットも
含む。ファイル自体のタグ(全プリセット)はそのまま。保管場所のタグは起動時の
同期(`KisResourceLocator::synchronizeDb()` の `addStorageTags()`)でも
追加されるので、登録済みのABRにも次の起動で付く。

テスト `KisAbrPresetTest`(2件追加、全7件通過): `testFolders` は同梱ファイルに
`phry` の区画を足し(余分な終わり、閉じないフォルダを含む)、フォルダと
タグ(名前、中身、入れ子の親が子のプリセットを含むこと)を確かめる。
`phry` のないファイルはファイルのタグだけ。`testFolderForms` は、項目が
1つの記述子の形、入れ子の形、32段の上限。

手動確認の項目(段階5、2026年10月10日に問題なし。フォルダのある実物のABRでは未確認):

1. これまでのABRで、プリセットの一覧とファイル名のタグが変わらない
   (手元のABRにはフォルダがない)。
2. フォルダのあるABR(Photoshop 2020以降でフォルダごと書き出したもの)が
   あれば、取り込むとタグ一覧に `<ファイル名> / <フォルダ>` が出て、選ぶと
   そのフォルダのプリセットだけが出る。
