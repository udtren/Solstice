# Layer Comps - agent development notes

## Status and locations

2026-10-16: LC1-LC4 implemented, built, tested and installed.
The user confirmed normal operation on 2026-10-16, and reported excessive
spacing when resizing. The user confirmed the layout refinement on 2026-10-16.
LC5 (new export engine,
cancel and format selection) is not included.

The user authorized implementation after the review on 2026-10-16.
Implementation defaults follow the proposals in the original plan, with
double-click preserved as Apply. These are implementation choices under that
authorization, not separate user confirmations of every option.

- Core: `libs/image/kis_layer_composition.*`, `kis_image.*`.
- Serialization: `plugins/impex/libkra/kis_kra_loader.cpp`, `kis_kra_saver.cpp`.
- UI: `plugins/dockers/compositiondocker/` (`CompositionDocumentState.*`,
  `compositionmodel.*`, `compositiondocker_dock.*`, `wdgcompositiondocker.ui`).
- Tests: `libs/image/tests/KisLayerCompositionTest.cpp` and
  `plugins/dockers/compositiondocker/tests/CompositionWorkflowTest.cpp`.
- User workflow: [Layer Comps](../layer-comps.md).

## Configuration and compatibility

Keep docker ID `CompositionDocker`, plugin/module IDs and action IDs
`update_composition` / `rename_composition`. Only the visible docker title
changes. Double-click applies; the existing Rename action edits names.
Up/Down without modifiers applies; Ctrl/Shift navigation does not.

Existing `<composition name exportEnabled>` and `<value uuid visible
collapsed>` records are unchanged. `<compositions newLayersVisible="1|0">`
stores the new policy. Always save the parent element, including zero comps.
The loader defaults the image flag to false before loading nodes, including
legacy files with no compositions element. Newly constructed images default
to true. Image copies and replacement copy both records and policy.

Missing UUIDs apply as hidden regardless of the current policy. The policy
only affects newly added nodes and never retroactively changes records.
Record both visibility and collapsed state: the old loader interprets a
missing collapsed attribute as true. New XML data is an attribute, not a
child that old loaders could mistake for another comp.

## Architecture and lifecycle

### Image state and locking

`KisImage::Private::compositionsLock` protects the list and new-layer flag.
Each `KisLayerComposition` has its own mutex protecting records, name and
export flag. Readers take value copies. Never hold a composition mutex while
waiting for the image barrier, and never hold the list mutex while acquiring
composition locks or emitting signals. `sigCompositionsChanged` updates
observers; UI mutations separately call `setModifiedWithoutUndo()` so loading
does not mark files dirty.

`nodeHasBeenAdded()` snapshots the list, then visits only `parent->at(index)`
and its children. Existing UUIDs are untouched, preserving deletion undo,
moves and repeated insertion. Fake nodes are skipped. Loading nodes precedes
loading compositions. Do not reuse the adjacent keyframe walk: that walk
starts at the parent and includes existing siblings.

Tree-reading methods (`store`, `apply`, `createApplyCommand`, visibility
matching and cross-image composition copies) require the caller to own the
image barrier or execute within the scheduler's protected context. Map
mutexes alone do not protect the graph. The docker locks around recording
and command construction. Indicator refresh uses `tryBarrierLock(true)` and
retries after 100 ms if busy; it does not block a running stroke or reset LoD.

`createApplyCommand()` builds a `KisCommandUtils::CompositeCommand` of
`KisNodePropertyListCommand`s for differing nodes. The outer image barrier
covers old-value capture and `undoAdapter()->addCommand()`. Nested barrier
locks are supported. No command is added if visibility already matches.
Collapsed state remains non-undoable. Direct legacy `apply()` remains for
export and callers/tests but never inserts missing UUIDs. Group traversal
visits descendants once rather than repeating every nested group.

### Transient state and views

`CompositionDocumentState` is a child of `KisDocument`, found with
`findChild` by every docker showing it; `get()` returns null without a
document. The docker can still hold a canvas whose view manager has no
document while views switch or close, so `updateModel()` returns early
then and `setCanvas()` binds a state only when there is a document
(2026-10-27: a delayed refresh in that window crashed in `findChild()`). It owns the compressed refresh timer,
last applied comp, active indicator and custom-state snapshot. Dockers hold
it through `QPointer`, disconnect when switching canvases, and share its
`changed` notification. Image signals use queued connections. Image rebinding
and root replacement invalidate transient state. No weak pointers are map
keys.

Matching prefers the last applied comp, then the first matching saved comp,
then the snapshot. Thus Undo/Redo can light a different matching comp; equal
records have a deterministic single indicator. Matching compares local node
visibility, not effective visibility inherited from a hidden group, and
ignores collapsed state. Deleted nodes do not participate.

Before applying from an unmatched state, capture a new snapshot. Applying
the snapshot itself never replaces it. A state already matching a saved comp
or the snapshot is not captured again. Snapshot apply preserves nodes absent
from it. The row is excluded from list editing and export. Export temporarily
suspends this controller in all views, then requests a refresh after restoring
the image. Export still uses the live image; asynchronous copied-image export
is deferred to LC5.

### List model

Resize layout: the six tool buttons share a `QHBoxLayout` with 2 px spacing
and a trailing stretch, spanning the outer grid. Do not put fixed-size
buttons in separate expanding grid columns: that distributes spare width
between them. Only grid row 0 (the list) has vertical stretch; the name,
buttons and policy checkbox keep their normal heights.

Flat three-column tree view: Apply, Export, Name. Extended row selection is
independent of the serialized export checkbox. MIME moves accept only the
same model and move non-contiguous selections in stable order. `moveRows()`
supports contiguous blocks. The transient first row is never draggable or a
valid insertion target before which normal rows can be placed. Reordering
validates the full identity list in `KisImage::reorderCompositions()`.

Add, overwrite, delete, rename, reorder, export-checkbox changes and policy
changes mark the document modified. List edits are not undoable, matching
the previous scope. Model resets restore selected/current comps by identity.

## Build, test, and install

Paths on this machine: `C:\Users\udtre\Projects\krita-dev`; activate
`env.bat` before commands. Build targets `kritacompositiondocker`,
`kritalibkra`, `KisLayerCompositionTest`, `CompositionWorkflowTest`.

Run ctest with `-R "KisLayerCompositionTest|CompositionWorkflowTest"` and
`--output-on-failure`, with `QT_QPA_PLATFORM=offscreen`,
`QT_QPA_FONTDIR=C:\Windows\Fonts` and `KRITA_NO_ASSERT_MSG=1`.
The integration test uses batch mode, explicit native MIME on save and a
temporary directory; it also checks legacy XML directly. Do not run the
general `kis_kra_saver_test` or libs-ui suite on the user's desktop.

Install rebuilt libraries and the docker using `cmake
-DCMAKE_INSTALL_LOCAL_ONLY=1 -P <build>/<directory>/cmake_install.cmake`.
At minimum this includes `libs/image`, `plugins/impex/libkra` and
`plugins/dockers/compositiondocker`; install any rebuilt dependent libraries
too. Compare build/install hashes. Never terminate Solstice to unlock DLLs;
ask the user to close it. Fully restart after installation.

## Manual regression checklist

1. Open Layer Comps using an existing workspace. Existing shortcuts work.
2. Add and rename comps. Eye, double-click, plain Up/Down and previous/next
   apply; Shift/Ctrl selection and export checkboxes do not apply.
3. Apply A, B, Undo, Redo. One step restores all visibility, including nested
   groups and masks, and LoD preview agrees with the final canvas.
4. Make a custom visibility state; apply comps and restore Last Layer Display
   State. Repeat after new layers and deletions; close/reopen (no snapshot).
5. Overwrite/delete multiple selections; drag adjacent and non-adjacent rows.
   Saving after only delete/reorder/export-check edits retains those edits.
6. Policy on/off: single layer, group subtree, duplicate, paste, move,
   delete/undo/redo. Existing records do not change with the policy.
7. Save zero comps with either policy; reopen. Open older .kra files and
   confirm false policy. Verify new files in upstream Krita if available.
8. Two documents, two views/windows of one document; switch and close them.
9. Export checked images/animations. Both the canvas and transient snapshot
   are preserved afterwards; unchecked comps are omitted.

## Verification record

2026-10-16:

- Built `kritacompositiondocker`, `kritalibkra`, `KisLayerCompositionTest`
  and `CompositionWorkflowTest` with the existing LLVM/MinGW toolchain.
- Focused ctest run: **2/2 passed**, 7.86 s total. Three core cases cover
  non-mutating apply/Undo, subtree recording/copy and concurrent additions.
  Three workflow cases cover model moves/checks, shared transient state and
  Undo, and native-file/legacy-XML persistence (including zero comps).
- Installed `libs/version`, `libs/image`, `libs/brush`, `libs/impex`,
  `libs/ui`, `plugins/impex/libkra` and the docker. SHA-256 hashes of all
  seven installed DLLs match the build outputs.
- Changed C++ lines formatted with the configured clang-format; new C++
  files formatted completely. UI XML parses; touched tracked files pass
  `git diff --check`.
- The user subsequently confirmed operation was correct (2026-10-16), with
  a resize-spacing issue. This is a general confirmation, not an itemized
  report of every checklist case. The follow-up groups buttons in a compact
  horizontal layout and directs extra height to the list; the user confirmed
  this refinement on 2026-10-16 before starting Color Reference.
- Resize follow-up: `kritacompositiondocker` rebuilt and installed; installed
  DLL SHA-256 matches the build. Changed lines formatted and diff-check clean.
  No additional behavior tests were run for this layout-only adjustment.

2026-10-27 (null-document crash, found by test control; see
[Test control](test-control.md#known-limitations-and-pending-work)):

- `kritacompositiondocker` and `CompositionWorkflowTest` rebuilt on Linux;
  `KisLayerCompositionTest` and `CompositionWorkflowTest` pass offscreen
  (the workflow test now checks `get(nullptr, image)`). The docker was
  installed into `_install`.
- The crash was seen once, under gdb, while test control closed and
  created documents quickly; it was not reproduced again, so the fix is
  checked by the backtrace and by not regressing (all 26 test control
  scenarios pass twice). Checklist item 8 (two documents, two views,
  switching and closing) still needs a manual run.
