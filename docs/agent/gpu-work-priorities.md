# GPU Engine — current work priorities

This page owns remaining work and deferrals. The
[maintenance reference](gpu-engine.md) owns implementation, compatibility and
regression rules; the [phase records](gpu-engine.md#phase-history) own detailed
results. The complete previous plan, priority rationale, measurements and session
diary are preserved in the
[work-order snapshot](wiki/history/gpu-work-priorities-development.md).

## Scope and evidence

Initially extracted on 2026-10-16 at
`cc2445cc4a521d58ae0904079ee67354667b3eb6` (through 4.109), then updated
for the integration of PRs #5/#7 (4.110/4.111). Follow
[the branch policy](../../AGENTS.md#branch-policy) for new work.
Later work in other checkouts/branches must be reconciled when it merges;
this page neither imports it nor marks its manual checks complete.

The original numbered priorities remain useful as a map, not a new instruction
to repeat completed work:

| Original priority | Current disposition | Evidence |
| --- | --- | --- |
| 1. Input-to-display measurement | Protocol and comparison completed; repeat for a relevant change, not automatically | [Baseline and re-baseline](paint-trace-baseline-runs.md) |
| 2. Short-stroke overhead | Implemented batching, update scheduling, shared pipelines and canvas handoff improvements; further optimization needs fresh evidence | [Latency history](wiki/history/gpu-phases-4.58-4.81.md), [brush latency](wiki/history/gpu-phases-4.82-4.92.md) |
| 3. GPU dab generation | Delivered circles, textures and predefined image applications at both float depths; supported masking composite also delivered | [Current brush contract](gpu-engine.md#pixel-brush-integration) |
| 4. Filters and transforms | Partially delivered: affine, Liquify, mesh Puppet Warp rendering and Gaussian convolution; broader coverage remains open | [Current contract](gpu-engine.md#transforms-and-gaussian-convolution), [phase evidence](wiki/history/gpu-phases-4.93-.md) |
| 5. Remaining blend modes | Delivered in 4.107/4.108 with explicit CPU exclusions; precision audit in 4.109 | [Blend contract](gpu-engine.md#all-other-blend-modes-phase-4108) |

The 2026-10-06 ordering placed blend expansion last; later user requests
authorized it before all possible filter work was complete. Do not retain the
old ordering as a prohibition against already-delivered work.

## Supported hardware

Decided on 2026-11-12: the engine starts by itself only on NVIDIA
Blackwell; other GPUs need the option in the settings
([reference](gpu-engine.md#feature-gate-and-user-interface-implemented-in-phase-33),
[decision](wiki/decisions/gpu-verified-devices.md)). No work is planned to
verify other GPUs: the user has none. Its manual check (checklist item 24)
was reported OK by the user on 2026-11-13.

## Remaining candidates and acceptance

Offset Clone Layer GPU copies (4.110) and partial-tile projection clears
(4.111) are delivered and user-checked on 2026-10-16.
[Clone Layer](clone-layer.md) owns the copy conditions and remaining
candidates; [projection clears](gpu-engine.md#partial-tile-projection-clears-phase-4111)
owns deferred-clear and CPU-reader behavior. Detailed measurements stay in
the linked benchmark page.

GPU compositing of moved layers and groups (4.112, 2026-11-12) is
implemented; [the contract](gpu-engine.md#moved-layers-and-groups-phase-4112)
and [measurements](wiki/benchmarks/move-layer-updates.md) are linked. Its
manual check (checklist item 19) was reported OK by the user on
2026-11-12. Open, not requested: a group
step still costs more than a layer step, and the final merge of a temporary
target off the tile grid stays on the CPU.

Remaining CPU-only items (user request, 2026-11-12): work through the
"What stays on the CPU" list of the [user guide](../gpu-engine.md), one
measured step and pull request at a time. Order chosen by the user: masks
and filter layers first; then filters, transforms and brushes (not yet
ordered). Excluded by design: integer layers, filter areas below about
180 x 180 pixels, wrap-around mode.

- Done: moved layers (4.112), transparency masks (4.113, checklist item
  20), the Stroke layer style, outside with a solid color (4.114,
  checklist item 21). The user
  moved the stroke ahead of the filter layers on 2026-11-12, after a count
  of their documents (73 strokes against 7 filter layers in float
  documents). Filter layers with Desaturate, and in RGBA F16 with
  Posterize or channel curves, and the Copy blend mode at full opacity
  (4.115, checklist item 22). Filter masks with those filters, and Color
  Reference filter layers and masks in RGBA F16 (4.116, checklist item
  23).
- Manual checks: the user ran checklist items 19 to 23 in the development
  build on 2026-11-12 and reported them OK. The run found one crash
  (adding a filter mask to a layer off the tile grid), fixed the same day
  ([tile data lifetime of readers](gpu-engine.md#gpu-access-api)); the
  user reported that it did not occur again with the fix.
- Open from 4.116: Color Reference in RGBA F32 (the conversion to the
  working space has no table there) and with profiles of other primaries;
  the first Color Reference update of a session pauses about 0.2 s for
  the conversion tables. The CPU filter itself is slow (two single-pixel
  LittleCMS conversions per pixel) and is still used by the Filter dialog,
  RGBA8 documents and Instant Preview: batching its conversions would
  help those.
- Open from 4.115: Posterize and curves in RGBA F32 (no
  tables there; they would need LittleCMS's arithmetic); the other color
  adjustment filters (HSV, Levels, Color Balance and others: each needs a
  GPU form, tables where the channels are independent); hue, saturation
  and lightness curves; the Copy mode at reduced opacity; the Filter
  dialog and filter strokes. The user's documents use none of them.
- Found in 4.115 and done in 4.116: the Color Reference filter mask on
  the main group of one of the user's RGBA F16 documents (about 330 ms
  per full-image Move Tool step; 10 ms now).
- Open from 4.114: Inside and Center strokes, gradient and pattern fills,
  the other style effects, and styles during Instant Preview stay on the
  CPU. The user's documents use none of them except one Outer Glow.
- Open from 4.113: a masked layer whose projection device is off the
  64 px grid (the layer was loaded or masked at such an offset) keeps the
  CPU mask path; measure painting on moved layers before changing
  `KisSafeNodeProjectionStore`.
- Next: not chosen. Candidates from the user's list: other filters
  (including the sharpening step of Unsharp Mask), transforms, brushes.

1. **Failure/save UI verification: done except autosave.** Accepted on
   2026-11-11 with Test Control's `gpu_fault` (results in
   [checklist item 16](gpu-engine.md#manual-regression-checklist)). Still
   open: the autosave exemption check. Findings left for a user decision:
   the warning's lost-tile count stops at the first report, its texts are
   untranslated, and a refused save replaces the `~` backup.
2. **Continue priority 4 when requested.** Measure remaining filters before
   choosing the next target (levels/curves were in the original scope, but their
   4.93 baseline configuration was questioned and must be corrected first).
   Maintain CPU fallbacks, operation-specific numerical parity, preview/final
   consistency, Undo/Redo and persisted behavior. Puppet Warp follows
   [its own contract](puppet-warp.md).
3. **Refresh performance claims when behavior changes.** The 2026-10-10
   re-baseline compared GPU brush input-to-display around 5 ms with CPU around
   18-22 ms. The CPU update policy changed in 4.100 afterwards; current CPU
   latency has not been re-measured in this record. Use the
   [capture protocol](wiki/history/gpu-phases-4.58-4.81.md#controlled-capture-protocol),
   three paths (CPU, GPU projection/CPU brush, GPU brush), fixed conditions,
   sample/process counts and dropped-event checks. Qt frame swap is not physical
   input-to-photon latency; queued-stroke benchmarks exclude input/display.
   Update the [user benchmark guide](../gpu-engine.md#current-benchmarks) and
   README together only when new representative evidence warrants it.

For implementation work, completion requires relevant automated parity,
fallback/failure, lifecycle and undo tests, plus documented manual acceptance.
State pending verification explicitly. Timings are measurements, never test
pass/fail thresholds or universal performance promises.

## Deferred by the user

- **Large brush × mirrors × Alpha Lock catch-up:** deferred on 2026-10-03.
  Keep the delivered improvements; do not resume without a new request.
- **Painting the masking brush's GrayA8 mask on the GPU:** investigated and
  deferred on 2026-10-11 because the measured benefit did not justify the
  new integer tile/painting path. This is separate from the supported
  F32/F16 masking composite.
- The technically excluded blend modes remain CPU fallbacks, not an implicit
  promise to port them. See the maintained exclusions and their reasons.

The original decisions and workload-specific measurements are retained in the
[work-order snapshot](wiki/history/gpu-work-priorities-development.md).

## Updating this page

- Update disposition/remaining work when a phase lands. Put full implementation
  logs and measured results in the newest GPU history page; link them here.
- Reconcile the maintained contract, user guide and work queue in the same
  change when behavior changes. Do not reintroduce a chronological diary here.
- Record user decisions or measurement evidence when priorities change.
  Historical next steps do not authorize new implementation.
- Allocate phase numbers using the
  [numbering decision](wiki/decisions/gpu-phase-numbering.md), checking existing
  records and concurrent reservations before choosing a number.
