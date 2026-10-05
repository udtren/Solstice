# Paint trace baseline run sheet

This is the controlled comparison following GPU phases 4.69-4.71. See
[the protocol](gpu-engine.md#controlled-capture-protocol) for measurement boundaries
and limitations. Earlier four-stroke feasibility captures are excluded.

## Frozen build

Source HEAD: `97a45044e5c39323b1e6e443c9f12c1fa5c03430`, with uncommitted native
trace instrumentation through phase 4.71. HEAD alone does not identify this build.
The selected installed binary hashes below were captured before run 1. Do not
rebuild or install native changes during this series. If binaries change, start
a separate series instead of pooling results. These hashes identify the selected
artifacts, not every runtime dependency.

| Installed artifact | SHA-256 |
| --- | --- |
| `bin/krita.exe` | `58f5ae6419934406df316b47ca71b8d4e48c8d7fe24534bd66bc7f9e381a5d20` |
| `bin/libkritaversion.dll` | `a68ea41629edb971fd4f1e545de1cd0da01e77b6a104459760eed87b440f53ed` |
| `bin/libkritaimage.dll` | `afce50588fd1f532e599ce3c69d47853505d089b912c16c1d9a9638440cbc79a` |
| `bin/libkritaui.dll` | `6b62788f17d128f7244d9ffce625024117a131a6c48e4f0dd566925afe3b69d3` |
| `lib/kritaplugins/kritadefaultpaintops.dll` | `3aecd9f8a5a188c64b0e2691110ff52d7171e93e013c447911c31d3148a068d2` |

## Run order and acceptance

| Run | Round | Requested mode | Trace / acceptance |
| ---: | ---: | --- | --- |
| 1 | 1 | cpu | `solstice-paint-trace-cpu.46096.json`; trace checks accepted, see below |
| 2 | 1 | projection | `solstice-paint-trace-projection.8352.json`; trace checks accepted, see below |
| 3 | 1 | brush | `solstice-paint-trace-brush.2716.json`; trace checks accepted, see below |
| 4 | 2 | projection | `solstice-paint-trace-projection.40868.json`; trace checks accepted, see below |
| 5 | 2 | brush | `solstice-paint-trace-brush.45512.json`; trace checks accepted, see below |
| 6 | 2 | cpu | `solstice-paint-trace-cpu.43820.json`; trace checks accepted, see below |
| 7 | 3 | brush | `solstice-paint-trace-brush.40716.json`; trace checks accepted, see below |
| 8 | 3 | cpu | `solstice-paint-trace-cpu.45132.json`; trace checks accepted, see below |
| 9 | 3 | projection | `solstice-paint-trace-projection.3752.json`; trace checks accepted, series complete |

Each user-operated process has 16 strokes: four each at 64px Buildup, 64px Wash,
256px Buildup and 256px Wash, in that order. The first stroke in each group is
warm-up; the following three are measurements. Record actual warm-up input IDs
after verifying the group order against the user's execution and snapshots.
Do not silently accept extra/missing strokes or infer warm-up solely from timestamps.

Reopen the same unchanged blank RGBA32F 2480x3508 document in each process. Keep
the layer structure, preset, unsaved brush options, tablet/mouse, smoothing,
display/window geometry and viewport unchanged; do not save measured strokes
into the source document. Keep strokes short, approximately equal in length and
duration, separated by pauses and in clean visible areas. Do not change zoom/pan
to make room; use smaller strokes. Use exact numerical brush sizes. Record any
deviation rather than retroactively assuming matched conditions. Display refresh
rate, zoom, smoothing and unsaved preset settings still need user-provided context;
they are not established by the stored preset hash.

After each normal closure, record the exact PID-suffixed JSON filename, dropped
events, condition snapshots, joined-check coverage, excluded inputs and observed
execution paths. Retain the launch log before the next run of that mode because
the existing launcher overwrites its mode-specific launch log. Overflowed captures
are invalid; revise the protocol uniformly before restarting a comparable series.
Never infer GPU composition merely from a requested mode: reused child images
may legitimately bypass it. Report per-stroke statistics and process variation;
this manual-input protocol is observational, not identical input replay or
physical input-to-pixel measurement. README speed claims remain unchanged.

## Run 1 results

Selected installed hashes still match the frozen build. Both requested GPU flags
are 0. All 16 ended strokes have the expected four groups, exact sizes and preset
hash; all 355 accepted inputs have membership. The 302 request-producing inputs,
82 input-linked batches and 734 uploads pass joined geometry/timing checks;
53 inputs without dab requests are excluded, with zero dropped events.
Every included input has CPU brush and CPU-pixel upload evidence, with no missing
stage identities. Projection reuses child/original images and skips composition;
Wash additionally records mask processing. Thus this is not a multilayer stack
composition workload, irrespective of the white appearance of the background.
Thirteen unlinked batches and one notification without upload remain outside
the verified population. User-reported closure follows the supplied 16-stroke
protocol and recorded conditions/order match; warm-up IDs are explicitly listed
below. After warm-up exclusion, 12 strokes / 222 timed inputs remain.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 3354 | 4082, 4716, 5274 | 19, 20, 18 | 26.92, 21.94, 19.56 | 51.93, 42.67, 45.31 |
| 64px Wash | 7074 | 7712, 8334, 8910 | 20, 20, 19 | 18.60, 16.63, 20.63 | 40.63, 41.06, 43.39 |
| 256px Buildup | 14422 | 15066, 15697, 16360 | 18, 16, 16 | 22.66, 22.27, 22.85 | 44.90, 46.32, 47.96 |
| 256px Wash | 17610 | 18360, 19074, 19757 | 20, 19, 17 | 17.78, 18.38, 20.05 | 27.53, 44.70, 46.08 |

Original JSON, launch log and full `summary.json` are archived locally under
`%TEMP%/solstice-paint-baseline-471/run-01-cpu/`. The raw trace SHA-256 is
`10fc2e61c38b38c1f9f2d1a3cc1dea62af83b6024cbe724e307b5de5eee4d7ae`.
The full summary retains warm-up rows; use this explicit selection for comparison.
One process is insufficient for a performance conclusion. Display/smoothing and
unsaved preset context are still not independently verified by the trace.

## Run 2 results

Selected installed hashes match. Requested flags are projection=1, brush=0.
All 16 strokes ended with the expected order, exact sizes and common preset/hash,
RGBA32F 2480x3508 conditions. All 376 accepted inputs have membership; all 297
request-producing inputs, 89 input-linked batches and 692 uploads pass joined
checks. Seventy-nine inputs without dab requests are excluded; zero dropped events.
The four warm-up strokes contain 79 timed inputs, leaving 12 measured strokes /
218 inputs. Warm-up selection follows the supplied protocol and verified groups.

Every included input records CPU brush work and shared-buffer canvas uploads,
with no missing stage identities. Projection reuses child/original images and
skips extra composition; Wash also records mask processing. No compositor
submission is associated with these inputs. Thus the requested `projection`
label must not be reported as executed GPU layer-stack composition in this scene;
the observed difference from run 1 includes the canvas transfer route. Twelve
unlinked batches and one notification without upload remain outside the verified
population. No speed conclusion is drawn from these single-process results.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 3633 | 4365, 5096, 5779 | 22, 21, 18 | 20.58, 19.82, 21.32 | 36.75, 34.02, 39.45 |
| 64px Wash | 7356 | 8061, 8756, 9424 | 20, 19, 18 | 18.79, 18.62, 20.25 | 32.36, 46.73, 50.60 |
| 256px Buildup | 12804 | 13545, 14230, 14870 | 17, 16, 16 | 30.97, 32.79, 34.77 | 56.01, 54.51, 63.37 |
| 256px Wash | 16638 | 17389, 18040, 18648 | 19, 17, 15 | 24.29, 29.13, 23.90 | 54.23, 57.41, 50.14 |

Archive: `%TEMP%/solstice-paint-baseline-471/run-02-projection/` contains the raw
JSON, launch log and full summary (including warm-up). Raw trace SHA-256:
`c857835e192361d8c6e22f08a75e5cd199858abfffa3c4da376e308d9db78082`.

## Run 3 results

Selected installed hashes match. Both requested GPU flags are 1. All 16 strokes
ended in the expected order with exact sizes, common preset/hash and RGBA32F
2480x3508 conditions. All 379 accepted inputs have membership. All 308
request-producing inputs, 89 input-linked batches and 757 uploads pass joined
checks; 71 inputs without dab requests are excluded, with zero dropped events.
Warm-up selection follows the verified four groups and supplied protocol: the
four warm-up strokes contain 80 timed inputs, leaving 12 measured strokes / 228
inputs. No missing stage identities occur in the included population.

All included inputs record successful GPU brush submissions and shared-buffer
uploads. Buildup records child/original reuse and skipped composition; Wash also
records mask processing and compositor submissions. No CPU branch is recorded in
the linked brush jobs. This is evidence for these stages, not proof that all
internal processing is GPU-only. Twelve unlinked batches and one notification
without upload remain outside the verified population.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 4017 | 4617, 5174, 5735 | 20, 19, 20 | 20.10, 17.19, 20.52 | 34.17, 41.41, 40.12 |
| 64px Wash | 6996 | 7645, 8193, 8746 | 21, 21, 18 | 20.33, 19.13, 19.22 | 37.00, 36.30, 44.43 |
| 256px Buildup | 11912 | 12598, 13214, 13788 | 19, 17, 18 | 56.58, 23.35, 25.11 | 78.12, 48.36, 56.58 |
| 256px Wash | 15094 | 15735, 16315, 16931 | 19, 19, 17 | 23.83, 22.34, 35.77 | 55.23, 54.94, 52.39 |

The longer 256px Buildup stroke is retained, not discarded as an outlier; no
cause or speedup is inferred from this single-process round. Round 1 is complete;
round 2 starts with projection, followed by brush and CPU.

Archive: `%TEMP%/solstice-paint-baseline-471/run-03-brush/` contains raw JSON,
launch log and full summary including warm-up. Raw trace SHA-256:
`e51f12992454703c8b51b3b11ab32ed4ff8b7bf3a6121e4e55498f0d64613ff4`.

## Run 4 results

Selected installed hashes match. Requested flags are projection=1, brush=0.
All 16 strokes ended in the expected order with exact sizes and common preset/hash,
RGBA32F 2480x3508 conditions. All 365 accepted inputs have membership. All 299
request-producing inputs, 83 input-linked batches and 681 uploads pass joined
checks; 66 inputs without dab requests are excluded, with zero dropped events.
The four protocol warm-ups contain 79 timed inputs, leaving 12 measured strokes /
220 inputs. No missing stage identities occur in the included population.

Every included input records CPU brush and shared-buffer canvas work. Projection
reuses child/original images and skips extra composition; Wash adds mask processing.
No compositor submission is associated with these inputs, consistent with run 2.
Twelve unlinked batches and one notification without upload remain outside the
verified population. Timing variation is retained without outlier removal.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 3462 | 4143, 4784, 5369 | 21, 19, 17 | 19.06, 18.23, 20.79 | 35.64, 48.40, 44.09 |
| 64px Wash | 6574 | 7207, 7783, 8363 | 20, 19, 16 | 16.65, 20.45, 22.32 | 49.73, 46.71, 47.03 |
| 256px Buildup | 11566 | 12206, 12817, 13469 | 19, 18, 17 | 41.36, 29.15, 38.99 | 66.47, 45.41, 56.05 |
| 256px Wash | 14750 | 15448, 16132, 16740 | 19, 18, 17 | 31.09, 22.57, 29.13 | 57.98, 49.44, 59.12 |

Archive: `%TEMP%/solstice-paint-baseline-471/run-04-projection/` contains raw JSON,
launch log and full summary including warm-up. Raw trace SHA-256:
`8acb5cdb7b83e74643f2f400f7e4e3cdeeff3c2643f5eaa3207d04fea5321c6b`.

## Run 5 results

Selected installed hashes match. Both requested GPU flags are 1. All 16 strokes
ended in the expected order with exact sizes and common preset/hash, RGBA32F
2480x3508 conditions. All 394 accepted inputs have membership. All 317
request-producing inputs, 93 input-linked batches and 739 uploads pass joined
checks; 77 inputs without dab requests are excluded, with zero dropped events.
The four protocol warm-ups contain 81 timed inputs, leaving 12 measured strokes /
236 inputs. No missing stage identities occur in the included population.

All included inputs record successful GPU brush submissions and shared-buffer
uploads. Buildup reuses child/original images and skips extra composition; Wash
also records mask processing and compositor submissions, consistent with run 3.
No CPU branch appears in linked brush jobs. Twelve unlinked batches and one
notification without upload remain outside the verified population. Variations
between strokes are retained; no speed conclusion is drawn before all rounds.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 3139 | 3794, 4468, 5084 | 24, 22, 18 | 18.16, 19.05, 22.00 | 37.63, 34.35, 38.73 |
| 64px Wash | 6317 | 7029, 7736, 8375 | 22, 21, 19 | 19.36, 21.34, 19.73 | 31.69, 39.33, 44.00 |
| 256px Buildup | 11572 | 12178, 12773, 13387 | 19, 17, 18 | 19.55, 21.95, 35.58 | 47.86, 47.69, 57.64 |
| 256px Wash | 14511 | 15192, 15808, 16406 | 20, 17, 19 | 22.92, 24.20, 29.53 | 43.37, 45.72, 56.41 |

Archive: `%TEMP%/solstice-paint-baseline-471/run-05-brush/` contains raw JSON,
launch log and full summary including warm-up. Raw trace SHA-256:
`73f51afea897ac73a5612e3bd7c7185698eddc2dd73ae2698dd35a657849d9e0`.

## Run 6 results

Selected installed hashes match. Both requested GPU flags are 0. All 16 strokes
ended in the expected order with exact sizes and common preset/hash, RGBA32F
2480x3508 conditions. All 384 accepted inputs have membership. All 310
request-producing inputs, 86 input-linked batches and 701 uploads pass joined
checks; 74 inputs without dab requests are excluded, with zero dropped events.
The four protocol warm-ups contain 78 timed inputs, leaving 12 measured strokes /
232 inputs. No missing stage identities occur in the included population.

Every included input records CPU brush work and CPU-pixel uploads. Projection
reuses child/original images and skips extra composition; Wash also records mask
processing, consistent with run 1. Twelve unlinked batches and one notification
without upload remain outside the verified population. Round 2 is complete;
round 3 starts with brush, followed by CPU and projection.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 2756 | 3440, 4120, 4717 | 21, 20, 20 | 16.21, 19.91, 21.92 | 30.70, 35.23, 44.44 |
| 64px Wash | 6096 | 6730, 7337, 7885 | 20, 20, 20 | 20.92, 18.75, 20.75 | 35.73, 39.74, 44.56 |
| 256px Buildup | 11081 | 11819, 12453, 13098 | 19, 20, 18 | 22.56, 21.65, 19.43 | 47.05, 43.05, 41.51 |
| 256px Wash | 14386 | 15040, 15696, 16321 | 18, 19, 17 | 20.87, 21.00, 22.86 | 41.40, 42.69, 51.16 |

Archive: `%TEMP%/solstice-paint-baseline-471/run-06-cpu/` contains raw JSON,
launch log and full summary including warm-up. Raw trace SHA-256:
`0fc24eedbe1263c31bf6410c5b97c1233c5c5e484fa254246a717bc0fe2dabe5`.

## Run 7 results

Selected installed hashes match. Both requested GPU flags are 1. All 16 strokes
ended in the expected order with exact sizes and common preset/hash, RGBA32F
2480x3508 conditions. All 368 accepted inputs have membership. All 310
request-producing inputs, 87 input-linked batches and 750 uploads pass joined
checks; 58 inputs without dab requests are excluded, with zero dropped events.
The four protocol warm-ups contain 79 timed inputs, leaving 12 measured strokes /
231 inputs. No missing stage identities occur in the included population.

All included inputs record successful GPU brush submissions and shared-buffer
uploads. Buildup reuses child/original images and skips extra composition; Wash
also records mask processing and compositor submissions, consistent with runs
3 and 5. No CPU branch appears in linked brush jobs. Twelve unlinked batches and
one notification without upload remain outside the verified population. All three
brush-requested processes are now captured; CPU and projection each need their
third process before the complete comparison.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 3088 | 3795, 4438, 5019 | 21, 20, 17 | 19.77, 17.69, 21.78 | 40.63, 30.91, 48.73 |
| 64px Wash | 6858 | 7509, 8116, 8671 | 19, 19, 19 | 17.72, 22.91, 19.95 | 37.47, 50.01, 48.63 |
| 256px Buildup | 11724 | 12329, 12993, 13608 | 20, 20, 20 | 30.42, 29.28, 29.01 | 49.45, 55.55, 41.92 |
| 256px Wash | 14898 | 15489, 16051, 17440 | 19, 18, 19 | 23.46, 25.16, 33.76 | 49.42, 49.06, 56.66 |

Archive: `%TEMP%/solstice-paint-baseline-471/run-07-brush/` contains raw JSON,
launch log and full summary including warm-up. Raw trace SHA-256:
`283932d8aaf30a0781ba379f1276eb77a0bed171c52467f527b7f4bca0ccf9f5`.

## Run 8 results

Selected installed hashes match. Both requested GPU flags are 0. All 16 strokes
ended in the expected order with exact sizes and common preset/hash, RGBA32F
2480x3508 conditions. All 436 accepted inputs have membership. All 338
request-producing inputs, 105 input-linked batches and 941 uploads pass joined
checks; 98 inputs without dab requests are excluded, with zero dropped events.
The four protocol warm-ups contain 83 timed inputs, leaving 12 measured strokes /
255 inputs. No missing stage identities occur in the included population.

Every included input records CPU brush work and CPU-pixel uploads. Projection
reuses child/original images and skips extra composition; Wash adds mask
processing, consistent with runs 1 and 6. Twelve unlinked batches and one
notification without upload remain outside the verified population. All three
CPU processes are captured; projection still needs its third process. Different
input counts and timing variation are retained without outlier removal.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 2659 | 3419, 4228, 4890 | 27, 22, 20 | 17.52, 19.43, 21.39 | 34.15, 34.05, 36.12 |
| 64px Wash | 6196 | 6880, 7517, 8127 | 20, 20, 19 | 24.26, 19.41, 17.77 | 44.87, 38.56, 25.42 |
| 256px Buildup | 11408 | 12297, 13191, 14028 | 22, 20, 20 | 22.80, 18.52, 21.35 | 36.97, 33.69, 36.45 |
| 256px Wash | 15462 | 16379, 17313, 18161 | 23, 22, 20 | 16.92, 17.48, 17.60 | 28.14, 31.86, 27.77 |

Archive: `%TEMP%/solstice-paint-baseline-471/run-08-cpu/` contains raw JSON,
launch log and full summary including warm-up. Raw trace SHA-256:
`06ae7ced34c383d597f3bf32f13c93b8f9949620e639be8dfc9ac0bc9879a4e8`.

## Run 9 results

Selected installed hashes match. Requested flags are projection=1, brush=0.
All 16 strokes ended in the expected order with exact sizes and common preset/hash,
RGBA32F 2480x3508 conditions. All 425 accepted inputs have membership. All 318
request-producing inputs, 100 input-linked batches and 790 uploads pass joined
checks; 107 inputs without dab requests are excluded, with zero dropped events.
The four protocol warm-ups contain 82 timed inputs, leaving 12 measured strokes /
236 inputs. No missing stage identities occur in the included population.

Every included input records CPU brush work and shared-buffer uploads. Projection
reuses child/original images and skips extra composition; Wash adds mask processing.
No compositor submission is associated with these inputs, consistent with runs
2 and 4. Fifteen unlinked batches and one notification without upload remain
outside the verified population.

| Condition | Warm-up ID | Measured IDs (in order) | Inputs per stroke | Median ms per stroke | p95 ms per stroke |
| --- | --- | --- | --- | --- | --- |
| 64px Buildup | 2725 | 3415, 4085, 4699 | 21, 21, 21 | 18.00, 18.69, 17.83 | 27.77, 29.73, 26.83 |
| 64px Wash | 6102 | 6773, 7382, 7924 | 20, 17, 18 | 17.98, 18.31, 17.04 | 33.58, 38.38, 32.07 |
| 256px Buildup | 12542 | 13335, 14157, 14978 | 20, 20, 19 | 26.53, 35.66, 26.92 | 45.73, 54.06, 53.22 |
| 256px Wash | 16421 | 17211, 17991, 18701 | 20, 21, 18 | 30.56, 23.50, 26.85 | 59.91, 40.80, 50.33 |

Archive: `%TEMP%/solstice-paint-baseline-471/run-09-projection/` contains raw JSON,
launch log and full summary including warm-up. Raw trace SHA-256:
`410fd41fa9f3272ed313553fdbcc7f72ee1d8ee72c7fb38292b5eff0abe3c117`.

## Completed comparison (October 6, 2026)

Nine fresh processes, three per requested mode; 144 total strokes, 36 warm-ups
excluded, leaving 108 measured strokes / 2,078 timed inputs. Including warm-ups,
all 2,799 request-producing inputs passed the joined checks and 683 inputs without
dab requests were excluded (3,482 accepted inputs total). All 6,785 upload
occurrences passed coverage checks. No capture overflowed. Background/unlinked
work is not included or retroactively certified.

Aggregation: for each condition in each process, take the median of its three
measured stroke medians. For each mode/condition, report the median and min/max
of those three process summaries. Apply the same hierarchy to stroke p95 values
for a separate tail summary; that value is **not** the p95 of all pooled inputs.
Each mode/condition has nine measured strokes. No outliers are removed. Full
precision process summaries and aggregates are in the local archive's
`comparison.json`; per-stroke source values are in each `summary.json` and the
explicit selections above. Do not recompute from the rounded tables.

| Condition | CPU median (process range), ms | Projection-requested median (range), ms | Brush-requested median (range), ms |
| --- | --- | --- | --- |
| 64px Buildup | 19.91 (19.43–21.94) | 19.06 (18.00–20.58) | 19.77 (19.05–20.10) |
| 64px Wash | 19.41 (18.60–20.75) | 18.79 (17.98–20.45) | 19.73 (19.22–19.95) |
| 256px Buildup | 21.65 (21.35–22.66) | 32.79 (26.92–38.99) | 25.11 (21.95–29.28) |
| 256px Wash | 18.38 (17.48–21.00) | 26.85 (24.29–29.13) | 24.20 (23.83–25.16) |

| Condition | CPU inputs / p95 summary ms | Projection inputs / p95 summary ms | Brush inputs / p95 summary ms |
| --- | --- | --- | --- |
| 64px Buildup | 187 / 35.23 | 181 / 36.75 | 181 / 40.12 |
| 64px Wash | 178 / 39.74 | 167 / 46.73 | 179 / 39.33 |
| 256px Buildup | 169 / 43.05 | 162 / 56.01 | 168 / 49.45 |
| 256px Wash | 175 / 42.69 | 164 / 54.23 | 167 / 49.42 |

Interpretation: no clear 64px GPU advantage; process ranges overlap. At 256px,
CPU has lower summary medians. Brush-requested Buildup overlaps CPU in process
ranges, so do not claim uniform slowdown in every run. Both shared-buffer modes
have higher 256px medians; this makes canvas preparation/transfer/synchronization
a priority-2 investigation candidate, not an established cause. CPU dab work,
scheduling, dirty areas, frame timing and hand-input differences remain possible
contributors. GPU brush submissions are confirmed, so unsupported-brush fallback
does not explain the linked brush samples.

Scope: the scene reuses child images, so this is not a GPU multilayer-composition
benchmark. The projection-requested mode actually shows CPU brush work plus
shared-buffer uploads, without layer-compositor submissions. Brush-requested
Wash includes compositor submissions within its processing. The endpoint is the
last required Qt command-swap acknowledgment, not physical scanout/pixel survival.
Inputs within shared batches/frames are correlated. Only three processes per mode
were observed; input motion/pressure and unsaved settings were not replayed or
fully snapshotted, and refresh rate/zoom/smoothing were not independently recorded.
The trace is enabled throughout; its GUI overhead has no calibrated correction.
Treat this as the completed manual software-timing baseline with these limitations,
not fulfillment of the stronger physical input-to-pixel acceptance criterion.
No further repetitions of this series are requested; next work is overhead analysis.
