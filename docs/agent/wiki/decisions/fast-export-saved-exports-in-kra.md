---
type: decision
updated: 2026-11-13
sources:
  - docs/agent/lazy-tools.md (saved exports)
  - libs/ui/lazytools/KisLazyToolsFastExportPresets.cpp
  - "plugins/impex/libkra/kis_kra_tags.h: SOLSTICE_FAST_EXPORT_PATH"
  - libs/ui/KisDocument.cpp (copyFromImpl)
  - "libs/image/kis_layer_composition.cpp: uuid() (2026-11-13)"
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

## Layer comps (2026-11-13)

**Decision (user, 2026-11-13).** To give each Layer Comp its own export
settings, a saved export refers to a comp (option B of that day's proposal),
rather than a comp referring to one saved export (A) or the Layer Comps
docker keeping its own settings (C). The user accepted the recommended
defaults: several saved exports per comp, `<document>_<comp>` as the default
name, the docker's Export check boxes and Export Images button left separate,
a deleted comp reported and skipped, a comp id saved in the `.kra`, and no
docker context menu or bulk creation for now.

**Why.** B reuses everything saved exports already have (the run, the
conflict check, Export Checked/All, the two actions, `.kra` storage) and lets
one comp have a PNG and a JPEG. A ties a comp to one setting and C would
duplicate the format, folder and name UI and a second export path. Comps had
no identity but their name, which can repeat and change, hence the id.

**How to apply.**
- Refer to a comp by `KisLayerComposition::uuid()` and keep its name as a
  fallback; Krita drops the id on save. Never fall back to an ambiguous name.
- Apply a comp only to a snapshot (`KisImage` copies keep the ids), never to
  the live image: the canvas, undo history and the docker's state stay as
  they are. A comp may hide the selection mask, so take a selection from the
  live document.
- Details: [Lazy Tools](../../lazy-tools.md), "Layer comps in saved exports".
