---
type: pitfall
updated: 2026-10-30
sources:
  - libs/ui/KisImportExportManager.cpp (convert, askUserAboutExportConfiguration)
  - libs/libkis/Document.cpp (exportImage, setBatchmode)
  - libs/libkis/Krita.cpp (setBatchmode)
  - plugins/impex/png/kis_png_export.cc (KisWdgOptionsPNG::setConfiguration)
related:
  - ../../lazy-tools.md
---

# Exported options are remembered

`KisImportExportManager::convert()` saves the export configuration to
`ExportConfiguration-<mime>` after every export of a document that is not in
batch mode, and File > Export starts from that key. A script's
`Document.exportImage(path, config)` on a non-batch document therefore:

1. shows the format's options dialog, prefilled from the script's `config`;
2. on OK, saves the dialog's whole configuration as the user's options.

The Spine Export plugin wrote `template.png` this way with
`forceSRGB=False`, and "Force convert to sRGB" stayed off for later
exports. The user connected it with downloading new versions (2026-10-30);
`krita.log` showed it was the plugin.

Rules:

- A script that passes its own configuration sets the document's batch mode
  around the export: `document.setBatchmode(True)`, restored afterwards.
  No dialog opens and nothing is saved.
- `Krita.setBatchmode()` is not the same switch. It applies to documents
  opened through `Krita.openDocument()` and to `Node.save()`'s temporary
  document, not to a document already open.
- A dialog's fallback for a missing key must match `defaultConfiguration()`
  and `convert()`. A configuration passed in by a caller is not merged with
  the defaults, so the fallback is what the user sees and then saves.
- To trace a changed option, read `krita.log`: each export logs its
  configuration, and each session its version and executable.
