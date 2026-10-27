---
type: pitfall
updated: 2026-10-27
sources:
  - libs/ui/kis_node_model.cpp (setDummiesFacade, slotDummyChanged, processUpdateQueue, slotBeginRemoveDummy)
  - libs/global/kis_signal_compressor.cpp (FIRST_ACTIVE)
  - libs/ui/tests/scenarios/lazy-rename-layer.json
  - docs/agent/test-control.md ("Known limitations and pending work")
related:
  - weak-pointer-map-keys.md
  - synthetic-canvas-input.md
---

# Compressed updates can outlive their model

A `KisSignalCompressor` delivers its signal up to its interval after the last
`start()`. Whatever the slot reads must still exist then. `KisNodeModel`
queued raw `KisNodeDummy` pointers in `slotDummyChanged()` and flushed them
100 ms later in `processUpdateQueue()` through its index converter. When the
document closed in between, `setDummiesFacade()` deleted the converter (and
the dummies went with the document), but the compressor and the queue were
left running, so the timer dereferenced a null converter (crash found by a
test control scenario, 2026-10-27; also in the Krita 6.0 snapshot).

With `FIRST_ACTIVE` the first change is delivered at once and only the next
ones wait for the timer, so it took two changes (a rename that also set the
color label) followed by a close within 100 ms. A person rarely closes that
fast; scripts and automated tests do.

When the source of queued items changes (a model's facade, a document, a
canvas), stop the compressor and clear the queue, as
`slotBeginRemoveDummy()` already did and `setDummiesFacade()` now does. To
find such races, follow a change with the teardown at once (no pause), and
repeat; a pause longer than the interval hides them.
