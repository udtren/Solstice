# Reference Image Filters — development notes

## Status and scope

Native ordered non-destructive effects on one reference image at a time.
[User guide](../reference-image-filters.md). On 2026-10-17, `kritaui` and
`krita_flaketools` built and the dedicated regression test passed (ordering,
XML/legacy/clone isolation, asynchronous invalidation and destruction).
Documentation checks and msgfmt passed (10,443 translated messages).
Rebuilt shared libraries, the tool plugin and Chinese catalog were installed
to the shared test prefix on 2026-10-17.
Interactive acceptance remains pending.
The user accepted the filter/template enhancement on 2026-10-17 and supplied
the screenshot used in the [Reference Tool overview](../reference-tool.md).
This confirms that demonstrated workflow, not every regression checklist item.
G'MIC and multi-image editing are outside scope.

Template extension verified 2026-10-17: native targets and the expanded
KisReferenceImageFiltersTest pass, including reload/reuse, independent image
copies, overwrite protection, deletion, safe names and unsupported-file handling.
Documentation checks and msgfmt pass (10,459 translated messages). Shared UI,
tool plugin and Chinese catalog installed; interactive acceptance is pending.

## Source locations

- `libs/ui/KisReferenceImage.*`: model, XML, asynchronous rendering/cache.
- `libs/ui/KisReferenceImageFilters.*`: modal editor, context menu, undo.
- `libs/ui/KisReferenceFilterTemplates.*`: profile-wide named stack persistence.
- `plugins/tools/defaulttool/referenceimagestool/ToolReferenceImages.cpp`: hook.
- `libs/ui/tests/KisReferenceImageFiltersTest.cpp`: rendering and persistence.
- `libs/ui/widgets/kis_filter_selector_widget.cc`: explicit configurations take
  precedence over remembered presets when reopening an existing effect.

## Architecture and lifecycle

The original QImage is unchanged. Each entry stores filter id, configuration
XML and enabled flag. Resource snapshots are prepared on the GUI thread.
QtConcurrent jobs own source/configurations and never access shapes or UI.
A shape-owned QFutureWatcher adopts results on the GUI thread; its destruction
disconnects callbacks while independent workers can finish safely. One job
per shape runs; revision mismatches discard stale results. Repaint schedules
the latest state. Preview overrides are separate from saved settings (including
autosave). Modal editing retains the reference layer and commits one undo
command. Clones clear caches to avoid inheriting transient previews.

## Configuration and compatibility

`referenceimage/solstice-filters` stores `enabled`. Ordered `effect` children
store `id`, `enabled` and escaped configuration XML text. KRA and KRF both use
the shared serializer. Missing stack means empty/enabled. Unknown ids and
configurations survive. Embedded PNGs remain original; linked paths retain
existing behavior. No new clipboard format or resource bundle.

### Templates

`KisSolsticePaths::configDir()/reference-image-filter-templates` holds one JSON
file per named stack. Version 1 stores `version`, `name`, `enabled`, and ordered
`effects` (`id`, configuration XML string `configuration`, `enabled`). Names
are trimmed, Unicode-normalized and case-folded before SHA-256 filename hashing;
labels never become paths. QSaveFile commits atomically; a per-file QLockFile
guards save/delete. Existing names require confirmed overwrite. Invalid or
unsupported files are preserved but omitted from enumeration. Load failures
leave outputs unchanged. Unknown filter ids/configuration strings survive.

Each menu opening and manager apply reloads disk state, sharing templates across
windows/documents. Apply copies the stack through the existing undo command or
manager transaction. Explicit saves in the manager are profile writes independent
of image Cancel/Undo. Pixels, placement, Saturation and Opacity are excluded.

## Invariants

- Source, shape size, transforms and z-order are unchanged.
- Stack then Saturation then Opacity; crop each stage to the source rectangle.
- Only native mask-compatible configurations run. Full-resolution preview and
  final processing share the same path; sampling uses the displayed cache.
- Workers never dereference UI, documents or shapes.

## Build, test and install

Use `development-workflow.md`: build `kritaui`, `krita_flaketools` and
`KisReferenceImageFiltersTest`; run CTest matching the latter. Install `libs/ui`
and `plugins/tools/defaulttool` plus rebuilt shared dependencies. Restart.

## Manual regression checklist

1. Add Levels, Gaussian Blur, Posterize and another Levels; edit/reorder.
2. Toggle individual effects and bypass; clear; undo/redo each operation.
3. Change settings rapidly, cancel, reopen; ensure stale jobs cannot win.
4. Save/reopen KRA/KRF, linked/embedded, missing ids, autosave during preview.
5. Move/rotate/scale, zoom, sample colors including transparent edges.
6. Delete images/close documents during processing; undo deletion.
7. Multiple selection disables Filter; check two views and documents.
8. Check Simplified Chinese labels and dialog layout.
9. Save and apply across images/documents, undo/redo, restart and reapply.
   Overwrite/delete templates; previously applied images must stay independent.
10. Save/apply in the manager then Cancel: explicit template saves persist,
    while cancelled image edits do not.

## Known limitations

No new clipboard stack, resource bundling, G'MIC or intermediate-stage cache.
Processing failures fall back to source and log a warning; no error badge yet.
Interactive verification remains required.
