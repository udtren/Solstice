# GPUエンジン調査・改善の引き継ぎ（Claude向け）

更新: 2026年10月6日。現在の到達点はフェーズ4.81（キャンバス更新の送信集約）の
実装・自動テスト・インストール完了。手動確認はユーザー報告で問題なし。
実機採取PID 43720の解析済み（`gpu-engine.md` の phase 4.81 follow-up）。
続いてフェーズ4.82（GPUブラシのバイト上限バッチの即時継続）を実装・テスト・
インストール済み。4.82の実機採取PID 45728を解析済み（手動確認用の別プロセス42316も保存）。
ダブ待ちは4.80と同程度に戻り、入力→表示も4.80の範囲。高速化とは断定しない。

## 最初に把握すること

現在の作業はGPUエンジンの固定オーバーヘッド調査・改善。優先順位2の途中。
**実機の256px Buildupでは、キャンバス送信前の共有ロック待ちの約95%が、
別スレッドの `vkQueueSubmit2` 呼び出しと重なっている。**
context側キューロック取得と送信情報の準備は小さい区間だった。
ドライバー呼び出しが長い理由、GPU実行時間、その改善方法は未確定。

PID 24148のsubmitを呼び出し元で分類すると、キャンバス834回・合成428回・
ブラシ62回に対し、GUI側の反映は169回だった。キャンバス待ちと重なる他スレッドの
ドライバー呼び出しは、ほぼ別スレッドのキャンバス送信（353.1ms）だった。

フェーズ4.81で、同時に届いたキャンバス更新を1回のGPU送信にまとめる実装を追加
（詳細は `gpu-engine.md` の「Shared canvas update builds (phase 4.81)」）。
ロックは外していない。次は下記「次の作業」の手動確認と集中採取。

## リポジトリ・操作上の条件

- ソース: `C:\Users\udtre\Projects\krita`
- ブランチ: `krita-sol-gpu`。GitHubの既定ブランチでもある。
- 作成時のHEAD: `510277ce2f6d4b58f008f9ca83e4b7ebe6c46551`（件名 `update`）。
  起動時に `git status` とHEADを再確認すること。
- origin: `https://github.com/udtren/Solstice.git`。上流同期は終了済み。
- 既存の変更はユーザー／前任者の作業。reset、破棄、無関係な変更の巻き戻しは禁止。
- この引き継ぎ作成時点では、フェーズ4.79〜4.81の計測・解析・修正は未コミット／未Push。
  コミットやPushは新たな依頼がある場合に実施する。
- `AGENTS.md` を読む。古い `development-workflow.md` のブランチ・上流同期規則は
  現在のAGENTSにより上書きされている。ビルド・テスト・インストール規則は有効。
- Androidを復活させない。Puppet Warp、ネイティブ移行済み機能の構造を保つ。
- `docs/agent/development-workflow.md` の無関係なユーザー変更を編集しない。
- **アプリの起動と終了はユーザーに依頼する。勝手に起動・終了・強制終了しない。**
  ユーザーは最後の採取後に「閉じました」と報告済み。
  インストール前には再度実行中プロセスを確認する。
- `%LOCALAPPDATA%\kritarc` とGPU設定を勝手に変更しない。
- ソース・資料は `apply_patch` で編集。C++は指定のclang-formatで変更行のみ整形。
  変更した追跡ファイルに `git diff --check` を実行する。

## 読む資料

作業順序の正本は [gpu-work-priorities.md](gpu-work-priorities.md)。
詳細な実装・検証記録は [gpu-engine.md](gpu-engine.md)、特にフェーズ4.72〜4.80。
ユーザー向け説明は [../gpu-engine.md](../gpu-engine.md)。
基準採取の明示的なウォームアップ／測定IDは
[paint-trace-baseline-runs.md](paint-trace-baseline-runs.md)。
変更前には一般ガイド `codebase-map.md`、`extension-points.md`、`coding-rules.md`、
`development-workflow.md`、`feature-inventory.md` も読む。

現在の順序:

1. 入力→表示の計測整備（ソフトウェア基盤・9回比較は完了。物理的な発光は未計測）
2. 固定オーバーヘッドの分解と改善（現在ここ）
3. 単純な円形ダブからGPU生成
4. フィルタ・変形計算のGPU化（全体計画のフェーズ5）
5. 残りのBlend Mode対応（ユーザー指定で最終順位へ移動）

大径ブラシ×ミラー×Alpha Lockのレアケース追従遅延は、ユーザーが明示的に先送り済み。
許容範囲とされた大径テクスチャブラシの追従遅延も、この引き継ぎで再調査対象にしない。

## 現在の環境と計測対象

ユーザー申告の環境: Windows 11、NVIDIA RTX PRO 6000 Blackwell、ドライバー596.86、
Qt 6.8 / QOpenGLWidget / デスクトップOpenGL 4.6互換プロファイル、Vulkan 1.3。
Vision MLのggml-vulkanが別のVkInstance/VkDeviceを作る構成。

開発ルート: `C:\Users\udtre\Projects\krita-dev`

- ビルド: `_build`
- テスト用インストール: `_install`
- 環境: `env.bat`
- Python: `PythonEnv\Scripts\python.exe`
- clang-format: `llvm-mingw-20251118-ucrt-x86_64\bin\clang-format.exe`
- Vulkan検証レイヤー: `VulkanSDK\1.4.357.0\Bin`

採取対象は2480×3508、RGBA 32bit float、Basic-4 Flow Opacity、正確に256px。
プリセット保存MD5は `22a33a4c8fd817194fc9fe7b80e35b18`。
各プロセスでBuildupを3本、続いてWashを3本。各条件の最初はウォームアップ。
入力は手描きで、線の長さ・入力数・更新数が完全には一致していない。

## 重要な測定の限界

- 主指標はQt入力受信→最後の必要な描画コマンドの `frameSwapped` 通知。
  ペンの物理入力→画面の発光、全画素の生存を保証する指標ではない。
- CPUスコープには待機を含むことがある。GPUカーネルの実行時間とは異なる。
- 共有されたバッチ／walkerは入力同士を相関させる。中央値やP95は加算しない。
- ロック重なりの合計は「各待機区間の時間の合計」。並行待ちが同じ保持区間を
  複数回数えるため、ストロークの経過時間と同一視しない。
- GPU投影設定の基準採取では子画像を再利用し、追加レイヤー合成を省略する経路が
  観測された。多層GPU合成のベンチマークとして扱わない。
- 時刻が近いだけの入力・更新・フレームは関連付けない。明示IDと領域被覆を使う。
- `dropped_events` が非ゼロのログは性能の判断に使わない。
- `residency.hold.*` は10µs未満を意図的に省略。
  メタデータの `residency_hold_min_us: 10` を確認する。短い保持の省略は記録欠落とは別。
- フェーズ4.80の外部区間イベントは処理後にログへ追加される。
  JSON配列の前後関係ではなくタイムスタンプ・同じスレッド・明示IDで照合する。

## 直近の実機結果

最新: PID **24148**、165計測入力、ウォームアップ除外後108入力。
ダブ要求なしの52入力は除外。記録欠落なし。

| 条件 | ウォームアップID | 測定ID |
| --- | --- | --- |
| Buildup | 4027 | 5252、6402 |
| Wash | 7986 | 9250、10529 |

測定ストロークのキャンバス送信を明示walker IDで選択し、その中のロック待ちを
同一backend・別スレッドの保持／送信区間と照合した。

| 測定ストローク | キャンバス待ち件数 | 待ち合計ms | 他送信保持と重なるms | 他ドライバー呼び出しと重なるms | 他キューロック待ちと重なるms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Buildup 5252 | 136 | 108.5455 | 104.0951 | 103.0312 | 0.0187 |
| Buildup 6402 | 125 | 94.1548 | 90.7168 | 89.7886 | 0.0206 |
| Wash 9250 | 142 | 14.2552 | 13.1517 | 12.2399 | 0.4865 |
| Wash 10529 | 132 | 10.2298 | 9.2950 | 7.7846 | 1.1863 |

Buildupの約95%という値はドライバー呼び出しとの時間的重なり。
ドライバーバグ、GPU処理時間、ggmlとの干渉を断定する根拠ではない。
直接選択したBuildup送信のドライバー中央値は0.0436／0.0482msだが、
最大2.467／2.400msの長い呼び出しもある。

## 実装済みだが効果を確認できなかった変更

フェーズ4.79で `KisGpuCommandList::finishMainRecording()` を追加し、
メインコマンドの `vkEndCommandBuffer` をresidencyロック取得前へ移した。
アップロード用preambleの記録、キュー送信、状態公開はロック内に残している。

この処理は実機で中央値0.0007〜0.0016msと短く、**速度改善は確認できなかった**。
修正後の単一プロセス計測では表示までの値が長くなったが、手描き入力数や更新数が
異なるため、この変更による悪化とは断定していない。
READMEやベンチマークで「高速化達成」と扱わない。
変更は回帰テスト済みの状態で保持している。現時点で再変更・取り消しはしていない。
閉じた報告だけからUndo/Redo成功を推測しない。明示的な実機操作成功の回答は未取得。

## フェーズ4.81の要点（未コミット）

- `libs/ui/canvas/KisCanvasUpdateBatcher.*`（新規）: グループコミット。
  各スレッドは自分の更新を含むビルドが終わるまで戻らない。ビルドは重ならず到着順。
  1バッチは最大32件・先頭以外で1Mpxまで。同一スレッドからの再入は単独ビルド。
- `kis_canvas2.cpp`: `sharesProjectionUploads()` が真のときだけ集約。
  更新情報は要求ごとに1つ、`update.ready` は各要求のflowで記録、
  `update.batched` リンクを追加。`putUpdateInfos()` でまとめて圧縮器へ。
- `KisOpenGLUpdateInfoBuilder::buildUpdateInfos()`: 全矩形のタイルを1回で
  `KisGpuCanvasUploader::upload()`。失敗時は矩形ごとに従来のCPU経路。
- `KisGpuCanvasUploader::upload()`: 64px未満で近接するパッチだけを同じ
  source領域にまとめ、領域ごとの `KisGpuTileAccess` を1回の送信に入れる。
- `KisGpuCanvasUpload` のGL保持は入れ子。`KisOpenGLCanvas2::updateCanvasProjection(QVector)`
  が全更新を1回保持し、全更新の反映後に解放する。
- テスト: キャンバス38件（新規 `testSharedUploadMatchesCpu` 10件）、
  トレース有効でも38件、新規 `KisCanvasUpdateBatcherTest` 8件、解析50件。
  既存のGL import失敗テストは自分の保持を解放するよう修正した。
- `libkritaui.dll` のbuild/install SHA256一致を確認済み。GPU/imageは4.80から変更なし。

## 実機結果（PID 43720）と次の作業

- PID 41908は手動確認と同じプロセスで記録上限を超えた（dropped 213,587）。性能判断に使わない。
- PID 43720（欠落0）: キャンバス送信834→241、ロック待ち合計459.8→73.0ms、
  Buildupの投影終了→更新準備は約3.5〜4.3→0.3〜0.4ms。被覆検証は731件すべて合格。
- 入力→表示の中央値は増加（Buildup 22.9/24.1→38.5/32.6ms、Wash 21.1/28.4→39.5/50.4ms）。
  増加はダブ要求→ブラシバッチ取り込みの待ち（10〜15→21〜35ms）。
  ジョブ待ち・バッチ周期・描画開始は不変。今回のストロークは102〜132msと短く、
  860〜1,566ダブ/秒と速いため、1バッチ約16〜19ダブの上限でダブが滞留した。
  手描き速度と交絡しているため、キャンバス変更の効果・悪化とは断定しない。

ユーザーは候補2（ブラシ側の滞留調査）を選択し、フェーズ4.82で対応した:
- 上限は時間ベースの `dabsLimit` ではなく、GPUブラシの32MiBソースバイト上限。
  プリセットの描画方向回転で256pxダブが約362px角（約2.1MB）になり約16ダブで打ち切り。
- `takeReadyDabs()` に任意の `stoppedByByteLimit` を追加。`KisBrushOp` は
  バイト上限で打ち切られ描画済みダブが残る場合に更新周期を0にする。
  トレース `batch.byte_limited` を追加。CPU経路と他の上限は従来どおり。
- `KisDabRenderingQueueTest` 12件（トレース有効13件）、`KisGpuStrokeTest testStroke` 16件合格。
  `kritadefaultpaintops.dll` のbuild/install SHA256一致。

次の作業:
1. 4.82の手動確認はユーザー報告で問題なし（速い斜め・水平、ミラー、Undo/Redo）。
2. ユーザー指示で順位3（GPUでのダブ生成、単純な円形ダブから）に着手した。
   フェーズ4.83（第1段階）を実装・テスト・インストール済み。詳細は `gpu-engine.md` の
   「GPU-evaluated circle dabs (phase 4.83, priority 3 step 1)」。
   - 対象: RGBA32F・デフォルト円形・ベクトル経路・テクスチャ/シャープネスなし・単色。
   - CPU生成は残し、GPUは画素の代わりに円の記述（64バイト）を受け取る。
   - 記述は生成画素と照合してから使う。ミラーは `proceduralFlips` とパスのフリップのXOR。
   - 次: 手動確認（256px Buildup/Wash、測定プリセット、ミラー、Undo/Redo、テクスチャ付き）と
     6ストロークの集中採取（`path.brush.generated_dabs`、バッチ数、ダブ待ち、入力→表示）。
   - その後の候補: CPU生成の省略（フォールバック時のみ生成）、Gauss/Soft、RGBA16F。
3. 4.83の実機採取PID 38504では生成0件（測定プリセットがGauss）。フェーズ4.84でGaussを追加し、
   CPUのAVX2+FMAカーネル（-ffp-contract=fastで融合）をビット単位で再現した。
   詳細は `gpu-engine.md` の「Exact Gaussian and fused default circle dabs (phase 4.84)」。
   - 実機PID 14156: 生成ダブ使用40/40、ブラシ送信CPU時間 中央値1.83→0.43ms。
     入力→表示はBuildupで改善傾向、Washは同程度（手描きのため断定しない）。
   - 4.84の手動確認はユーザー報告で問題なし。
4. ユーザー指示でフェーズ4.85（CPU生成の省略）を実装・テスト・インストール済み。
   詳細は `gpu-engine.md` の「Skipped CPU generation of described dabs (phase 4.85)」。
   - 種別ごとに16ダブ検証後に省略。CPU利用時は `KisRenderedDab::materialize()` で生成。
   - 手動確認OK。実機PID 39228: ストローク中のダブは検証13個以外すべて省略、
     ダブ生成ジョブ中央値248→19µs。入力→表示は4.84と同程度。
   - 残る候補: ブラシ最小更新周期（10ms）の見直し、非FMA CPU用の変種、
     プレビュー描画（非対象デバイス）の扱い。
5. ユーザー指示でフェーズ4.86（RGBA16F）と4.87（Soft曲線マスク）を実装・テスト・インストール済み。
   詳細は `gpu-engine.md` の該当節。手動確認（Softブラシ、16bit float文書、ミラー、Undo/Redo）はユーザー報告で問題なし。
6. 4.79〜4.87はコミット `60040d6ac5`（未Push）。ユーザー指示でフェーズ4.88（GPU経路の
   ブラシ更新周期を-1に短縮）を実装・テスト・インストール済み、未コミット。
   詳細は `gpu-engine.md` の「GPU brush update period (phase 4.88)」。
   手動確認OK。実機PID 45620: ダブ待ち中央値 約10ms→0.3〜0.8ms、
   入力→表示中央値 18〜24ms→7〜13ms（4.85のPID 39228比）。コミット `cb86388460`。
7. フェーズ4.89（Wash遅延の分解）: バッチ終了後の更新再試行と、合成内部のトレース区間を実装・
   テスト・インストール済み。未コミット。手動確認OK。
   実機PID 24596: Washのバッチ取り込み待ち 3.8→0.39ms、入力→最後の転送 9.8→7.75ms。
   Wash合成の最大要因はCPUでの元画像複写（1.08ms）で、次の候補はそのGPU化。
   コミット `0eaab86e83`。
8. フェーズ4.90（Washの元画像複写のGPU化）を実装・テスト・インストール済み。未コミット。
   詳細は `gpu-engine.md` の「GPU base copy in the Wash preview (phase 4.90)」。
   実機PID 55584では背景レイヤー（初期ピクセルが不透明）のため使われていなかった。
   初期ピクセルが同じであればその色で埋めるよう修正済み、修正後は未計測。
   最初のWashストロークの突出（約36ms）は原因未確定。
   修正後PID 52420: 2本目以降のWashの入力→表示 5.8〜6.5ms（4.89は11ms台）。手動確認OK。
   最初のWashストロークの遅れは再現（初回だけのコスト）。コミット `748c09657e`。
9. フェーズ4.91（パイプラインの共有と事前コンパイル）を実装・テスト・インストール済み。未コミット。
   初回Washの遅れの原因は、作業コンテキストごとのパイプラインのコンパイル（約60ms）。
   実機PID 51608: 最初のWashの入力→表示 36.2→10.8ms、最初のBuildup 14.1→5.2ms。手動確認OK。
   コミット `e9d49f459a`。
10. フェーズ4.92（GPU共有転送の経路でキャンバスへの転送を待たせない）を実装・テスト・インストール済み。
    未コミット。実機PID 12232: Washの入力→表示 8.2→5.7ms、Buildupは変化なし（5.4→5.6ms）。
    残りは画面更新の待ちが中心。手動確認OK。コミット `a7a007ab89`。
11. 順位4に着手。フェーズ4.93でフィルタと変形のCPU時間を計測した（ベンチマークのみ、未コミット）。
    変形ツールの描画（アフィンから）を最優先とする案を提示し、ユーザーの選択待ち。
    ユーザー自身がコミット `8447ac0ada update` としてコミット・Push済み。
12. フェーズ4.94（アフィン変形のGPU化）を実装・テスト・インストール済み。未コミット。
    CPUとビット単位で一致し、2480x3508で432→63ms。手動確認OK。
   - 残り候補: CPU生成の省略、Soft（curve）マスク、RGBA16F、非FMA CPU用の変種。
3. さらに減らすなら、ダブをRGBA F32ではなくマスク＋色で送る（転送量削減）か、
   GPU側のダブ生成（順位3）を検討する。

## 現在の未コミット変更

以下は作成時の `git status` に出ているもの。既存変更として保持する。

| 場所 | 主な内容 |
| --- | --- |
| `build-tools/paint-trace/overhead.py`、`test_overhead.py` | 発行前後・更新準備・投影区間の解析とテスト |
| `build-tools/paint-trace/summarize.py` | 新しい解析結果の統合 |
| `build-tools/paint-trace/residency.py`、`test_residency.py`（未追跡） | 同一ロック・別スレッドの区間重なり解析 |
| `libs/gpu/KisGpuCommandList.{h,cpp}` | 早期記録終了、二重終了防止、任意の送信時刻出力 |
| `libs/gpu/KisGpuContext.{h,cpp}` | `KisGpuSubmitTiming`、queueロック・driver呼び出しの時刻 |
| `libs/gpu/tests/KisGpuEngineTest.{h,cpp}` | 早期終了・preamble順序・並行送信・失敗後復帰のテスト |
| `libs/image/KisPaintTrace.{h,cpp}` | 10µsの保持記録閾値、処理後に保存する `externalSpan()` |
| `libs/image/gpu/KisGpuTileAccess.cpp` | 準備・送信の細分計測、早期記録終了、処理後の区間保存 |
| `libs/image/gpu/KisGpuTileBackend.cpp` | 各residencyロック保持元の計測 |
| `libs/image/gpu/KisGpuProjectionCompositor.cpp` | コンテキスト取得・待ち・target/layer準備・送信の計測 |
| `libs/ui/opengl/KisGpuCanvasUploader.cpp` | コンテキスト・source準備・共有buffer・送信の計測 |
| `docs/agent/gpu-engine.md`、`gpu-work-priorities.md`、`feature-inventory.md`、`docs/gpu-engine.md` | 状態・実装・検証の記録 |
| `libs/ui/canvas/KisCanvasUpdateBatcher.*`、`libs/ui/tests/KisCanvasUpdateBatcherTest.cpp`（未追跡） | フェーズ4.81の集約器とテスト |
| `libs/ui/canvas/kis_canvas2.cpp`、`kis_canvas_updates_compressor.*`、`kis_abstract_canvas_widget.h`、`kis_canvas_widget_base.*` | フェーズ4.81の集約経路 |
| `libs/ui/opengl/KisOpenGLUpdateInfoBuilder.*`、`kis_opengl_image_textures.*`、`KisOpenGLCanvasRenderer.*`、`kis_opengl_canvas2.*`、`KisGpuCanvasUploader.*` | 複数矩形の共有アップロードとGL保持 |
| `libs/ui/CMakeLists.txt`、`libs/ui/tests/CMakeLists.txt`、`libs/ui/tests/KisGpuCanvasUploadTest.cpp` | 新規ファイル登録とテスト |

この引き継ぎファイル自体と、その案内リンクも今回追加している。
より古い計測基盤などは既にHEADに含まれており、差分に出ないものもある。

## コードの入口と次の作業の注意点

重要な入口:

- `libs/ui/canvas/kis_canvas2.cpp`: `startUpdateCanvasProjection()`、更新キューの消費。
- `libs/ui/opengl/KisOpenGLUpdateInfoBuilder.cpp`: タイルパッチ構築とGPU upload呼び出し。
- `libs/ui/opengl/KisGpuCanvasUploader.cpp`: 共有bufferへのパッチ書き込みと送信。
- `libs/ui/canvas/kis_canvas_updates_compressor.cpp`: 更新包含時の圧縮と順序維持。
- `libs/image/gpu/KisGpuTileAccess.cpp`: source準備、世代判定、送信、状態公開。
- `libs/gpu/KisGpuContext.cpp`: 単一compute queueとタイムラインの直列送信。

**既存の更新圧縮はGPUパッチ準備・送信の後に行われる。**
`startUpdateCanvasProjection()` がbuilder経由でGPU uploadを行ってから
`projectionUpdatesCompressor.putUpdateInfo()` を呼ぶ。
GUI側の圧縮だけを強化しても、既に発行したVulkan送信は削減できない。

次の実装前に、準備前の更新集約または準備段階の送信共有が可能かを検討すること。
圧縮済みという理由だけでsourceデバイスの読む時点を遅らせると、スナップショットの
意味や読み取り競合を変える可能性がある。呼び出しスレッドと画像更新の契約を調べる。

守る条件:

- uploadの世代判定、古いCPU snapshotによる新しいGPU内容の上書き防止。
- residencyロック内のキュー順序・状態公開・エンジン停止との整合性。
- fallback時のCPU内容復元と、GPUの最新内容を失った場合の既存エラー経路。
- tile pin、COW、readback／evictionとのロック順序。
- 文書／projection／LOD／表示色空間・変換条件／チャネル表示／proofingの違い。
- パッチの中心・余白・mipmapと、更新領域全体の正確な被覆。
- 重ならない遠い領域を無条件に外接矩形へ広げ、余計な転送を増やさない。
- バッチ開始・終了、LOD切替、画像サイズ変更等のmarker境界を越えない。
- `update.merged`／`update.superseded` の来歴を保持し、計測の被覆検証を通す。
- GL interop失敗時のCPU切替と、共有bufferのセマフォ／寿命を保持。

## ログ・基準データの保存場所

すべてローカルの一時フォルダー。リポジトリには生ログを追加していない。

- 9回の基準採取:
  `C:\Users\udtre\AppData\Local\Temp\solstice-paint-baseline-471`
  - `run-01-cpu` から `run-09-projection`（途中はcpu/projection/brushの混在）。
  - 各runに生JSON、launcher log、`summary.json`。
  - `comparison.json` に明示的な測定stroke IDと集約結果。
  - `upload-boundary-comparison.json` に発行前後の解析。
- 実機診断の保存フォルダー:
  - `solstice-stage-476-real-43812`
  - `solstice-stage-477-real-11944`
  - `solstice-stage-478-real-44756`
  - `solstice-stage-479-real-3704`
  - **`solstice-stage-480-real-24148`（最新）**
- 上記5フォルダーはすべて
  `C:\Users\udtre\AppData\Local\Temp\` の下。
- `solstice-stage-478-canvas.42372.json` はフィルタ導入前にoverflowした無効ログ。
  有効な自動テスト採取は `solstice-stage-478-filtered-*`。
- 最後の自動テストは `solstice-stage-480-*`。
  `build.log`、`install.log`、`engine.txt`、`device.txt`、canvas/strokeのon/offログ。
- `solstice-paint-trace-brush.launch.log` は次回起動で上書きされる。
  新規採取後はPID付きJSONとlaunch logを専用フォルダーへ保存する。

再解析例（PowerShell、ソースルートで実行）:

```powershell
& C:\Users\udtre\Projects\krita-dev\PythonEnv\Scripts\python.exe -B `
  build-tools/paint-trace/summarize.py `
  C:\Users\udtre\AppData\Local\Temp\solstice-stage-480-real-24148\solstice-paint-trace-brush.24148.json

& C:\Users\udtre\Projects\krita-dev\PythonEnv\Scripts\python.exe -B `
  -m unittest discover -s build-tools/paint-trace -p 'test_*.py'
```

`residency.py` の通常出力は全待機を含む。
本資料のBuildup/Wash別の値を再現する場合は、明示測定stroke IDに属する入力の
`pipeline.input_audit.downstream_walkers` を使い、canvas submitに包含される待ちだけを選ぶ。
他スレッドのholderは同じstrokeのIDに限定しない。限定すると実際の競合を除外してしまう。

## 最後の検証とインストール状態

フェーズ4.80のビルド・インストールは完了。
`libkritagpu.dll`、`libkritaimage.dll`、`libkritaui.dll` のbuild/install SHA256一致を確認済み。

Vulkan検証レイヤー有効で合格:

- 選択した `KisGpuEngineTest`: 7件（初期化・終了を含むQtTest合計）。
  新規4スレッド128送信の検証を含む。
- `KisGpuPaintDeviceTest` のshared arena、停止後送信拒否、送信失敗内容保持、
  古いアップロードの順序: 13件。
- `KisGpuCanvasUploadTest`: 28件、トレース有効／無効とも合格。
- `KisGpuStrokeTest` の64/256px Buildup/Wash: 6件、トレース有効／無効とも合格。
- Python解析: 50件。
- 新しい内部3区間はキャンバス1,191送信・ストローク258送信で、各々一意な
  親queueスコープ内に収まり、記録欠落なし。

ビルド例:

```bat
cmd.exe /d /s /c "call C:\Users\udtre\Projects\krita-dev\env.bat >nul 2>&1 && cmake --build C:\Users\udtre\Projects\krita-dev\_build --target kritaui KisGpuEngineTest KisGpuPaintDeviceTest KisGpuCanvasUploadTest KisGpuStrokeTest -j 8"
```

`env.bat` には機密情報が含まれる可能性があるため、内容を出力しない。
テスト実行はこの環境を読み、作業ディレクトリを `_build\bin` にして行う。
`KRITA_GPU_VALIDATION=1`、`VK_LAYER_PATH` を上記SDKのBinへ設定。
QtTest結果は `-o <結果ファイル>,txt` で保存すると確実に確認できる。

**`ctest` で `libs-ui-*` を一括実行しない。`kis_kra_saver_test` も実行しない。**
ダイアログでブロックする既知の経路がある。

DLL変更後は該当ライブラリをインストールし、ユーザーに完全再起動してもらう。
GPU API変更でimage/UIも再ビルドされた場合は、そのDLLも揃える。

## 必要になった場合だけ依頼する実機採取

ユーザーに次のコマンドを実行してもらう。エージェント自身は起動しない。

```bat
C:\Users\udtre\Projects\krita\build-tools\paint-trace\run.cmd C:\Users\udtre\Projects\krita-dev brush
```

GPU設定はlauncherの環境変数で指定される。kritarcを書き換えない。
同じ文書・プリセット・256pxでBuildup3本→Wash3本、各先頭はウォームアップ。
通常終了後の「閉じました」を受けてからPID付きJSONを調べる。
起動／終了の報告は描画・Undo/Redo・保存再読込の成功回答とは区別する。

## Claudeへの開始指示（貼り付け用）

> `C:\Users\udtre\Projects\krita\docs\agent\gpu-engine-handoff.md` とAGENTS.mdを読み、
> 現在の `krita-sol-gpu` ブランチでGPUエンジン作業を引き継いでください。
> `gpu-work-priorities.md` の優先順位2を続けます。最新PID24148の実機診断では、
> Buildupの共有ロック待ちの約95%が他スレッドのvkQueueSubmit2と重なっています。
> 次はGPUパッチ準備前／準備段階のキャンバス更新集約による送信回数削減を検討し、
> 安全な実装・回帰テスト・インストールまで進めてください。
> 既存の未コミット変更を保持し、単純にロックを削除しないでください。
> アプリの起動・終了はユーザーに依頼し、必要になるまで追加採取を繰り返さないでください。
