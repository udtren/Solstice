---
type: decision
updated: 2026-11-03
sources:
  - docs/agent/lazy-tools.md (saved exports)
  - libs/ui/lazytools/KisLazyToolsFastExportPresets.cpp
  - "plugins/impex/libkra/kis_kra_tags.h: SOLSTICE_FAST_EXPORT_PATH"
  - libs/ui/KisDocument.cpp (copyFromImpl)
related: []
---

# Fast Image Export: saved exports live in the `.kra`

**Decision (user, 2026-11-03).** A document's saved Fast Image Export
settings are stored inside its `.kra`, as the root entry
`solstice/fastexport.xml`, not in `kritarc` keyed by path and not in a
sidecar file. The user accepted all recommendations of the proposal of that
day: the `.kra` storage, session-only presets for other formats, presets that
carry their format options and run without the options dialog, both "run all"
and "run checked" actions, the selection read at run time, document or fixed
names, no overwrite prompt and no folder creation (only a conflict check),
modified flag without undo for every edit (check boxes included), unsaved
documents still skipped, no cross-document templates yet, and the list in the
existing dialog.

**Why.** The settings belong to the artwork: they must survive renaming,
moving and copying the file to another computer. A path key in `kritarc`
silently loses them on rename; a sidecar breaks when only the `.kra` is moved
and would show up inside a game project. A separate store entry instead of
new `maindoc.xml` elements keeps Krita's main schema untouched, so upstream
intake does not conflict, and Krita and older Solstice simply ignore it.

**How to apply.**
- The entry is a compatibility contract: change its XML only with
  backward-compatible reading (`fromXml()` ignores what it does not know).
- Anything stored on `KisDocument` for saving must be copied in
  `Private::copyFromImpl()`: background saving and snapshots save a clone.
- Saving a `.kra` in Krita or an older Solstice drops the presets; PNG/JPEG
  documents cannot keep them. Both are documented for users.
- Running presets must not show the format options dialog or touch the
  source document's batch mode: export a `lockAndCreateSnapshot()` clone or
  a region document in batch mode instead.
