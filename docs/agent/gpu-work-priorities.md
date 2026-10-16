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

## Remaining candidates and acceptance

Offset Clone Layer GPU copies (4.110) and partial-tile projection clears
(4.111) are delivered and user-checked on 2026-10-16.
[Clone Layer](clone-layer.md) owns the copy conditions and remaining
candidates; [projection clears](gpu-engine.md#partial-tile-projection-clears-phase-4111)
owns deferred-clear and CPU-reader behavior. Detailed measurements stay in
the linked benchmark page.

1. **Close outstanding failure/save UI verification.** The archived phase plan
   records automated coverage but leaves interactive dialog acceptance pending.
   Use the [manual checklist](gpu-engine.md#manual-regression-checklist) and
   record the tested build and result. A documentation extraction does not
   establish acceptance.
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
