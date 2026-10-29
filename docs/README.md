# Solstice user guides

Guides to the features Solstice adds to Krita. For the basics of using the
application, and everything else Solstice shares with Krita, see the
[Krita User Manual](https://docs.krita.org/en/user_manual.html).
Technical notes and document roles are in the [agent guide](agent/README.md).
The Markdown sources in this repository are the maintained originals.

[0.6.1](https://github.com/udtren/Solstice/releases/tag/v0.6.1) fixes the
layer thumbnails of group and filter layers pasted from another document
partly outside the canvas, and middle-clicking a document tab closing other
documents. It builds on
[0.6.0](https://github.com/udtren/Solstice/releases/tag/v0.6.0), which added
[Clipping Mask](clipping-mask.md) and
[duplicating a selection on the same layer](move-tool.md), fixed the
[HueSVC](quick-access.md#huesvc) color selector, and adopted the upstream
Krita clipboard rewrite.

## Getting started

- [Trial builds](test-builds.md): download, extract and start Solstice.
- [Settings folder](settings-folder.md): where Solstice keeps its settings
  and resources, and importing a Krita profile.
- [Versions](versioning.md): Solstice's version number, and the News area
  of the welcome page.
- [Interface languages](languages.md): switching the language, and the
  Simplified Chinese translation. Added in **0.2.0-beta.7**.
- [Explorer thumbnails](windows-shell-thumbnails.md): previews of `.kra`
  and `.krz` files in Windows Explorer.
- [Test control](test-control.md): letting an MCP client such as Claude
  Code draw on the canvas and read back the result, for testing. Early test
  version.

## Painting and brushes

- [Reference Tool](reference-tool.md): editable filter stacks and reusable
  templates, added in 0.4.0. [Detailed filter guide](reference-image-filters.md).

- [GPU engine](gpu-engine.md): the Vulkan engine and the GPU brush, what
  runs on the GPU, and measured results.
- [Brush Presets](brush-stroke-preview.md): stroke previews, engine and
  bundle filters, and grouping.
- [Brush Editor](brush-editor.md): editing brush options, and showing them
  in Tool Options with the eye marks.
- [Brush Stroke Layer](brush-stroke-layer.md): a layer that redraws its
  strokes when scaled.
- [Photoshop brushes](photoshop-brushes.md): importing `.abr` files.

## Editing

- [Clipping Mask](clipping-mask.md): clip a layer to the layer below it
  without a group. Added in 0.6.0.
- [Puppet Warp](puppet-warp.md): posing and reshaping artwork with pins in
  the Transform Tool.
- [Move Tool](move-tool.md): duplicating a selection on the same layer with
  Ctrl-drag. Added in 0.6.0.
- [Remove Dust](remove-dust.md): removing or selecting small specks and
  filling small holes, with a tool or a filter. Added in 0.5.0.
- [Vision ML](vision-ml.md): AI-assisted selection, Smart Fill and
  background removal, run on your computer.

## Effects and color

- [Effects & Color](effects-filters.md): illustration filters for color,
  lighting, texture and line-art extraction, including Color Reference and
  extended Lens Blur. Added in **0.2.0-beta.6**.
- [G'MIC-Qt](gmic.md): a separate CPU filter dialog, input/output modes
  and supported scope.

## Workflow and interface

- [Layer Comps](layer-comps.md): save, switch and export layer visibility
  variations. Added in **0.2.0-beta.6**.
- [Tools when switching documents](document-tool-state.md): tool restoration
  when moving between canvases with a mouse, pen, or eraser.
- [Quick Access Manager](quick-access.md): the Quick Access Palette of
  brushes, actions and dockers, keyboard-and-mouse gestures, Quick Brush
  Adjustments with temporary keys and brushes, and the HueSVC selector.
- [Asset Library](asset-library.md): browsing asset folders and inserting
  files as layers.
- [Rest Note](rest-note.md): a work timer with eye-break and full-break
  reminders.
- [Lazy Tools](lazy-tools.md): layer color labels, preset layer names,
  saved selections, Fast Image Export and color shortcuts. Extended in 0.5.1.
- [Docker locks](docker-locks.md): locking the size of docked dockers.
- [Overview live update](overview-live-update.md): the Overview follows the
  canvas while you paint.
- [Solstice interface](ui-modernization.md): fonts, theme and widget style.
- [Visual branding](visual-branding.md): the application and document
  icons.
