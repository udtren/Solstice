---
type: history
topic: gpu-engine
status: archived record (moved from docs/agent/gpu-engine.md on 2026-10-07)
sources: [docs/agent/gpu-engine.md, git history]
---

# GPU engine history: paint trace and latency analysis (phases 4.58-4.81)

Paint pipeline trace, capture protocol, real-app stage and lock analysis, shared canvas builds. Moved verbatim from `docs/agent/gpu-engine.md`; current state, decisions and
invariants stay there ([gpu-engine.md](../../gpu-engine.md)). Each section records what was true
when it was written; later sections and the current document take precedence.

## Paint pipeline trace foundation (phase 4.58)

Started October 5, 2026, as priority 1 of `gpu-work-priorities.md`.
This is measurement infrastructure only. Priority 1 is not complete:
per-event pixel/frame attribution and the 64px/256px Buildup/Wash three-path
baseline still need implementation and real-application capture. Do not move
on to dab generation or more blend modes on the strength of these traces.

`KisPaintTrace` is enabled only when `KRITA_PAINT_TRACE` contains an output
filename prefix. Normal process teardown writes `<prefix>.<pid>.json` using
QSaveFile. A bounded 262,144-event buffer uses one steady-clock timebase across
threads. Overflow is counted in metadata and makes a capture unusable for
comparisons. No file I/O, GPU fences, queue changes, or configuration writes
are added to the measured path. Disabled hooks return without acquiring the
recording lock, reading the clock, or allocating a recorder. Enabled recording
does take a mutex; its overhead must be measured before drawing conclusions.
Do not call `flush()` during a timed capture: export snapshots allocate and
perform I/O. Abnormal termination may lose the buffered trace.

Captured boundaries:

| Event | Meaning |
| --- | --- |
| `input.mouse_*`, `input.tablet_*` | Arrival at the canvas input manager, before its filtering/compression; includes hover and rejected events |
| `tool.begin/move/end` | Freehand helper dispatch on the GUI thread |
| `stroke.enqueue`, `stroke.end_requested` | Image scheduler submission requests |
| `stroke.run`, `stroke.dirty` | Freehand callback and dirty-notification CPU spans |
| `dab.generate_and_postprocess` | Executed CPU dab generation/postprocessing; cache hits need not run this span |
| `brush.cpu_composite`, `brush.gpu_attempt_and_fallback` | CPU painting job or GPU-path attempt including possible CPU fallback, not GPU execution time |
| `projection.merge` | Merger CPU span; Vulkan work may outlive it |
| `canvas.prepare`, `canvas.upload` | Canvas data preparation and compressed update upload spans |
| `canvas.paint`, `canvas.frame_swapped` | OpenGL widget paint and Qt frameSwapped callback; not physical scanout |

Each record includes thread ID and opaque `owner`/`related` identities.
Enqueue/run share a job-data identity, but pointer identities can be reused
after destruction and are not persistent event IDs. Canvas records use their
canvas/widget identities. Parallel or nested spans cannot be added to obtain
elapsed latency. Critically, the next swap after an input may still show old
pixels; the summary deliberately does not report that difference as latency.
Future event lineage must survive input compression, asynchronous dab batches,
dirty-rectangle/projection compression and update-info upload before a frame
can be attributed to that input. Multiple documents/views and canceled/no-op
inputs must be accounted for. QPainter/QtQuick presentation is not covered.

User-invoked capture (agent must not start or stop the real application):

```bat
build-tools\paint-trace\run.cmd <krita-dev-root> brush
```

The second argument is `cpu`, `projection` or `brush` (default). It sets only
process-local overrides: projection/brush 0/0, 1/0 or 1/1, disables Vulkan
validation for timing, and prepares the same dependency/Python environment
as the working debug launcher. An existing Krita/Solstice process blocks
launch, avoiding forwarding to a process with different flags. `--check` as
the third argument validates setup without starting an application. Output
goes to `%TEMP%/solstice-paint-trace-<mode>.<pid>.json`; startup stderr is in
the adjacent `.launch.log`. Flags in metadata describe requested paths, not
proof of successful GPU execution. Never change `kritarc` for the comparison.

For initial capture validation, open a single RGBA32F document, use a plain
pixel brush without smoothing/texture/masks, draw a few short strokes with
pauses, and close normally. First establish that input, job and frame events
are all present. This smoke capture is not the performance baseline.

```bat
python build-tools/paint-trace/summarize.py <trace.json>
python -B -m unittest discover -s build-tools/paint-trace -p test_summarize.py
```

The summary validates schema/units and rejects overflow, then reports counts
and median/p95 CPU span durations per event name. It does not sum overlapping
spans, infer pixel causality, or report a drawing speedup. The raw format uses
Chrome trace-event JSON for timeline inspection. Tests cover disabled mode in
a fresh process, four concurrent writers, export, and bounded overflow;
timing values are not pass/fail performance thresholds.

After lineage is validated, the planned baseline remains 64px/256px ×
Buildup/Wash × all three paths, with warmup, at least three fresh processes,
explicit sample counts and fixed document/preset/view conditions. Include
tracing-disabled comparisons to quantify measurement overhead. Re-run the
README operation workloads at that point; do not relabel old numbers as new.

Validation on October 5, 2026: `KisPaintTraceTest` passed 5/5 including
init/cleanup; Python summary tests passed 3/3. The four ordinary 64px/256px
Buildup/Wash rows of `KisGpuStrokeTest` passed with tracing enabled and again
disabled (6/6 each including init/cleanup). Each row compares all three
painting paths and verifies pixels, Undo/Redo and Vulkan validation errors.
These single-sample regression timings are not a performance baseline.
The enabled run exported a parseable trace with no dropped events; as this
fixture has no UI, it cannot verify input or frame-swap hooks. That remains
a real-app smoke check at that stage. Both launcher environment checks passed without
starting Solstice. A local convenience launcher is
`%TEMP%/solstice-paint-trace.bat` (brush path).

The rebuilt version/image/brush/UI/paintop libraries and default paintop
plugin were installed with the application closed; all six build/install
SHA256 hashes matched. Logs: `%TEMP%/solstice-paint-trace-build.log`,
`solstice-paint-trace-test.txt`, `solstice-paint-trace-strokes-{enabled,disabled}.txt`
and `solstice-paint-trace-install.log`. No application preferences changed.

## Paint trace identity and frame coverage (phase 4.59)

The first real-app capture passed: `%TEMP%/solstice-paint-trace-brush.39868.json`
contains seven freehand begin/end pairs, 697 moves, 726 projection spans,
397 upload spans and 1,485 Qt frame swaps, with zero dropped events.
Both tablet and mouse input streams are present. Counting their combined
arrivals as brush samples, or pairing each with the next swap, is invalid.
This is a smoke result, not a latency measurement or a three-path baseline.

Schema 2 adds process-unique numeric IDs serialized as strings. Dispatch
scopes in the input manager expose a thread-local input identity to the
freehand tool. Nested non-input events clear it temporarily; nested pointer
events replace it and restore the outer identity on return. Worker threads
cannot inherit it. `input.accepted_*` means synchronous freehand dispatch,
not a promise that the event creates a dab. Delayed/compressed input dispatched
outside the original scope is deliberately unattributed. Propagating lineage
through compression, brush jobs, dirty notifications and projection is still
required before reporting input-to-pixel latency.

Each `KisUpdateInfo` has an ID (zero with tracing off). `update.ready` marks
preparation; compression records `update.superseded`, and successful animation
update merging records `update.merged`. Neither means that old pixels survive.
`update.upload_issued` assigns a separate occurrence ID after the nonempty
texture upload loop returns successfully, so reuse of a cached update is not
mistaken for a single upload. Early exits are not marked successful. This
records command issuance, not GPU completion or a GL error check.

The OpenGL widget owns a tracing-only `KisCanvasPaintTrace`. It tracks remaining
render/blit regions in widget coordinates for the visible part of each upload.
Decoration-only paints cannot consume unrendered image regions. A cached image
render can be followed by a later blit; partial coverage remains pending.
`frame.covered_upload` connects a covered occurrence to `frame.submitted`.
`frame.replaced` connects paints occurring before the previous swap; only this
widget's `frameSwapped` signal acknowledges the pending frame. Extra swaps and
uploads arriving between paint and swap cannot acknowledge a future frame.
Resize clears tracking; uploads outside the viewport remain unassociated.
Tracking is bounded to 4,096 pending occurrences; overflow marks the whole
trace incomplete so the summary rejects it.

Coverage is a conservative record of render/blit commands, not proof that an
upload's pixels survived later overwrites or became physically visible. Capture
with a fixed view (no pan/zoom/rotation, resize, animation, wrap-around or LOD
changes); coordinate changes and non-OpenGL canvases are not attribution targets
yet. The summary accepts schemas 1/2, counts only explicit same-widget frame
chains and reports no latency or speedup. Merged/superseded updates remain raw
graph evidence, not extra successful samples. No waits or repaint requests
were added, and the tracking object is absent when tracing is disabled.

Automated coverage: `KisPaintTraceTest` 6/6 (including init/cleanup),
`KisCanvasPaintTraceTest` 5/5, and Python summary tests 4/4. Tests exercise
nested input dispatch, thread isolation, stable ID export, partial render and
delayed blit, late uploads, duplicate swaps, separate widgets, reset and bounded
state. The summary regression rejects implicit nearest-swap attribution and
cross-widget matches. `KisGpuStrokeTest` passed the 64px/256px Buildup/Wash rows
with tracing on and off (6/6 each), and `KisGpuCanvasUploadTest` passed 28/28 with Vulkan
validation enabled. Timing values from these regressions are not benchmarks.

Real-app schema-2 verification passed on October 5, 2026:
`%TEMP%/solstice-paint-trace-brush.19084.json` contains nine freehand strokes,
657 linked freehand dispatches (9 begins, 639 moves, 9 ends), and 772 upload
occurrences. All 772 have render/blit coverage and an explicit same-widget
swapped-frame chain. No events were dropped. There are 1,190 submitted frames
and 1,187 attributed swaps; the 1,206 raw Qt swaps must not be treated as 1,206
new image updates. This confirms coverage bookkeeping in the real app, not
input-to-pixel latency. Priority 1 remains in progress.

The six rebuilt version/image/brush/UI/paintop libraries and default paintop
plugin were installed with Solstice closed; build/install SHA256 hashes match.
Logs use `%TEMP%/solstice-paint-lineage-` with suffixes `build.log`, `test.txt`,
`canvas-test.txt`, `strokes.txt`, `strokes-disabled.txt`, `upload.txt` and
`install.log`. Both GPU regression traces exported schema 2 with no dropped
events; these fixtures do not exercise real freehand input or the canvas widget
frame hooks. No preferences were changed and the application was not launched.

## Scheduled job trace lineage (phase 4.60)

`KisStrokeJob` now assigns a process-unique ID at construction and records
`job.created`, `job.started`, `job.finished` and `job.destroyed`. The creation
parent is the executing job's ID, or the synchronous input-dispatch ID when
there is no executing job. Mutation-generated jobs therefore retain their
creating context even when they run later on a different worker. Queued jobs
destroyed without execution remain distinct from completed jobs. IDs belong
to the wrapper, not the reusable data pointer or copied LOD data.

`KisPaintTrace::JobScope` sets/restores a thread-local job identity around the
strategy call. CPU scopes and instant records created there include `args.job`;
`job.run` covers the strategy call including its nested work. No scheduler
ordering, queue ownership, dependency or wait behavior changed. The disabled
path reads neither clocks nor trace locks and allocates no recorder; the wrapper
adds one ID field and cheap disabled checks. Enabled-trace overhead is still
unquantified and must be measured before publishing a performance baseline.

The summary reports creation-to-start intervals for explicitly matching job IDs
(including queue insertion and scheduling), counts unexecuted destroyed jobs,
and counts jobs with a freehand-input ancestor. **Creation ancestry is not a
complete pixel dependency graph:** timer-generated work may have no input parent,
and a batch can consume dabs from several earlier inputs. Never label all batch
pixels as belonging to the context that created its flush job. Delayed input,
dab-batch input sets, dirty rectangles and compressed projection requests still
need attribution. No input-to-display latency is reported yet.

Validation: `KisPaintTraceTest` 7/7, including actual `KisStrokeJob` execution on
separate threads, child creation, context restoration, canceled/unrun jobs and
CPU-span job tags; existing `kis_stroke_test` 8/8 and `kis_strokes_queue_test`
21/21 (cancellation, LOD, mutated jobs and concurrent/barrier ordering). Python
summary tests pass 6/6, including repeated execution/cyclic graph rejection.
The 64px/256px Buildup/Wash rows of `KisGpuStrokeTest` pass with tracing on and
off, 6/6 each, with Vulkan validation enabled. The enabled regression trace
contains 3,044 created/executed jobs and 8,116 job-tagged spans, no dropped
events; it has no real UI input, as expected. These are correctness checks,
not benchmark samples. Logs: `%TEMP%/solstice-paint-jobs-{build.log,test.txt,
stroke-test.txt,queue-test.txt,enabled.txt,disabled.txt}`.
The six rebuilt libraries/plugin were installed with the app closed; all
build/install SHA256 hashes match (`solstice-paint-jobs-install.log`). No
preferences were changed and the real application was not started by the agent.

For the next real-app capture use `%TEMP%/solstice-paint-trace.bat`, a fixed
RGBA32F view and plain pixel brush. Draw short 64px/256px Buildup/Wash strokes
with pauses, then close normally. Verify job ancestry/intervals and frame
coverage before requesting the three-path repeated performance baseline.

Real-app job capture passed: `%TEMP%/solstice-paint-trace-brush.15916.json`
records 14 strokes, 1,005 linked freehand dispatches, 25,025 created/executed/
finished/destroyed jobs and 71,276 job-tagged CPU spans. All 2,551 upload
occurrences reach an explicit swapped-frame chain; nine prepared updates were
superseded before upload. There are 208,922 events and none were dropped.
Only 8,018 jobs have a freehand-input creation ancestor. This is not evidence
that the remaining jobs are unrelated to drawing: timer-driven batch flushes
need explicit source membership, motivating phase 4.61. Aggregate scheduling
intervals include all job types in this single process, not a controlled
CPU/GPU performance comparison or input-to-pixel latency.

## Logical dab and paint-batch trace membership (phase 4.61)

The pixel-brush rendering queue assigns an ID to each accepted logical request,
including cached copies and postprocessed dabs. `dab.request`,
`dab.cache_request` and `dab.postprocess_request` record its creating job/input
context. Copies of the internal rendering-job value preserve the request ID;
separate queue requests always receive new IDs even when they share pixels.
Generation spans include the logical request ID and executing job ID.

The asynchronous brush update allocates a batch ID and passes it through
`KisDabRenderingExecutor::takeReadyDabs` to the rendering queue. `dab.in_batch`
is recorded only when a completed request is actually returned, after count/
byte-limit checks. Retained cache entries, requests not ready yet and already
consumed requests do not create extra membership records. This keeps input
sources distinct when one timer-driven flush consumes multiple earlier inputs.
`batch.ready` records the prepared group; `batch.paint_job` associates scheduled
GPU-attempt/CPU painting jobs; `batch.dirty_recorded` marks the final sequential
callback recording the painter's dirty rectangles. Mirroring/wrapping can reuse
the group; these records do not claim that every member affects every rectangle
or that a skipped mirrored job wrote pixels. No `KisRenderedDab` ABI change,
queue ordering change, new waits or scheduling changes were needed.

The summary reports request kinds, explicit memberships, generation coverage,
batch counts and batches with multiple traced source inputs. It rejects duplicate
request IDs and repeated consumption records. It deliberately does not use the
timer job's creation ancestor as the only source of a batch. Source ancestry is
still a creation relationship; interpolation can depend on earlier input samples.
Dirty recording is not GPU completion, projection completion or presentation.
The remaining priority-1 work includes dirty/projection compression lineage,
delayed input handling and controlled repeated CPU/projection/brush measurements.

Validation: `KisPaintTraceTest` 7/7, `KisDabRenderingQueueTest` 13/13 and Python
summary tests 7/7. The new queue test covers two input contexts, ordinary/cached/
postprocessed requests, pending dependencies, count-limited splitting, empty
fetches and copied IDs; existing byte-budget and mutable-dab tests also pass.
The 64px/256px Buildup/Wash `KisGpuStrokeTest` rows pass with tracing enabled and
disabled (6/6 each) and Vulkan validation. The enabled trace
`solstice-paint-batches-regression.31100.json` contains 1,836 logical requests,
all with generation spans and batch membership, 28 prepared/dirty-recorded
batches and 284 paint-job links, with no dropped events. The no-UI fixture has
no freehand-input source IDs, as expected. These are correctness checks, not
performance samples.

The first GPU test launch loaded an old installed default-paintop DLL against
the new trace API and failed plugin initialization. Installing the rebuilt
library/plugin set resolved it; both reruns passed. Logs:
`%TEMP%/solstice-paint-batches-{build.log,test-build.log,trace-test.txt,
queue-test.txt,enabled.txt,disabled.txt,install.log}`. Real-app batch membership
has not been verified yet. Defer another manual capture until the remaining
projection linkage is ready, instead of repeating the same smoke test after
each instrumentation step. Tracing remains disabled by default; the 262,144
event bound is unchanged, so long enabled captures can be rejected as incomplete.

## Dirty groups and projection lineage (phase 4.62)

Each completed pixel-brush batch records its ID in its `KisPainter` alongside
the existing dirty rectangles. The final sequential batch callback is the only
writer of this diagnostic list. `takeDirtyRegion()` drains both together and
emits `batch.to_dirty` links into the active explicit dirty-dispatch context.
The list is bounded to 4,096 batches; overflow marks the trace incomplete so
summaries reject it. No new locking or waits were added to painter operations.

`FreehandStrokeStrategy::issueSetDirtySignals()` supplies the dirty-group ID
while collecting all masked painters and calling `setDirty`. When masked-brush
updates defer that call to a job, the lambda explicitly captures/restores the
same ID. `KisPaintTrace::FlowScope` is thread-local and restores outer state;
it is separate from job creation ancestry and is not implicitly inherited by
arbitrary jobs or timers. Dirty drains outside a known dispatch remain
unattributed rather than being assigned to a nearby timestamp.

`KisSimpleUpdateQueue` records each projection request and its source context.
Recursive patch splitting retains the parent-request link. A new walker gets
a stable ID; requests merged into an existing walker link to that same ID.
Queue collection/optimization records removed-walker to surviving-walker edges.
Rectangle/checksum recalculation preserves the walker ID. All existing checks
for node, crop, update type, clone invalidation and LOD still determine whether
updates can merge; instrumentation does not change that decision.

The executing merge job installs the walker context around both the merge and
the existing `continueUpdate` notification. The existing direct image-to-canvas
connection carries it synchronously into `update.ready`, which links prepared
canvas updates to that walker. Existing upload/frame links complete the graph.
Deferred/suppressed UI notifications or projection filters that replay requests
outside this context are intentionally unlinked. Do not assume the graph covers
animation, delayed input, every filter or every rendering backend.

The summary follows explicit input/job/dab/batch/dirty/request/walker/update/
upload/frame edges. It checks frame/upload owner identities and reports whether
**at least one command descendant** reaches each stage. Superseded updates and
merged walkers preserve command ancestry; they do not prove that an earlier
input's pixels survive. Counts do not assert that all split regions have been
displayed, and no input-to-pixel latency is published. Stronger per-sample
coverage remains required before the three-path baseline. The trace's extra
overhead must also be measured.

Validation: `KisPaintTraceTest` 8/8; `kis_simple_update_queue_test` 9/9;
Python summary tests 8/8. Coverage includes nested/thread-isolated flow state,
multiple painter batches drained exactly once, recursive splitting, ordinary
merging, optimize-time merging, checksum recalculation and rejection of a
cross-canvas or missing-link shortcut. The trace test now uses the standard
test resource/plugin paths and QApplication; its first QCoreApplication-based
attempt lacked those paths and was stopped after unnecessary plugin searching.
This was a test-harness issue, fixed before the successful rerun.

The 64px/256px Buildup/Wash GPU stroke rows pass with tracing on/off (6/6 each,
including pixel and Undo/Redo comparisons); canvas upload tests pass 28/28.
Vulkan validation is enabled for these regressions. The enabled stroke trace
`%TEMP%/solstice-paint-projection-regression.40148.json` contains 1,836 requests
in 28 batches: all 28 have projection requests and executed merge descendants,
with zero dropped events. This fixture has no canvas widget or tablet input,
so its input/frame coverage correctly remains zero. The synthetic Python graph
test covers the complete explicit chain, not interactive frame delivery.

Logs use `%TEMP%/solstice-paint-projection-` plus `build.log`, `test-build.log`,
`trace-test.txt`, `queue-test.txt`, `enabled.txt`, `disabled.txt`, `upload.txt`
and `install.log`. The rebuilt libraries/plugin are installed with the app
closed. For the combined real-app check, run the existing launcher and draw
one short stroke each at 64px/256px Buildup/Wash in an RGBA32F document with a
plain pixel brush and fixed viewport, then close normally. Keep the capture
short because the unchanged 262,144-event bound now includes more lineage.
Verify input/batch descendants through projection and swapped frames, no
dropped events and no suspicious cross-document associations before collecting
performance baselines. Priority 1 remains in progress; priorities 2–4 have
not started.

Real-app follow-up (October 5): `%TEMP%/solstice-paint-trace-brush.41716.json`
contains four accepted stroke beginnings/ends and 68 accepted moves, with zero
dropped events. All 21 input-linked batches have projection-request, executed
merge, canvas-update and swapped-command descendants. All 234 upload occurrences
have render/blit coverage and a swapped-frame chain. These are command ancestry
checks, not a guarantee of pixel survival or complete coverage of every input.
Only 64 of the 76 accepted dispatches have a swapped-command descendant; the
remaining dispatches must not be treated as measured latency samples or declared
lost without examining whether they generated paint work. Preset, size and
painting-mode metadata are not recorded, so the requested four conditions cannot
be independently verified from this capture.

There are also 80 batches without input links. Brush stroke previews use their
own RGB8 image and `FreehandStrokeStrategy`, so background preview rendering is
a source-supported explanation for this population, not a proven per-batch
classification. Do not report 80 lost canvas updates or compare process-wide
CPU-span aggregates against GPU brush timings. The offline analyzer now reports
input-linked batches separately at every pipeline stage, including those without
a swapped descendant. Its eight tests pass, including an unrelated same-time
batch, a wrong-canvas swap and a missing projection edge. This analysis-only
change needs no DLL replacement. Priority 1 is still incomplete: per-sample
coverage, workload identification, tracing overhead and the three-path baseline
remain before publishing latency results.

## Input audit and workload conditions (phase 4.63)

`summarize.py` now audits each accepted input through job creation, logical dab
requests and batch membership. Traversal stops at batches, so a shared batch or
merged projection does not assign another input's requests to this input. Each
row reports requests lacking a ready batch and batches lacking any swapped
command descendant. An input with one presented batch and another missing batch
does not pass the all-request check. Inputs without requests are reported by
begin/move/end kind; they are not assigned a zero latency or declared lost.
This is still command ancestry, not coverage of all rectangles within each batch.

Reanalysis of capture 41716: all 64 inputs that created requests have all their
requests in batches with swapped-command descendants. No request from these
inputs lacks batch membership. The other 12 accepted inputs comprise four begins,
four moves and four ends with no recorded request descendants. This resolves the
earlier 64/76 count at the request level; it does not prove those inputs could
not influence later interpolation or delayed drawing.

When tracing is enabled, `KisToolFreehand::initStroke` snapshots the GUI preset
name, existing stored MD5 (without generating one), engine ID, nominal brush
size, incremental flag, image dimensions, color model and depth. The incremental
flag corresponds to Buildup/Wash for the pixel engine. `KisPaintTrace` stores these
under `metadata.stroke_conditions`, keyed by accepted begin input ID and canvas
identity. At most 1,024 snapshots are kept; overflow marks the capture incomplete.
No disk access, preset serialization, configuration change or GPU wait is added
to stroke start. Disabled tracing does not construct the snapshot. An unscoped
begin has no invented input ID and remains without conditions. The analyzer
rejects duplicate or wrong-input/canvas condition records; older captures remain
readable and report missing conditions.

These are GUI settings at stroke start, not pressure-adjusted dab sizes or proof
of GPU execution. Stored MD5 does not identify unsaved preset edits. Conditions
are not yet propagated as a stroke ID onto every later input/job; do not assign
them to later samples merely by nearest timestamp. Selection, mirrors, smoothing,
view transform and other workload conditions still need control in the manual
protocol. Automated coverage does not verify this GUI hook in an actual stroke.

Validation: `KisPaintTraceTest` 9/9 (including snapshot immutability, zero-input
suppression, bounded storage and disabled-process behavior); Python tests 10/10
(including partial batches, unbatched requests, mixed input sources and condition
identity mismatches). The 64px/256px Buildup/Wash stroke rows pass with tracing
enabled and disabled, 6/6 each, with Vulkan validation. Build and installation
completed; hashes of all six installed library/plugin files match build outputs.
Logs use `%TEMP%/solstice-paint-conditions-` followed by `build.log`, `test.txt`,
`enabled.txt`, `disabled.txt` and `install.log`. The first compile found a const
smart-pointer access to the non-const `paintIncremental()` API; the local handle
was corrected before the successful build. No painting algorithm changed.

Tracing-cost check on October 5, same Windows/Blackwell development environment:
`KisGpuStrokeTest` with `KRITA_GPU_STROKE_REPEATS=3`, validation disabled. Three
fresh processes per tracing state, ordered off/on, on/off, off/on. Each process
warms up each workload/path, then records three samples; the table is the median
of the three process medians in milliseconds. Rows test 1024x1024, four RGBA32F
layers, four image workers, no canvas/input dispatch, and untimed verification
readback. Pixel and Undo/Redo checks passed in all six processes.

| Workload | CPU off / on | GPU projection off / on | GPU projection + brush off / on |
| --- | ---: | ---: | ---: |
| 64px Buildup | 4.781 / 4.760 | 8.014 / 8.179 | 8.785 / 9.241 |
| 64px Wash | 5.416 / 5.437 | 8.846 / 8.724 | 9.297 / 9.563 |
| 256px Buildup | 7.792 / 7.938 | 11.249 / 11.097 | 10.023 / 10.723 |
| 256px Wash | 10.113 / 10.772 | 13.770 / 13.905 | 15.652 / 14.753 |

Raw results: `%TEMP%/solstice-trace-overhead-{1,2,3}-{off,on}.txt`. Enabled traces
have PIDs 7216, 40672 and 42292, each with 51,992 events and no drops. Across rows,
median differences range from about -5.7% to +7.0%; several ranges overlap, and
the 256px GPU-brush Wash row is faster with tracing in this run. This is evidence
that run variation matters, not evidence that tracing improves performance or
that one correction factor is valid. This fixture does not execute GUI condition
snapshots, pointer dispatch or frame tracing. Do not subtract these numbers from
interactive measurements or replace README's representative benchmarks with them.

Priority 1 remains in progress. Next: complete per-sample split-region coverage,
explicit stroke identity across accepted inputs, and controlled interactive
CPU/projection/brush captures with the new condition records. Defer another
identical manual smoke test until that capture protocol is ready. Priorities 2–4
remain deferred in accordance with `gpu-work-priorities.md`.

## Recorded branch completeness (phase 4.64)

The offline pipeline summary now includes `recorded_branch_audit`. Starting at
input-linked batches, every recorded dirty/request/walker/update/upload branch
must reach a swapped frame; one successful sibling no longer hides a pending
split request or second canvas update. Superseded updates and merged walkers
retain their explicit downstream obligations. Update replacement/merge edges
are accepted only between known, matching canvas owners. Shared downstream
work can conservatively keep several inputs unresolved.

An iterative reverse-topological pass evaluates the graph in linear time and
rejects cycles, including cycles with a swapped frame. It avoids recursion-limit
failures on long chains. Unresolved leaf counts identify the stage where links
stop: missing dirty dispatch, projection child/walker, canvas update, upload,
frame coverage or swap. The report includes up to 16 terminal IDs for inspection.
These counts describe recorded paths, not proof of lost drawing: offscreen work,
resets, suppressed notifications and missing instrumentation remain unresolved.

The request audit's earlier `all_requested_dabs_have_swapped_batch_command`
field remains available and keeps its weaker meaning (some descendant per
batch). The new `all_requested_dabs_have_all_recorded_branches_swapped` field
also requires every recorded downstream branch. Neither field certifies
rectangular coverage: dirty/projection coordinates are not recorded, so an
unrecorded split or a rectangle mismatch cannot be detected by this graph.
Do not publish input-to-pixel latency from either field.

Validation: Python tests 12/12, including one missing split sibling, multiple
canvas updates, merged/superseded resolution, cross-canvas replacement rejection,
a cycle and a 3,000-node chain. Reanalysis of real-app capture 41716 passes the
stronger check for all 21 input-linked batches and 64 request-producing inputs,
with zero unresolved terminals. The no-canvas regression capture 40148 correctly
has no input-linked population; zero unresolved rows there is not a presentation
pass. This change is offline analysis/documentation only: no DLL rebuild or
new interactive capture is needed. Next remains geometric coverage and explicit
stroke identity, followed by the controlled interactive comparison.

## Image-space rectangle coverage (phase 4.65)

`KisPaintTrace::rectangle` records signed x/y/width/height and explicit LOD in
the existing bounded event buffer. JSON rectangle construction occurs at export;
the active recording path stores a `QRect` value. A negative LOD marks the trace
incomplete rather than inventing a coordinate scale. Existing schema-2 captures
remain readable. The extra rectangle/LOD fields enlarge in-memory event records
when tracing is enabled; phase-4.63 tracing-cost numbers are not a calibration
for this version. The 262,144-event limit and opt-in behavior remain unchanged.

Hooks:

- `KisSimpleUpdateQueue::addJob`: `projection.request_rect` for every requested
  rectangle, including empty split pieces, in the request's image/LOD space.
- `KisUpdateJobItem::runMergeJob`: after the merger returns, record the walker's
  final `requestedRect()` and `changeRect()` as `projection.executed_request_rect`
  and `projection.change_rect`, with its node owner and LOD. This includes the
  effects of queue merging/recalculation; it does not certify individual writes.
- `KisCanvas2::startUpdateCanvasProjection`: `update.request_rect` records the
  notification's image rectangle. `KisImage::notifyProjectionUpdated` has already
  upscaled it to LOD 0. The canvas identity stays attached to each update.

`build-tools/paint-trace/geometry.py` follows explicit request splitting and
walker merging, then checks whether the union of executed requested rectangles
covers each requested rectangle. Bounding boxes are insufficient: subtraction
preserves holes and uses half-open boundaries derived from x/y/width/height.
It separately checks a walker's change rectangle against canvas notification
rectangles **per canvas**, never by combining areas from different views.
Only LOD 0 is evaluated. Missing coordinate records, unknown/mismatched owners,
other LODs and excessive fragmentation are reported as unverified. Rectangle
lists are capped at 1,024 for analysis and subtraction at 100,000 piece checks;
exceeding those budgets does not become a successful coverage result.

These are declared image-space regions, not pixel survival or whole-pipeline
display coverage. A walker's requested region does not prove each pixel was
modified. Canvas notification coverage does not establish which tiles were
uploaded or which viewport pixels reached a swap. Dirty-group geometry, tile
transfer coverage, image-to-widget mapping and view changes still require work
before an input-to-pixel baseline. Old captures without coordinates correctly
produce unverified results, rather than being retroactively certified.

Validation: `KisPaintTraceTest` 10/10, including signed/empty rectangles and LOD
serialization; `kis_simple_update_queue_test` 9/9 with tracing enabled. The first
queue run had tracing unset and skipped the trace-only row; rerunning with the
flag passed all rows. Python tests 17/17 include holes, touching edges, negative
coordinates, overlapping unions, per-view isolation, wrong owner/LOD, missing
coordinates, bounded analysis and 100 deterministic comparisons against an
independent small-pixel-set oracle. Run all analysis tests with:

```bat
python -B -m unittest discover -s build-tools/paint-trace -p test_*.py
```

The four 64px/256px Buildup/Wash GPU stroke rows pass with tracing on/off and
Vulkan validation (6/6 each, including initialization/cleanup). Trace
`%TEMP%/solstice-paint-regions-regression.2684.json` has zero dropped events;
all 100 projection requests pass rectangle containment. Its 293 executed walkers
have no canvas links because the fixture has no window; that stage is explicitly
unverified, not passed. Build/install completed and all six installed DLL/plugin
hashes match the build. Logs use `%TEMP%/solstice-paint-regions-` plus `build.log`,
`test.txt`, `queue.txt`, `enabled.txt`, `disabled.txt` and `install.log`.

Actual GUI condition/rectangle capture remains pending. Combine it with the
remaining display-geometry work rather than requesting another identical smoke
test at this intermediate stage. No application launch, termination or preference
change was performed. Priority 1 remains in progress.

## Upload and widget geometry (phase 4.66)

For completed `updateCanvasProjection` calls with `paintTraceUploadIssued`,
`KisOpenGLCanvas2` records the dirty image rectangle clipped to current image
bounds, every tile's `realPatchRect()` clipped to those bounds, the corresponding
logical-widget rectangles and the actual dirty widget rectangle passed to the
existing render/blit coverage tracker. Geometry is keyed by the unique upload
occurrence ID, not a reusable buffer/pointer or update ID. The patch list is read
only after the renderer has issued all upload commands; it is command geometry,
not a new GPU completion check.

Supported mapping is invertible affine scaling/translation with zero cross-axis
terms, including negative scale (mirror display), LOD 0 and no wrap-around view.
Use `QRectF` mapping followed by `toAlignedRect()` and widget clipping, matching
the renderer's logical-pixel coordinate convention. Image expectations are
clipped before mapping, while the tracked dirty rectangle includes the renderer's
existing two-pixel growth. The analyzer checks that this tracked rectangle covers
the visible expectation; it does not demand that image patches cover that border.
Rotated/sheared/projective views, wrap-around and nonzero LOD record
`update.geometry_unsupported`; they are not accepted as verified geometry.

`KisCanvasPaintTrace::setView` compares the transform, widget rectangle, device
pixel ratio and wrap mode before upload tracking and before painting. Changes
clear pending upload/frame tracking and emit `frame.reset`. This is conservative:
it prevents acknowledgments against stale coordinates and can leave pending work
unattributed, but does not alter actual painting, upload or widget scheduling.
The tracing object and these checks exist only with opt-in tracing enabled.

The offline `geometry.transfers` report combines three containment checks
(image patch union, mapped widget patch union, tracked render/blit rectangle)
with same-widget `frame.covered_upload` and swapped/replaced frame links.
It rejects acknowledgments preceding the upload and treats an intervening view
reset as unverified. Successful status is `covered_to_swapped_commands`, not
pixel visibility or physical scanout. `outside_image`/`outside_view` are exclusions,
not successful presentation. Missing metadata, patches, unsupported mapping,
nonzero LOD, uncovered regions and absent swaps have separate statuses.

Validation: mapping/coverage tests 6/6, including negative coordinates, clipping,
mirror display, offscreen regions, rejected rotation/wrap/singular transforms,
pan/scale/DPR/viewport/wrap invalidation, delayed blits and partial renders.
Vulkan-validation-enabled `KisGpuCanvasUploadTest` passes 28/28. Analysis tests
19/19 include image/widget/tracking holes, wrong-widget swaps, replacement
chains, stale acknowledgments, resets, offscreen exclusions and old metadata.
The real OpenGL widget integration still requires the combined manual capture.
Build and install completed; logs use `%TEMP%/solstice-paint-display-` followed by
`build.log`, `trace-test.txt`, `upload.txt` and `install.log`.

Combined real-app check: run `%TEMP%/solstice-paint-trace.bat`, create RGBA32F,
use a plain pixel brush without texture/masked tip, keep zoom/pan/rotation fixed,
and draw one short stroke each at 64px Buildup, 64px Wash, 256px Buildup and 256px
Wash. Close normally. Inspect condition records, projection rectangle containment,
per-view notification coverage and each upload's transfer geometry/swap status,
with no dropped events. Keep it short to stay below the event bound. No app is
launched or terminated by the agent, and no preferences are changed.

Priority 1 remains incomplete. These stage checks are not yet a joined guarantee
of every input's visible pixels: explicit stroke identity, dirty-group geometry,
notification compression/transfer coverage linkage and controlled repeated
three-path captures still need completion. Superseded pixels and offscreen work
also require explicit sample inclusion rules before publishing latency.

Combined real-app follow-up (October 5, capture
`%TEMP%/solstice-paint-trace-brush.2572.json`, 22,257,961 bytes): no dropped events.
All four accepted begins have condition records for `paintbrush`, preset
`b) Basic-4 Flow Opacity`, RGBA/F32, 2480x3508. Actual nominal sizes/modes were
64.2358px Wash, 64.24px Buildup, 256.1px Wash and 256.1px Buildup. These satisfy
the approximate-size integration check, not an exact-size controlled benchmark.

There are 91 accepted dispatches: four begins, 83 moves and four ends. The 80
request-producing inputs have all requests assigned to batches; all recorded
branches from their 33 batches reach swapped commands. Eleven dispatches (four
begins, three moves and four ends) have no recorded request descendants. Another
68 batches are not input-linked and are excluded from this painting population.

Geometry results: all 37 projection requests have covered requested regions;
466 walker/canvas notification checks pass. Another 70 executed walkers have no
canvas link and remain unverified for that stage. All 70 finish within the first
9.759ms of the trace, before the first stroke at 11,269.060ms; their change
rectangles comprise two full-image sets of 35 patches. This supports startup
refresh as their context, rather than lost stroke updates; do not certify those
unnotified walkers as presented. There are 467 canvas updates in total, including
one without a merge parent, and 16 superseded updates.

All 450 upload occurrences pass image patch containment, mapped widget patch
containment, tracked-widget containment and same-widget swapped-command coverage.
No upload has unsupported mapping, intervening view invalidation, an uncovered
region or a missing acknowledgment. Seven `frame.reset` events exist globally;
none invalidates an upload's acknowledged interval. This validates the installed
condition/geometry hooks for this fixed-view real-app workload. It does not
establish pixel survival, physical display timing, or a three-path latency
baseline. No native changes or reinstall were needed for this log analysis.

## Explicit stroke membership and joined checks (phase 4.67)

`KisToolFreehand` records `stroke.input` edges from accepted input IDs to the
begin input ID. A tracing-only dynamic QObject property, `solsticePaintTraceStroke`,
holds the current ID on that tool instance; no exported class layout changes.
Every begin replaces the value, including an unscoped begin with zero. Move/end
read it; end emits `stroke.ended` and removes it before scheduling the helper's
end processing. Tool destruction also destroys the property. Deactivation's
existing `endStroke()` path clears it. These are diagnostic operations only when
tracing is enabled, with no persistent configuration or painting changes.
An unscoped input remains unassociated; no nearby timestamp is substituted.

`stroke_summary` validates that the input and its begin belong to the same
canvas, rejects conflicting memberships and makes the condition snapshot
available via the explicit begin ID. Begins themselves are already explicitly
identified by their accepted event/condition record. Older logs retain unknown
membership for later moves/end events. `stroke.ended` describes tool finalization,
not asynchronous job completion or presentation.

The pipeline input audit now follows each input's recorded downstream branches
and joins their projection-request geometry, executed-walker notification geometry
and upload/display geometry. Shared downstream branches are checked conservatively
in full, so a failure can exclude several inputs. `sample_readiness` combines
those checks with complete request/batch membership, all recorded branches reaching
swaps, known stroke conditions and a recorded stroke end. Its exclusions state
which prerequisite is missing; inputs without requests are excluded rather than
assigned zero latency. `recorded_checks_passed` is deliberately not named
"latency valid": dirty-group and compressed-update geometry, input interpolation
dependencies and pixel survival are not yet established. No timing metric is
published by this change.

Validation: all Python analysis tests 21/21, including a complete synthetic
joined chain, removal of a required rectangle, missing old-log membership,
conflicting strokes and cross-canvas association. Reanalysis of real-app capture
2572 finds no joined-stage geometry failures among the 80 request-producing
inputs. It correctly produces zero passing readiness rows because later-input
membership and stroke-end records were not instrumented in that version; this
does not invalidate the earlier stage-by-stage integration result.

`kritaui` rebuilt and installed. Vulkan-validation-enabled 64px/256px Buildup/Wash
stroke rows pass with tracing enabled and disabled, 6/6 each. These headless
regressions do not exercise the GUI membership hook; its interactive verification
is pending. Logs use `%TEMP%/solstice-paint-samples-` followed by `build.log`,
`install.log`, `enabled.txt` and `disabled.txt`. No application launch, termination
or preferences change was performed. Defer another identical manual capture until
the remaining dirty-group and compressed-update geometry linkage is ready, then
verify membership and the complete protocol together before three-path sampling.

## Dirty and compressed-update geometry bridges (phase 4.68)

`FreehandStrokeStrategy::issueSetDirtySignals` records `dirty.source_rect` after
collecting all painter dirty regions, before masking normalization/partitioning.
`dirty.submitted_rect` records the actual rectangles immediately before calling
the target node's `setDirty`, including inside the delayed masked-brush job.
Both use the dispatch ID, target-node owner and projection's current LOD. Empty
vectors produce an explicit empty rectangle rather than absent metadata. Existing
job order, masking partitioning and painting behavior are unchanged.

`geometry.bridges` checks source-region containment in submitted regions and
submitted-region containment in the directly linked projection request regions.
This complements the existing request-to-executed-walker check. It is group-level
provenance; it does not claim that each member dab affects every group rectangle.
Only the established LOD-0 coordinate convention is accepted; unknown owners,
missing records and other LODs remain unverified. Intentional clipping or other
unmodeled coordinate changes must not be interpreted as lost pixels solely from
a gap report.

Each supported upload also records `update.upload_bounds_rect`. The analyzer
follows same-canvas `update.merged`/`update.superseded` edges to actual upload
occurrences, verifies widget-to-canvas identity via `canvas.created`, and checks
that the original notification rectangle (clipped to the image bounds) is
covered by the union of descendant upload expectations. All descendant bounds
must agree; resizing, cross-canvas edges, missing metadata and absent transfers
remain unverified. Existing per-upload patch/widget/frame checks then verify the
upload expectations. Outside-image notifications are exclusions, not presentation
successes. The input-level readiness report now requires both new bridge checks.

Validation: all Python tests 24/24. Cases include holes after masking partitioning,
truncated projection requests, chained replacement/merge, clipping to image bounds,
cross-node/canvas rejection, image-bound changes and missing bridge records despite
otherwise successful frame ancestry. Vulkan-validation-enabled ordinary 64/256px
Buildup/Wash tests pass with tracing on/off (6/6 each); the masked-150-texture0
row passes with tracing enabled (3/3 including initialization/cleanup).

Trace `%TEMP%/solstice-paint-bridges-regression.45344.json` verifies both dirty
bridges for all 28 ordinary groups. The masked trace
`%TEMP%/solstice-paint-bridges-masked.39864.json` verifies all six masked groups,
including delayed submission. Both have zero dropped events. These fixtures have
no canvas and cannot verify real update compression or tool membership. `kritaui`
rebuilt/installed; logs use `%TEMP%/solstice-paint-bridges-` plus `build.log`,
`install.log`, `enabled.txt`, `disabled.txt` and `masked.txt`.

Combined real-app capture `%TEMP%/solstice-paint-trace-brush.45816.json` passed
after normal closure. Four RGBA32F strokes on a 2480x3508 image record the Pixel
engine and `b) Basic-4 Flow Opacity`, approximately 64px/256px in Wash/Buildup.
All 106 accepted inputs have explicit stroke membership; all four stroke ends
are recorded. All 94 request-producing inputs pass the joined checks, including
dirty bridges and compressed-update coverage. The other 12 inputs have no
recorded dab requests and remain excluded. All 40 input-linked batches reach
their recorded swapped-command branches; all 472 upload geometry/frame checks
pass. There are zero dropped events.

This does not certify the entire process: 58 batches without input links,
70 walker records without canvas links and one notification without an upload
remain outside the verified drawing-input population. They are not automatically
lost painting updates. Older captures intentionally lack the new records and
must not be retroactively accepted by the stricter gate.

Priority 1 remains open until controlled three-path sampling and its timing
analysis are complete. The recorded checks still describe command/region
provenance, not all interpolation dependencies, pixel survival or physical scanout;
any timing report must name its actual Qt presentation boundary and inclusion rules.

## Qt command-presentation timing (phase 4.69)

`build-tools/paint-trace/timing.py` consumes the joined readiness gate. Only an
input with exactly one finite, nonnegative mouse/tablet receipt timestamp and
verified geometry for every downstream upload is timed. The endpoint is the
latest required same-widget swap acknowledgment from `summarize_transfers`,
including frame replacement. An earlier successful sibling or unrelated later
swap cannot substitute for this endpoint. Missing, ambiguous, nonfinite or
reversed timestamps exclude the input. The analyzer reports per-stroke sample
counts, median, nearest-rank p95 and maximum in milliseconds; it does not pool
different strokes or processes. Shared batches/frames correlate samples.

All 29 Python tests pass, including the full joined-chain timing case, latest
required upload, unrelated swaps, invalid/duplicate receipts, incomplete geometry,
invalid/reversed acknowledgments and separate stroke populations. This phase is
offline analysis only: no native rebuild, installation or additional runtime
instrumentation cost. Existing command coverage remains distinct from physical
scanout, pixel survival and complete interpolation dependencies.

Reanalysis of `solstice-paint-trace-brush.45816.json` yields 94 timed inputs and
12 exclusions without dab requests. Exploratory single-process results follow;
these are not a controlled baseline or evidence that GPU beats CPU:

| Recorded condition | Inputs | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| ~64px Wash | 24 | 24.08 | 40.90 | 48.62 |
| ~64px Buildup | 26 | 18.66 | 39.65 | 43.02 |
| ~256px Wash | 22 | 55.48 | 64.53 | 66.29 |
| ~256px Buildup | 22 | 53.00 | 90.27 | 94.75 |

CPU-requested feasibility capture `solstice-paint-trace-cpu.43504.json` records
`projection_env=0`, `brush_env=0`, zero dropped events and four ended strokes.
All 95 accepted inputs have stroke membership. All 83 request-producing inputs,
22 input-linked batches and 275 uploads pass the joined checks and timing gate;
the other 12 inputs have no dab requests. Outside this population, 81 unlinked
batches, 70 walkers without canvas links and one notification without an upload
remain unverified. Recorded conditions are the same preset/hash and RGBA32F
2480x3508 image, with nominal sizes 64.4551/64.46px and 255.96px:

| Recorded condition | Inputs | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| ~64px Buildup | 21 | 21.48 | 43.13 | 48.29 |
| ~64px Wash | 21 | 18.66 | 31.50 | 43.11 |
| ~256px Buildup | 19 | 18.94 | 42.98 | 42.98 |
| ~256px Wash | 22 | 19.52 | 37.56 | 40.91 |

This verifies that the timing/geometry gate also works with the CPU-requested
configuration. Sizes, stroke order and hand-drawn inputs differ from the earlier
brush-requested capture, and each has only one process: do not calculate speedups
or treat these as a controlled baseline.

Projection-requested feasibility capture `solstice-paint-trace-projection.31336.json`
records `projection_env=1`, `brush_env=0`, zero dropped events and four ended
strokes. All 97 accepted inputs have explicit membership. All 76 request-producing
inputs, 23 input-linked batches and 241 uploads pass the joined checks and timing
gate. The other 21 inputs (four begin, 13 move, four end) have no recorded dab
requests. Outside this population, 15 unlinked batches, 70 walkers without canvas
links and one notification without upload remain unverified. All four snapshots
record exactly 64px/256px, the same preset/hash, RGBA32F and 2480x3508 dimensions.

| Recorded condition | Inputs | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| 64px Buildup | 17 | 20.43 | 46.29 | 46.29 |
| 64px Wash | 18 | 16.80 | 32.30 | 32.30 |
| 256px Buildup | 20 | 49.19 | 76.08 | 80.80 |
| 256px Wash | 21 | 43.67 | 78.00 | 78.12 |

These single-process observations validate the analysis in the third requested
configuration, not GPU execution or a speed comparison. The longer 256px intervals
are a candidate for controlled reproduction, not evidence of their cause.

The subsequent brush-requested capture `solstice-paint-trace-brush.45824.json`
records both environment flags as 1, zero dropped events and four ended strokes.
All 98 accepted inputs have membership. All 81 request-producing inputs, 27
input-linked batches and 275 uploads pass the joined checks and timing gate;
17 inputs without dab requests are excluded. Outside this population, 13 unlinked
batches, 70 walkers without canvas links and one notification without upload
remain unverified. Sizes are exactly 64px/256px, in Buildup/Wash order, with the
same preset/hash and RGBA32F 2480x3508 image as the projection-requested capture.

| Recorded condition | Inputs | Median ms | p95 ms | Maximum ms |
| --- | ---: | ---: | ---: | ---: |
| 64px Buildup | 22 | 23.97 | 45.72 | 49.55 |
| 64px Wash | 18 | 23.11 | 38.06 | 38.06 |
| 256px Buildup | 18 | 41.74 | 52.94 | 52.94 |
| 256px Wash | 23 | 36.40 | 51.91 | 51.93 |

Feasibility now passes in all three requested configurations. These are still
single-process hand-drawn observations, with differing input counts and slightly
different CPU brush sizes. Do not calculate speedups or update README benchmarks.
The launch logs contain ggml Vulkan initialization, which is not evidence of the
painting engine's effective GPU path. Before repeated baseline captures, establish
per-workload projection/brush/interop success or CPU fallback attribution; the
current environment flags and region trace alone do not establish this. Retain
all feasibility captures separately from the eventual controlled baseline.

### Controlled capture protocol

Track the current series, selected installed binary hashes and capture acceptance
in [paint-trace-baseline-runs.md](../../paint-trace-baseline-runs.md). CPU run 1 passed
trace checks: 16 strokes, 302 timed inputs, zero dropped events. Excluding the
four explicit warm-up strokes leaves 12 strokes / 222 inputs. Run 2 (projection)
also passed, leaving 12 measured strokes / 218 inputs, with CPU brush and shared
buffer uploads observed. Run 3 (brush) passed with 12 measured strokes / 228 inputs,
successful brush submission and shared-buffer evidence. Round 1 is complete;
run 4 (projection) also passed with 12 measured strokes / 220 inputs and the same
observed paths as run 2. Run 5 (brush) passed with 12 measured strokes / 236 inputs
and the same observed paths as run 3. Run 6 (CPU) passed with 12 measured strokes /
232 inputs and the same observed paths as run 1. Run 7 (brush) passed with 12
measured strokes / 231 inputs and the same observed paths as runs 3 and 5.
Run 8 (CPU) passed with 12 measured strokes / 255 inputs and the same observed
paths as runs 1 and 6. Run 9 (projection) passed with 12 measured strokes / 236
inputs. All nine captures are complete; see phase 4.72 and the run sheet's
completed comparison. The observed scene reuses child images rather than doing
multilayer stack composition; retain that distinction when reporting results.

1. Use the same installed build, machine, tablet/mouse, display refresh rate and
   window geometry. Close other heavy workloads. Record build revision and local
   diff, driver/display settings and preset edits alongside the captures.
2. Prepare one RGBA32F 2480x3508 document with a white background and one empty
   paint layer. Reopen this same document for every fresh process; never save the
   measured strokes into it. Use `b) Basic-4 Flow Opacity`, Normal blend, full
   opacity, no selection, channel locks, mirror, texture or masked brush. Keep
   smoothing, pressure behavior, zoom, pan and rotation identical. Use a fixed
   axis-aligned LOD-0 view and keep the complete strokes within the viewport.
3. The user starts `build-tools\paint-trace\run.cmd <devroot> <mode>` from Command
   Prompt. Modes are `cpu`, `projection`, `brush`; they request CPU only, GPU
   projection with CPU brush, and GPU projection with GPU brush respectively.
   The agent must not launch or terminate the app or alter `kritarc`.
4. Each process uses exactly 64px Buildup, 64px Wash, 256px Buildup, 256px Wash,
   in that order. For each condition make one warm-up stroke, wait for completion,
   then three short strokes of approximately equal length and duration in clean,
   separate regions. Leave a pause between strokes. Do not pan/zoom mid-stroke.
   Record deviations; normal closure flushes the trace. If overflow occurs,
   discard the run and shorten/split the protocol equally for every mode.
5. First capture one process per mode as a feasibility pass, using just one short
   stroke per condition (four strokes total, no warm-up). These are feasibility
   captures only and must not be pooled with the eventual baseline. For that
   baseline, use three fresh processes per mode in rounds `cpu/projection/brush`,
   `projection/brush/cpu`, `brush/cpu/projection`. Do not mix earlier exploratory
   captures or native instrumentation revisions into the baseline. Label warm-up
   stroke IDs explicitly in the analysis notes; the summarizer does not guess
   warm-up status or discard the first input automatically.
6. Run `python -B build-tools/paint-trace/summarize.py <trace files...>` and retain
   its JSON with the original logs. Check every condition snapshot, exclusion
   count, membership and geometry gate before comparing timing. Report per-stroke
   counts/median/p95 and per-process variation, with warm-up strokes separated;
   samples within a stroke are not independent trials. Hand drawing differences
   must be reported rather than attributed entirely to GPU changes.

Environment flags express requested configurations only. Effective GPU brush,
projection and interop/fallback use still needs corroboration before publishing
an actual GPU-path baseline; do not relabel CPU fallback as GPU success. This
protocol and timing metric prepare that comparison but do not complete priority 1
or the physical input-to-pixel acceptance criterion. README benchmark figures
remain unchanged until comparable results exist.

## Explicit execution-path evidence (phase 4.70)

Trace-only markers now distinguish actual submission from API success/no-op:

- `KisGpuBrushPainter` emits `path.brush.submitted` with the executing job ID
  only after a nonzero `submitAndFinish` result. Empty successful operations
  cannot produce this marker. BrushOp records CPU and failed-attempt CPU branches
  as `path.brush.cpu` / `path.brush.cpu_fallback`, skipping already-combined mirrors.
- `KisGpuProjectionCompositor` emits `path.compositor.submitted` only after
  successful submission, using the current flow and job IDs. Within a merger
  this identifies the executing walker. The shared compositor also serves other
  operations, so only explicitly matched walker IDs count as projection evidence.
  `KisAsyncMerger` records its direct CPU apply branch; `KisGpuMergeBatch` records
  CPU replay after a failed batch. A walker may legitimately contain both paths.
- After a renderer reports uploads issued, `KisOpenGLCanvas2` records whether
  its tile list still uses GPU shared buffers or CPU pixels, keyed by the unique
  upload ID. Interop-failure readback replaces GPU references before this point,
  so those uploads are counted as CPU-source uploads. Mixed lists keep both markers.

`build-tools/paint-trace/paths.py` associates these records with each input's
explicit batch jobs, executed walkers and uploads. It reports evidence counts
and identities without evidence. It never infers CPU execution from absent GPU
markers, nor GPU execution from environment flags. Shared identities are deduped
within each input, but rows across inputs must not be summed as independent work.
These markers add no waits, queries, persistent settings or painting decisions.
Submission is not completion, and CPU branches can have no pixel effect. Wash
preview/final merge and internal leaf work are not fully classified: this is
evidence for the observed stages, not an all-GPU stroke certification. Missing
records in earlier captures remain unknown and do not invalidate their geometry
checks. Do not use this evidence report alone as a new latency inclusion gate.

Validation: Python analysis tests 32/32, including mixed paths, unrelated events,
missing/zero identities and shared jobs, plus prior timing/geometry coverage.
Vulkan-validation-enabled 64px/256px Buildup/Wash regressions pass with tracing
on/off (6/6 each). Trace `solstice-paint-path-regression.41940.json` has no dropped
events and records 12 brush submissions, 233 compositor submissions, 272 CPU brush
branches and 368 CPU projection branches across the fixture's CPU/GPU runs.
Those process-wide counts are instrumentation evidence, not timing samples.
Canvas upload regressions pass 28/28 with Vulkan validation. `kritaimage`,
`kritaui`, version library and default paintops plugin are installed and match
their build outputs by SHA-256; the user had already closed the application.
Build/test/install logs use `%TEMP%/solstice-paint-path-` prefixes. Real-app
association of path markers with the four measured strokes remains to be checked.

Real-app capture `solstice-paint-trace-brush.46604.json` subsequently verifies
explicit brush and canvas attribution. Both requested flags are 1, with no dropped
events; all four strokes ended and all 96 accepted inputs have membership. All
83 request-producing inputs, 26 input-linked batches and 298 upload occurrences
pass the joined geometry/timing checks. Thirteen inputs without dab requests are
excluded. Conditions are exactly 64px/256px Buildup/Wash with the same Pixel preset
and RGBA32F 2480x3508 image. For every one of the 83 included inputs, all linked
brush job identities have successful submission evidence and all descendant
uploads have shared-buffer evidence, with no recorded CPU branch in those stages.

Projection attribution is only partial: all 43 Wash inputs have compositor
submission evidence for their walkers, while all 40 Buildup inputs have no
projection-path evidence. Do not classify the latter as CPU fallback or GPU
projection. `KisAsyncMerger::setupProjection` has an oblige-child reuse path that
leaves `m_currentProjection` empty, intentionally skipping composition; other
recalculation/no-op paths also remain unclassified. This is a candidate explanation
from code inspection, not yet established by the trace. Before another baseline
capture, distinguish explicit reuse/skip decisions from missing instrumentation.

Process-wide records include 26 brush submissions, 170 compositor submissions,
45 CPU brush branches and 298 shared-buffer uploads. The CPU branches are outside
the included input-linked jobs and must not be attributed to these strokes. As in
earlier captures, 15 unlinked batches, 70 walkers without canvas links and one
notification without upload are not certified. Exploratory timing medians/p95
in ms are 25.53/47.69 (64 Buildup, 21 inputs), 20.30/42.42 (64 Wash, 23),
52.29/74.50 (256 Buildup, 19), and 41.76/60.23 (256 Wash, 20). These use the new
native markers and are not pooled with older instrumentation or used as speedups.

## Projection reuse and skip evidence (phase 4.71)

`KisAsyncMerger` now records `path.projection.child_reused` in the existing
oblige-child setup branch, `no_target` when the corresponding composition call
has no destination, and `invisible` when an invisible leaf is skipped. An empty
walker records `empty_walk`. Root and extra-node recalculation record
`root_recalculated` / `extra_recalculated` after the existing call returns.
`KisLayer::updateProjection` records `original_reused` after releasing an unneeded
projection device, `masks_applied` after its mask branch, and `recalculate_skipped`
for its existing early return. All use the current walker flow ID; node ownership
is retained where available. Conditions and painting behavior are unchanged.

The path analyzer reports these decisions separately from CPU apply and GPU
submission. Recalculation alone proves neither backend; reuse of a layer original
does not prove reuse of the entire group. One known walker cannot hide another
walker without evidence. These markers explain skipped work without claiming all
internal processing is classified. Older captures remain unknown; do not infer
reuse retrospectively from the absence of submission markers.

Validation: 34 Python tests pass, including separate reuse/skip and CPU/GPU
evidence, mixed processing and an unknown sibling walker. The four ordinary
64px/256px Buildup/Wash regression rows pass with tracing on/off and Vulkan
validation (6/6 each including setup/cleanup). Trace
`solstice-paint-reuse-regression.19132.json` has zero dropped events and records
873 original-reuse decisions, 385 root recalculations, 364 CPU apply branches
and 89 mask branches across the fixture's runs. This fixture does not exercise
the oblige-child branch, so the real-app Buildup diagnosis remains pending.
`kritaimage` and version DLLs are installed and match build SHA-256 values;
logs use `%TEMP%/solstice-paint-reuse-` prefixes.

One combined real-app capture is requested using the existing `brush` launcher:
same RGBA32F document, fixed viewport and exact 64px Buildup, 64px Wash, 256px
Buildup, 256px Wash short strokes, then normal closure. Check the Buildup walker
decision records alongside successful brush submissions and shared-buffer
uploads before scheduling controlled baseline repetitions.

Real-app capture `solstice-paint-trace-brush.23828.json` completes this check.
Both environment flags are 1, with zero dropped events. All 96 accepted inputs
have membership and all four strokes ended. All 80 request-producing inputs,
26 input-linked batches and 306 uploads pass the joined geometry/timing checks;
16 inputs without dab requests are excluded. Sizes are exactly 64px/256px in
Buildup/Wash order, with the same preset/hash and RGBA32F 2480x3508 image.

All 40 included Buildup inputs have `child_reused`, `no_target`, `original_reused`
and `root_recalculated` evidence for their downstream walkers, with no identities
without evidence. This establishes the previously suspected reuse/skip path:
missing compositor submissions here do not indicate CPU fallback. All 40 Wash
inputs additionally have `masks_applied` and compositor submission evidence.
The compositor is shared with Wash processing; its marker is not necessarily
a separate layer-stack composition. All 80 inputs have successful brush submission
and shared-buffer canvas evidence, without recorded CPU branches in those linked
stages. This establishes the recorded paths, not that every internal operation
or the complete stroke runs on GPU.

Outside the included population, 13 unlinked batches, 70 walkers without canvas
links and one notification without upload remain unverified. The process-wide
39 CPU brush markers do not belong to the included input-linked jobs. Exploratory
median/p95 ms: 27.03/41.51 (64 Buildup, 22 inputs), 22.17/47.44 (64 Wash, 19),
51.41/70.44 (256 Buildup, 18), 35.28/50.90 (256 Wash, 21). Keep this capture separate
from the controlled baseline and earlier instrumentation revisions. Integration
verification is complete for this workload; priority 1 still needs the planned
three-process-per-configuration comparison, with actual paths reported rather
than assuming every requested projection configuration performs composition.

## Nine-process software-timing baseline (phase 4.72)

Completed on October 6, 2026 without changing the phase-4.71 native binaries.
The [run sheet](../../paint-trace-baseline-runs.md#completed-comparison-october-6-2026)
records all nine hashes, explicit warm-up IDs, per-stroke values, sample counts,
hierarchical aggregation and observed paths. Its local archive contains original
traces, launch logs, full summaries and `comparison.json`.

All 2,799 request-producing inputs passed joined checks, with zero dropped events;
683 accepted inputs without dab requests were excluded. Removing 36 warm-up
strokes leaves 108 measured strokes and 2,078 inputs. This completes the scheduled
manual software-boundary baseline, not the stronger physical input-to-pixel goal.
The user is not asked to repeat this nine-run series. CPU/GPU process ranges
overlap at 64px; 256px CPU summary medians are smaller. Shared-buffer configurations
are candidates for examining transfer preparation, waits, dirty area and scheduling.
No causal attribution follows from these observations alone. The scene reuses
child images, so this is not a GPU multilayer-stack comparison.

Next: priority 2 overhead decomposition, using existing traces for initial
analysis before adding narrowly scoped CPU/GPU measurements. Preserve actual
submission/reuse classification, do not sum overlapping spans, and do not infer
GPU execution time from CPU recording spans. The published software-timing
results retain manual-input, missing display-context and uncalibrated trace-cost
limitations; they do not establish a general GPU speedup. Earlier synthetic
benchmark values are retained separately; README links the new results.

## Upload-boundary wall-interval analysis (phase 4.73)

Priority 2 begins with offline reanalysis; no native binary or setting changes.
`build-tools/paint-trace/overhead.py` consumes only already-verified timing inputs
and their explicit descendant upload IDs. Each required upload must have exactly
one finite, nonnegative issue timestamp within the verified input/swap interval.
It partitions that interval at the **latest required upload-issued timestamp**:
input receipt to that boundary, and that boundary to the last required Qt swap.
Unrelated uploads are ignored. Missing, ambiguous or reversed timestamps exclude
the input rather than inventing an association. The partitions add to total wall
time for each input; independently aggregated medians/p95 do not necessarily add.

This is a chronological partition, not a GPU timer or causal critical path.
The first part includes job queues, dab generation, brush processing, projection,
canvas preparation/upload and any waits before the marker; the second includes
remaining canvas/presentation work and the Qt acknowledgment. Asynchronous stages
overlap; do not sum CPU spans or call the first partition transfer-only cost.

Validation: all Python tests 39/39. New cases cover last-required versus unrelated
uploads, exact per-input partition, missing/duplicate/nonfinite timestamps,
out-of-interval timestamps, empty verified population and nonadditive medians.
All nine raw baseline logs reanalyzed successfully with zero additional timing
exclusions; original timing reports are unchanged. All 2,078 measured inputs
remain after the same explicit warm-up exclusions. Original archived summaries
are untouched; each run adds `upload-boundary.json`, with the aggregate in
`%TEMP%/solstice-paint-baseline-471/upload-boundary-comparison.json`.

Aggregation follows the baseline hierarchy independently per component: median
of three measured stroke medians per process, then median of three processes.
The values below are milliseconds (before / after latest required upload issue):

| Condition | CPU | CPU brush + shared buffer | GPU brush + shared buffer |
| --- | ---: | ---: | ---: |
| 64px Buildup | 16.55 / 3.26 | 16.56 / 2.98 | 17.33 / 1.43 |
| 64px Wash | 15.91 / 3.76 | 14.77 / 3.24 | 16.68 / 2.42 |
| 256px Buildup | 19.77 / 2.31 | 31.20 / 2.74 | 23.01 / 1.08 |
| 256px Wash | 15.85 / 2.92 | 23.10 / 0.91 | 19.73 / 2.71 |

For 256px Buildup, process ranges before the boundary are 17.10–21.38 (CPU),
24.49–31.52 (CPU brush/shared buffer), and 18.95–26.63 (GPU brush/shared buffer).
After it, ranges are 2.25–3.23, 0.51–2.99, and 0.66–1.56 respectively. For 256px
Wash, before ranges are 14.68–17.64, 22.96–27.16, 19.60–22.06; after ranges are
2.46–3.24, 0.65–1.73, 2.02–3.54. Preserve overlap/variation and manual-input
limitations. The larger shared-buffer-mode intervals appear predominantly before
upload issuance, not as an increased trailing Qt acknowledgment interval.

Next investigation is the pre-upload region: distinguish queue delay, canvas
preparation, source staging and actual synchronization, correlating update IDs
instead of matching nearby timestamps. Existing broad `canvas.prepare` and
`canvas.upload` spans are not sufficient to uniquely assign every sub-operation
or GPU wait. Vulkan timestamp measurements, submission counts and transfer byte
accounting remain open priority-2 work. No runtime optimization or causally
established bottleneck is claimed; no further manual capture is requested for
this offline step.

## Direct update-ready to upload-issue analysis (phase 4.74)

2026-10-06: offline analysis only; no native rebuild or installation.
`overhead.summarize_ready_to_issue()` adds `ready_to_issue_timing` to the normal
paint-trace summary. It selects uploads linked to verified timing inputs,
deduplicates shared uploads across inputs, and joins each upload's explicit
parent to exactly one `update.ready` event. Missing/duplicate markers and
nonfinite, negative, boolean or reversed timestamps are excluded. It never
matches by timestamp proximity or substitutes an older compressed update.

Reanalysis of the nine archived phase-4.71 captures produced these exploratory
process summaries (all verified inputs, including warm-ups and all conditions):

| Run | Unique uploads | Median ms | P95 ms |
| --- | ---: | ---: | ---: |
| 01 CPU | 734 | 2.164 | 3.822 |
| 02 projection | 692 | 3.033 | 8.427 |
| 03 brush | 757 | 1.764 | 10.029 |
| 04 projection | 681 | 2.630 | 9.459 |
| 05 brush | 739 | 1.818 | 5.143 |
| 06 CPU | 701 | 2.107 | 3.691 |
| 07 brush | 750 | 1.754 | 5.281 |
| 08 CPU | 941 | 1.714 | 8.800 |
| 09 projection | 790 | 2.364 | 7.800 |

No selected upload was excluded. These are upload-weighted mixed-condition
diagnostics, not replacements for the measured-stroke baseline or evidence of
a speedup. The interval includes event-loop/compressor waiting and upload
processing. It excludes residence of earlier merged/superseded updates and
can start before a later input joins that update. Therefore it must not be
subtracted from per-input latency or summed across overlapping updates.
The existing native `update.ready` marker occurs after preparation; preparation
and the earlier drawing/projection scheduling remain unresolved. GPU execution
timestamps, transfer volume and submission overhead remain open.

Validation: 42 Python tests pass, including direct-parent selection despite an
older merged update, deduplication, missing/ambiguous/invalid timestamps, and
empty verified input sets. Run `python -B -m unittest discover -s
build-tools/paint-trace -p 'test_*.py'` from the repository root. Running the
ordinary `summarize.py` on each archived trace reproduces the new summary field.
No new interactive capture is required for this offline addition.

## Projection-end to prepared-update analysis (phase 4.75)

2026-10-06: `overhead.summarize_projection_preparation()` joins the explicit
walker ID in `projection.merge` to the parent ID of `update.ready`. Only walkers
downstream of verified timing inputs are selected. Each requires one complete
merge span and one ready marker with valid ordered timestamps; multiple ready
notifications are excluded rather than choosing the nearest. Walkers are
deduplicated globally and within each stroke. The normal summary now includes
`projection_preparation_timing`, including per-stroke medians and P95s.

All nine archived phase-4.71 captures passed without exclusions: respectively
737, 719, 767, 688, 757, 703, 756, 946 and 795 unique walkers including warm-ups.
The comparison below uses the explicit measured stroke IDs in the baseline
run sheet and archived `comparison.json`: median of three measured stroke
medians per process, then median of the three process values. Entries show
**CPU merge span / merge-end to update-ready**, in milliseconds. These are
walker-weighted intervals within a stroke, not an input-latency partition.

| Condition | CPU | CPU brush + shared buffer | GPU brush + shared buffer |
| --- | ---: | ---: | ---: |
| 64px Buildup | 0.010 / 0.258 | 0.010 / 1.067 | 0.010 / 0.143 |
| 64px Wash | 0.049 / 0.202 | 0.046 / 0.948 | 0.702 / 0.074 |
| 256px Buildup | 0.013 / 0.723 | 0.011 / 6.161 | 0.014 / 5.030 |
| 256px Wash | 0.096 / 0.601 | 0.097 / 5.744 | 2.021 / 0.251 |

For 256px Buildup, process ranges for merge-end to ready were CPU
0.608–0.747ms, CPU brush/shared buffer 5.957–7.908ms, GPU brush/shared buffer
4.214–5.120ms. For 256px Wash, the GPU brush merge CPU span was 1.775–2.489ms,
while its merge-end to ready was 0.161–0.440ms. Thus Buildup and Wash do not
justify assuming the same dominant stage. Shared batches/walkers correlate
samples; components' medians cannot be added or subtracted from input medians.
Merge spans measure host work, including submission/waiting when present, not
GPU execution. The subsequent interval includes notification and canvas
preparation/synchronization; it does not isolate a copy or queue wait.

Code inspection identifies next measurement sites in
`KisGpuCanvasUploader::upload()`: context acquisition/`commands.begin()`,
`KisGpuTileAccess::prepare()`, shared-buffer acquisition and submit/finish.
The context pool currently selects its last free entry without checking whether
its prior submission has completed; `begin()` can wait for that submission.
This is a candidate, not a demonstrated cause. Wash additionally needs its
indirect-painting/compositor span split. No synchronization or rendering behavior
has been changed based on the offline data alone.

Validation: 45 Python tests pass, including unrelated-nearer-marker rejection,
deduplication, per-stroke separation, ambiguous/missing events, invalid/reversed
timestamps and empty verified input sets. No native rebuild, installation or
new user capture was needed. Original baseline logs/summaries remain unchanged.

## Native preparation stage probes (phase 4.76)

2026-10-06: installed opt-in CPU scopes in `KisGpuCanvasUploader::upload()`:
`canvas.gpu.acquire_context`, `canvas.gpu.begin`, `canvas.gpu.prepare_source`,
`canvas.gpu.acquire_buffer`, and `canvas.gpu.submit_finish`. Added compositor
scopes for context acquisition, existing context wait, target/layer preparation
and submit/finish (`compositor.*`). Scopes carry the current walker flow ID and
the existing automatic job identity; direct test calls can legitimately have
zero flow. Multiple layer scopes per walker are expected. These are CPU spans,
not GPU timestamps or isolated transfer costs. The submit/finish compositor
scope also includes its existing result handling and success marker.

The wrappers preserve original return values, short-circuit validity checks,
failure cleanup and call order. No extra GPU wait or rendering branch was
introduced. Disabled tracing does not record timestamps/events. Existing
summary stage statistics expose these names; do not associate zero-flow spans
with nearby inputs or add overlapping scopes as input latency.

Build succeeded for `kritaui`, `KisGpuCanvasUploadTest`, `KisGpuStrokeTest`.
Validation-enabled trace-on tests exited successfully. Their logs contain
13,597 and 1,829 new stage events respectively, all finite nonnegative complete
spans, with no recorder overflow. Trace-off QtTest reports: canvas 28/28,
64/256px Buildup/Wash stroke rows 6/6. Logs/traces use the temporary prefix
`solstice-stage-476-`. Installed `libs/image` and `libs/ui`; no app was launched
or terminated by the agent.

Next manual capture is a focused diagnostic, not a repeat of the nine-process
baseline: launch `build-tools/paint-trace/run.cmd <krita-dev-root> brush` and use
the same RGBA F32 document and Basic-4 Flow Opacity preset. At exactly 256px,
draw three short Buildup strokes, then three Wash strokes, and exit normally.
The first of each group is warm-up. Inspect the new linked scopes before
choosing an optimization. This real-app check remains pending; automated
success does not establish which wait dominates in the user's document.

## Focused real-app stage capture (phase 4.76 follow-up)

2026-10-06: user closed the diagnostic process, PID 43812. Archived trace,
launcher log and full summary are under `%TEMP%/solstice-stage-476-real-43812/`.
Six ended strokes match Basic-4 Flow Opacity, exact 256px, RGBA F32 2480x3508:
Buildup warm-up 4193, measured 4959/5856; Wash warm-up 7144, measured 7904/8696.
All 112 timed inputs passed the recorded checks; 24 inputs without dab requests
are excluded. Recorder overflow is zero. This single-process diagnostic does
not replace the nine-process baseline or establish a performance improvement.

For each measured stroke, select the distinct downstream walker IDs of verified
timing inputs and collect the new CPU scopes with matching explicit `args.id`.
The table shows the two per-stroke medians in milliseconds, not summed values:

| Scope | Buildup 4959 / 5856 | Wash 7904 / 8696 |
| --- | ---: | ---: |
| canvas.gpu.acquire_context | 0.1602 / 0.2671 | 0.0001 / 0.0002 |
| canvas.gpu.begin | 0.0131 / 0.0145 | 0.0103 / 0.0108 |
| canvas.gpu.prepare_source | 0.1216 / 0.1575 | 0.0830 / 0.0698 |
| canvas.gpu.acquire_buffer | 0.1783 / 0.0003 | 0.0003 / 0.0003 |
| canvas.gpu.submit_finish | 1.1584 / 1.4042 | 0.1455 / 0.2210 |
| compositor.wait_context | not observed | 0.0080 / 0.0071 |
| compositor.prepare_target | not observed | 0.4789 / 0.5127 |
| compositor.prepare_layer | not observed | 0.1472 / 0.1307 |
| compositor.submit_finish | not observed | 0.1165 / 0.2354 |

Buildup has 119/102 matched canvas calls; Wash has 101/109 calls per listed
stage. Measured Buildup buffer acquisition has maxima 14.7683/10.9974ms, so its
occasional tail remains relevant despite low medians. Context begin medians do
not support treating reuse waiting as the main cost in this capture.

Next priority: split `KisGpuTileAccess::submitAndFinish()` into residency mutex
acquisition, recording uploads, queue submission, publishing and completion;
also inspect target preparation for Wash. Its present scope includes all these
operations, so a long value does not prove GPU execution or mutex contention.
Do not remove/reorder the residency lock: upload ordering and failed-state
publication depend on it. No runtime optimization has been made yet, and no
additional user capture is requested until narrower measurement is ready.

## Tile preparation and submission substage probes (phase 4.77)

2026-10-06: `KisGpuTileAccess.cpp` now records opt-in `tile_access.resolve_tiles`
(tile lookup, COW handling and pinning) and `tile_access.stage_uploads`
(staging allocation/reuse and CPU snapshot copies). Submission records
`tile_submit.lock`, `record_uploads`, `queue`, `publish`, and `complete`.
Names carry the `tile_submit.` prefix. Flow ID and existing job identity are
retained, and the command-list pointer identifies the submission owner.

The lock scope wraps construction of the existing RAII locker; the returned
locker remains alive across the original critical section. Upload ordering,
failed-state checks, state publication and restoration outside the lock are
unchanged. No GPU wait is added. The spans describe host intervals, including
waits inside their existing operations, not GPU execution. Trace recording
itself adds overhead, including while the residency mutex is held; these probes
are diagnostic and must not be treated as an uninstrumented performance result.
Preparation scopes apply to all tile accesses; match enclosing canvas or
compositor scopes with the same thread/flow to distinguish their callers.

Build succeeded. With Vulkan validation, canvas tests passed 28/28 and the
64/256px Buildup/Wash stroke rows passed 6/6, both tracing on and off. Trace-on
captures have 16,505/2,867 new finite nonnegative spans, zero dropped events,
and matching lock/record/queue/publish/complete counts (1,191/260 submissions).
Temporary artifacts use `solstice-stage-477-`. Installed the rebuilt image
library; no app was launched or stopped by the agent.

Real-app follow-up is pending: use the same phase-4.76 `brush` launcher and
256px Basic-4 Flow Opacity conditions, three Buildup then three Wash strokes,
with the first of each group as warm-up. Prior captures lack these substage
events. This is one diagnostic process, not another nine-process baseline.
Do not optimize lock scope or upload publication until this distinction is known.

## Real-app submission substage findings (phase 4.77 follow-up)

2026-10-06, PID 11944: six 256px strokes, zero recorder overflow, 108 verified
timing inputs; 23 inputs without recorded dab requests excluded. Buildup warm-up
3593, measured 4395/5159; Wash warm-up 6609, measured 7413/8213. Archived raw
trace, launch log and summary: `%TEMP%/solstice-stage-477-real-11944/`.

Select distinct downstream walker IDs of verified inputs per measured stroke.
Substages are assigned only to one enclosing canvas submit, compositor submit,
or compositor target-prepare span with the same flow ID and thread (timestamp
containment, not nearest-event matching). Per-stroke medians in milliseconds:

| Substage | Buildup 4395 / 5159 | Wash 7413 / 8213 |
| --- | ---: | ---: |
| Canvas submission lock acquisition | 0.9164 / 0.5936 | 0.0062 / 0.0001 |
| Canvas record uploads | 0.0012 / 0.0009 | 0.0006 / 0.0004 |
| Canvas queue submit | 0.0898 / 0.0546 | 0.0326 / 0.0406 |
| Canvas publish | 0.0003 / 0.0003 | 0.0002 / 0.0002 |
| Canvas complete | 0.0003 / 0.0003 | 0.0001 / 0.0002 |
| Compositor target resolve tiles | not observed | 0.1932 / 0.2039 |
| Compositor target staging | not observed | 0.1512 / 0.1150 |

Canvas submit counts are 95/97 for measured Buildup and 72/96 for measured
Wash; target staging is conditional and appears 56/69 times. These medians
must not be summed. The Buildup lock-acquisition interval is much longer than
the queue-submit interval in this capture. This identifies a host lock
acquisition bottleneck candidate, not GPU kernel execution time or the identity
of the lock holder. Wash does not show the same typical contention.

Next investigate all residency-lock holders: `pinState()` (including slot
allocation), `unpinStates()`, `tryEvict()`/`tryEvictBatch()` (which can download
under the lock), and concurrent `submitAndFinish()`. Existing traces cannot
uniquely identify the competing holder. Preserve upload ordering and failure
publication invariants; no lock removal is justified. No further manual capture
is requested until the next change is ready; no speedup is claimed.

## Residency lock-holder tracing (phase 4.78)

2026-10-06: added `residency.hold.*` scopes after acquisition and before release
at all current backend residency-lock sites: `pin_existing`, `pin_allocate`,
`unpin`, `evict`, `evict_batch`, `fail`, plus `submit` in tile access. RAII
destruction order preserves the original lock lifetime. `tile_submit.lock`
now uses the backend as owner and command list as related pointer, so wait and
hold records identify the same mutex. Other submission substage owners remain
the command list. Existing phase-4.77 wait owners are not compatible with this
holder join and must not be guessed by temporal proximity.

Unfiltered per-tile holds overflowed the canvas regression trace (621,250
dropped events); that initial trace is unusable. `KisPaintTrace.cpp` now omits
`residency.hold.*` spans below 10 microseconds before taking the recorder mutex.
The threshold is serialized as `residency_hold_min_us: 10`. Omitted short holds
are intentional filtering, not overflow. Other event families remain unfiltered.
Trace-on still adds clock/recording overhead; observed holds exclude unlock and
recording gaps and cannot fully account for every wait.

New `build-tools/paint-trace/residency.py` provides the summary field
`residency_contention`. It matches process/owner and different threads,
clips intersections with waits, and rejects owner groups with overlapping hold
intervals beyond a 1e-6 microsecond floating-point tolerance. Results are
observed overlap, not proof of scheduler causation. Sums count time per wait,
not elapsed process wall time, and include warm-ups unless filtered explicitly.

Validation: 50 Python tests pass, covering clipped overlap, gaps, wrong
process/owner/thread, ambiguous holds, invalid timestamps and serialization
roundoff. Native Vulkan-validation tests pass: canvas 28/28, stroke 6/6, with
tracing on and off (off run before the trace-only threshold adjustment).
Final filtered traces have zero overflow, invalid events or ambiguous groups.
Canvas: 1,191 waits, 446 with observed hold overlap; 449.46ms summed wait,
405.16ms overlap, of which 404.88ms belongs to other submit holders. Stroke:
260 waits, 45 matched; 3.91ms summed wait, 3.55ms overlapping submit holders.
These aggregate test workloads are not the user's six-stroke workload and
do not establish real-app contention ownership.

Installed rebuilt `libs/image`; logs use `solstice-stage-478-`, final trace-on
logs use `solstice-stage-478-filtered-`. Real-app follow-up remains one `brush`
process with the same 256px preset/document, three Buildup then three Wash
strokes. First stroke of each group is warm-up. No lock-order or performance
optimization has been applied before identifying the real-app holder.

## Real-app residency overlap (phase 4.78 follow-up)

2026-10-06, PID 44756: expected six 256px Basic-4 Flow Opacity strokes in
RGBA F32 2480x3508, 109 verified timing inputs; 27 no-dab inputs excluded.
No dropped events; hold threshold is 10us. Buildup warm-up 4152, measured
4891/5618; Wash warm-up 7040, measured 7754/8487. Raw trace, launch log and
summary are archived at `%TEMP%/solstice-stage-478-real-44756/`.

Select each stroke's verified downstream walkers and their enclosing
`canvas.gpu.submit_finish` scopes, then select contained same-thread/flow lock
waits. Compare against all same-backend cross-thread holds, including holders
outside that stroke's lineage. Results below are **sums over individual waits**,
not wall-clock stroke delays or medians; concurrent waits can overlap.

| Measured stroke | Canvas waits | Wait sum ms | Overlap with other submit holders ms |
| --- | ---: | ---: | ---: |
| Buildup 4891 | 74 | 68.0875 | 66.0468 |
| Buildup 5618 | 88 | 91.8102 | 89.0707 |
| Wash 7754 | 75 | 14.5011 | 13.5494 |
| Wash 8487 | 93 | 19.7232 | 18.2418 |

Thus about 97% of measured Buildup canvas lock-acquisition time overlaps other
submission holders. Pin-existing contributes just 0.113ms in the first Buildup
stroke; no other holder category overlaps those measured Buildup waits.
No ambiguous owner groups, invalid intervals or excluded waits were found.
Wash also overlaps submit holders, but has a much smaller summed wait in this
capture. This strengthens the case for investigating concurrent submissions
rather than slot allocation or eviction first. It does not prove a speedup
without tracing, nor quantify physical input-to-pixel delay.

One directly joined example: walker 4963 holds residency for 1.8486ms during
canvas submission; its `tile_submit.queue` interval is 1.8474ms. The queue
scope covers command-buffer ending and context submission, not just Vulkan
kernel work. Other calls can be shorter. Next inspect that path and safe ways
to reduce residency critical-section time while preserving upload generation,
queue ordering and failure-publication invariants. Do not simply move queue
submission outside the lock or remove locking. No further manual run is
requested until there is a concrete next change.

## End main command recording before residency acquisition (phase 4.79)

2026-10-06: `KisGpuCommandList::finishMainRecording()` ends the main command
buffer once, caches success/failure until `begin()`, and leaves the late-upload
preamble available. `KisGpuTileAccess::submitAndFinish()` calls it before
acquiring residency, traced as `tile_submit.finish_main`. Main commands are
already complete at this point; generation-dependent upload decisions and
preamble recording, queue submission, and state publication remain under the
original residency lock. Failure skips upload recording/submission and retains
the existing abandon/publish-zero/restore/complete path. `abandon()` does not
end the main buffer twice, and `begin()` clears the cached state.

After early finish, callers must not record more main commands. The existing
`isRecording()` indicates an open recording/submission cycle (including the
still-available preamble), not that the main buffer remains writable. Ordinary
`submit()` callers still work without explicitly finishing early. This changes
host recording-end timing, not GPU execution/submission order; preamble stays
first in the same submission. Queue submission itself remains in the critical
section. Improvement magnitude is unmeasured; earlier queue-scope observations
included both command-buffer ending and context submission.

Added `KisGpuEngineTest::testEarlyMainFinish`: main readback recorded and ended
before a late preamble fill, exact output for two reuse cycles, idempotent end,
abandon, injected submit failure, and successful reuse after failure. With Vulkan
validation: selected engine tests 6/6; shared upload arena, failed-state refusal,
failed-submission content preservation and older-upload ordering tests 13/13;
canvas 28/28; 64/256 Buildup/Wash stroke tests 6/6. Trace-on canvas also passes
28/28 and verifies 1,191 early-finish spans end before their matching lock spans,
with zero overflow. Artifacts: `%TEMP%/solstice-stage-479-*`.

Installed rebuilt GPU, image and UI libraries together (command-list layout
changed). Real-app follow-up: same `brush` launcher, exact 256px Basic-4 Flow
Opacity in RGBA F32, three Buildup then three Wash strokes. Compare against the
archived phase-4.78 capture, excluding first strokes, and check Undo/Redo for
visible regressions. No speedup is claimed until this check; no lock was removed.

## Early-finish real-app result: no demonstrated improvement

2026-10-06, PID 3704, phase 4.79: six expected 256px Basic-4 Flow Opacity
RGBA F32 strokes, 117 verified timing inputs, 14 no-dab inputs excluded, zero
overflow. Buildup warm-up 3907, measured 4687/5607; Wash warm-up 6974,
measured 7800/8714. Archive: `%TEMP%/solstice-stage-479-real-3704/`.

Each pair below is the two measured per-stroke medians in milliseconds.
Canvas substages use the same explicit walker/thread/enclosing-submit selection
as phase 4.78. Input timing is verified input receipt to command-swap notification.

| Metric | Before Buildup | After Buildup | Before Wash | After Wash |
| --- | ---: | ---: | ---: | ---: |
| Canvas lock acquisition | 0.7831 / 0.9466 | 0.8996 / 0.7845 | 0.0001 / 0.0077 | 0.0129 / 0.0172 |
| Early main finish | not split | 0.0015 / 0.0016 | not split | 0.0007 / 0.0007 |
| Canvas queue stage | 0.0382 / 0.0343 | 0.0397 / 0.0947 | 0.0309 / 0.0343 | 0.0347 / 0.0283 |
| Input to command swap | 30.269 / 34.326 | 35.876 / 60.148 | 34.585 / 32.662 | 48.180 / 44.333 |

No speedup is demonstrated. The moved operation is very small in these samples
and Buildup lock contention remains. End-to-end software timings are worse in
this capture, but one process per version with two hand-drawn measured strokes
per condition cannot isolate a regression caused by the change. Matched canvas
call counts also differ (before Buildup 74/88, after 115/112; before Wash 75/93,
after 110/129). Do not label this a verified optimization in README/benchmarks.
User reported closure only; explicit Undo/Redo success was not provided.

Next investigate `KisGpuContext::submit()` queue-mutex acquisition and the
driver `vkQueueSubmit2` call, especially long competing submissions. Early main
finalization is retained as a correctness-tested change with unproven latency
benefit; do not remove residency ordering protection based on this result.
No additional manual repetition requested at this analysis step.

## Queue-lock and driver-call timestamps (phase 4.80)

2026-10-06: optional `KisGpuSubmitTiming` output in the context/command-list
submission API captures steady-clock nanoseconds before/after queue-mutex
acquisition and immediately before/after `vkQueueSubmit2`. It is reset on each
call; injected refusal leaves driver timestamps zero. Null output performs no
clock sampling. GPU execution is not timed by these host timestamps.

Tile access requests timing only when paint tracing is enabled and records
saved `tile_submit.queue_lock`, `tile_submit.queue_prepare`, and
`tile_submit.driver` intervals through `KisPaintTrace::externalSpan()` after
the residency critical section and completion work. This avoids acquiring the
trace recorder mutex inside these newly measured queue/driver intervals.
Each event retains backend owner, command-list related pointer, flow, job and
originating thread. Events may appear later in the JSON array than their
timestamp; analysis must use timestamps and explicit identities, not array
adjacency. Existing surrounding scopes still incur their normal trace overhead.

New `testConcurrentSubmissionTiming` uses four worker threads, independent
command lists/output buffers and 32 submissions each. It checks all readback
words, 128 unique timeline values, ordered host timestamps, refusal without a
driver call, successful untimed reuse, and no added validation errors.

Vulkan-validation regression results: selected engine tests 7/7, shared arena /
failure / old-upload ordering 13/13, canvas 28/28, stroke 6/6; trace-enabled
canvas/stroke also 28/28 and 6/6. Python analysis remains 50/50. Trace captures
have zero overflow and one uniquely enclosing queue scope for each of the three
new intervals across 1,191 canvas and 258 stroke submissions.

Exploratory full-test medians (queue lock / preparation / driver, milliseconds):
canvas 0.0001 / 0.0008 / 0.0520; stroke 0.0001 / 0.0005 / 0.0445. Stroke P95
queue-lock and driver intervals are 0.1471 and 0.2710ms. These mixed test calls
suggest investigating driver-call tails, but are not the real-app Buildup
measurement. No rendering/locking policy or claimed performance result changed.

Artifacts use `%TEMP%/solstice-stage-480-*`. Installed GPU/image/UI libraries;
their build/install hashes match. Next focused real-app capture remains the
same six-stroke 256px Buildup/Wash protocol, to inspect the competing submit
holders' queue-lock and driver intervals. Do not repeat the nine-run baseline
until there is a demonstrated runtime improvement worth benchmarking.

## Real-app driver-call overlap (phase 4.80 follow-up)

2026-10-06, PID 24148: six expected 256px Basic-4 Flow Opacity strokes,
RGBA F32 2480x3508, 165 verified timing inputs (108 after warm-up exclusion),
52 no-dab inputs excluded, zero overflow. Buildup warm-up 4027, measured
5252/6402; Wash warm-up 7986, measured 9250/10529. Archive:
`%TEMP%/solstice-stage-480-real-24148/`, raw trace, launcher log and summary.

Select verified walkers, contained same-thread/flow canvas-submit lock waits,
and all directly timed queue/driver spans of other threads on the same backend.
The overlap sums below count time per waiter; concurrent waiters can count the
same driver interval repeatedly. They are not wall-clock stroke duration.

| Stroke | Canvas waits | Wait sum ms | Other submit hold overlap ms | Other driver-call overlap ms | Other queue-lock overlap ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Buildup 5252 | 136 | 108.5455 | 104.0951 | 103.0312 | 0.0187 |
| Buildup 6402 | 125 | 94.1548 | 90.7168 | 89.7886 | 0.0206 |
| Wash 9250 | 142 | 14.2552 | 13.1517 | 12.2399 | 0.4865 |
| Wash 10529 | 132 | 10.2298 | 9.2950 | 7.7846 | 1.1863 |

Buildup's driver-call overlap is about 95% of summed residency acquisition
time. Other-thread queue-information preparation overlap is only 0.2112 and
0.1654ms for these two strokes. This directly narrows the observed long holder
intervals to `vkQueueSubmit2`, rather than the context's queue-mutex acquisition.
It does not explain why the driver call takes time or establish a driver bug,
GPU execution duration, or physical input-to-pixel latency.

For directly selected canvas calls, Buildup lock medians are 0.5454/0.6640ms,
driver medians 0.0436/0.0482ms with maxima 2.467/2.400ms; queue-lock medians
are 0.0001ms. Wash driver medians are 0.0225/0.0228ms; queue-lock medians
are also 0.0001ms. Selected canvas call counts are larger than input counts,
so the next candidate is coalescing compatible canvas preparation/submissions,
using actual patch/tile coverage and preserving compression lineage, fallback,
color conversion, and complete image-update coverage. Keep serial queue ordering
and residency publication protection; removing those locks is not justified.

Measured input-to-command-swap medians are Buildup 22.942/24.098ms and Wash
21.101/28.408ms. Capture lengths and per-stroke update counts differ from prior
captures; no runtime change occurred since phase 4.80 and no speedup is claimed.
No further manual repeat requested at this analysis step. Next implementation
should reduce demonstrably redundant submissions before rebenchmarking.

## Shared canvas update builds (phase 4.81)

2026-10-06. Classifying the PID 24148 submit spans by enclosing scope showed
834 canvas, 428 compositor and 62 brush submissions, against only 169
GUI-thread canvas consumptions (`canvas.upload`). Canvas lock waits overlapped
other threads' canvas driver calls for 353.1ms, compositor calls for 14.1ms and
brush calls for 2.2ms (summed per waiter, whole capture including warm-ups).
The contention is therefore mainly between concurrent canvas uploads of
different projection walkers. Canvas driver calls also had the longest tail
(median 0.030ms, P95 1.09ms, maximum 4.55ms).

Implementation (all in `libs/ui`, GPU canvas path only):

- `canvas/KisCanvasUpdateBatcher.*`: group commit. `process()` enqueues a
  request with its thread's paint-trace flow and returns only after a build
  containing it has finished. A thread with no build running takes the oldest
  pending requests (at most 32 and 1 Mpx after the first, which is always
  taken) and builds them; others wait on a condition variable (`canvas.batch_wait`
  scope). Builds never overlap and complete in arrival (ticket) order. A
  same-thread re-entry from inside a build builds alone instead of deadlocking.
- `KisCanvas2::startUpdateCanvasProjection()` uses the batcher when
  `KisAbstractCanvasWidget::sharesProjectionUploads()` is true (OpenGL canvas,
  GPU canvas upload enabled and supported for the projection/display pair, no
  soft proofing or channel selection). Otherwise the original per-update path
  runs unchanged (QPainter canvas, CPU canvas path).
- The build creates **one update info per request**, unchanged rects, trace ids
  and compressor semantics. It emits each request's `update.ready` with that
  request's captured flow, `update.request_rect`, and a new informational link
  `update.batched` (later update -> first update of the batch). The infos are
  put into the compressor atomically (`KisCanvasUpdatesCompressor::putUpdateInfos()`)
  before any waiting thread returns.
- `KisOpenGLUpdateInfoBuilder::buildUpdateInfos()` collects the tiles of all
  rects and calls `KisGpuCanvasUploader::upload()` once. On failure each info
  takes the original CPU path (per-rect `syncToCpu`, retrieve, convert).
  The single-rect `buildUpdateInfo()` now delegates to it.
- `KisGpuCanvasUploader::upload()` groups patch centers into source regions,
  merging only centers closer than one 64px GPU tile, and prepares one
  read-only `KisGpuTileAccess` per region. A combined row-major address table
  over the regions' tile grids (zero between regions, never read) feeds the one
  patch-writer dispatch; all accesses go to one `submitAndFinish()`. Distant
  updates (e.g. mirrored dabs) are not widened to a bounding rect. Region grids
  are checked to be disjoint. A single update produces exactly the previous
  single access and table.
- `KisGpuCanvasUpload` GL holds nest. `KisOpenGLCanvas2::updateCanvasProjection(QVector)`
  acquires all uploads of the taken updates once, and releases after every
  update was applied; `recalculateCache()` holds still nest inside it. The last
  release signals GL completion, so no update of a batch can read a buffer after
  Vulkan may reuse it. A re-acquire after the final release is a safe assert.

Ordering argument: before, a walker thread returned from `sigImageUpdated` after
its canvas submission and compressor put; conflicting walkers start only after
that. The batcher keeps this: a waiting thread returns only after a build that
submitted its read and put its info. Builds are serialized and in ticket order,
so compressor puts remain in read order. Reads are still ordered on the single
queue before later conflicting projection writes.

Tests (Vulkan validation): `KisGpuCanvasUploadTest` 38/38, including 10 new
`testSharedUploadMatchesCpu` rows: overlapping, distant (separate accesses in one
submission), one-pixel and outside-image rects build with one upload counter
increment, one shared upload object, CPU-identical textures, and nested hold
behavior; trace-enabled run also 38/38; zero validation errors. The existing GL
import failure test now releases its own outer hold (it had relied on idempotent
acquire). New `KisCanvasUpdateBatcherTest` 8/8 (single thread, waiters sharing a
build, request/pixel limits, 8 threads x 300 requests without overlap, duplicate or
early return, re-entry), five repeated runs stable. Python analysis 50/50.
`KisGpuStrokeTest` has no canvas and does not exercise this path. Only `kritaui`
changed; its build/install SHA256 match.

Not yet measured in the real app. Expected effect: fewer canvas submissions and
less canvas-vs-canvas residency contention in 256px Buildup; Wash had less
contention. The GUI may receive more update infos per frame; texture upload
volume per update is unchanged. Do not claim a speedup before a focused capture.
Manual checks required: painting/undo/redo/mirror/zoom/LOD with the GPU canvas,
soft proofing and channel selection (unbatched CPU path), and GL import fallback.

## Real-app capture of shared canvas builds (phase 4.81 follow-up)

2026-10-06. The user reported the manual checks (painting, Undo/Redo, mirror,
zoom/rotation, soft proofing, channel selection, save/reload) without problems.
A first capture (PID 41908) combined the manual checks with the strokes and
overflowed the event limit (213,587 dropped). It is not used for performance.
It confirmed batching in the app (1,808 updates, 1,304 canvas submissions,
504 `update.batched` links) without warnings in the launcher log.

Focused capture PID 43720: six 256px strokes, zero dropped events, 129 inputs
passing recorded checks. Buildup warm-up 3056, measured 3827/4613; Wash warm-up
6102, measured 7151/8211. Archive: `%TEMP%/solstice-stage-481-real-43720/`.
Geometry: all 731 uploads `covered_to_swapped_commands`.

Whole-capture comparison with PID 24148 (both include warm-ups):

| Metric | 4.80 (24148) | 4.81 (43720) |
| --- | ---: | ---: |
| Canvas updates / canvas GPU submissions | 834 / 834 | 774 / 241 |
| All residency submissions (`tile_submit.lock`) | 1,324 | 766 |
| Summed residency lock wait ms | 459.8 | 73.0 |
| Summed canvas submit (`canvas.gpu.submit_finish`) ms | 598.6 | 42.8 |
| Summed `canvas.prepare` ms (includes batch waits) | 2,301.0 | 1,059.6 |
| Summed `canvas.batch_wait` ms | - | 916.0 |

Measured strokes, medians in ms:

| Stroke | Merge end -> ready | Input -> first merge start | Input -> swap |
| --- | ---: | ---: | ---: |
| 4.80 Buildup 5252 / 6402 | 3.47 / 4.28 | 13.43 / 13.58 | 22.94 / 24.10 |
| 4.81 Buildup 3827 / 4613 | 0.43 / 0.25 | 27.07 / 25.27 | 38.50 / 32.62 |
| 4.80 Wash 9250 / 10529 | 0.17 / 0.23 | 12.39 / 16.56 | 21.10 / 28.41 |
| 4.81 Wash 7151 / 8211 | 1.28 / 1.33 | 25.26 / 37.40 | 39.46 / 50.41 |

The targeted post-merge interval fell for Buildup, but input-to-swap rose. The rise
is before the projection merge: input to dab request stays about 0.1ms and dab
render job queue delay stays about 2ms, but a requested dab waits 21-35ms (was
10-15ms) to be taken into a brush batch. Batch cadence (median 9-15ms gaps) and
batch-ready to paint start (0.02ms) are unchanged. The strokes of this capture
were much shorter and faster: 102-132ms at 860-1,566 dabs/s, versus 151-216ms at
607-1,199 dabs/s. Batches took about 16-19 dabs, so the dab queue backlogs at these
rates (`someDabsAreStillInQueue`). The input-to-swap difference is therefore
confounded by hand-drawn stroke speed and is not attributed to the canvas change.
Neither a speedup nor a regression of input-to-swap is established. A fair
comparison needs strokes of similar length and speed. The brush batch dab limit
under fast strokes is a separate candidate for investigation.
