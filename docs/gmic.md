# G'MIC-Qt filters

Solstice includes G'MIC-Qt 3.7.4.1 in its Windows package from 0.2.0-beta.6. Its image
processing runs on the CPU, with OpenMP parallel processing where the filter
supports it. Enabling Solstice's GPU canvas does not move G'MIC computation
to the GPU.

## Using G'MIC

1. Select an editable paint layer and choose **Filter > Start G'MIC-Qt**.
2. Search or browse the filter list, select a filter and adjust its controls.
3. Use **Apply** to apply while keeping the dialog open, or **OK** to apply
   and close. Closing without applying leaves the image unchanged.
4. Use Solstice's normal **Undo/Redo** after returning to the canvas.

**Re-apply the last G'MIC filter** becomes available after a result has been
accepted through this window. Opening and cancelling the dialog alone does
not enable it. A new application session requires an application through
the full dialog before Re-apply is enabled.

The bundled catalog loads offline (691 filters in the verified fresh
profile). Some individual filters need additional data or external programs;
the catalog count is not a claim that every filter and external dependency
has been tested. Four standard data packs for color lookup tables, denoising,
light leaks and upscaling are included. Optional Internet updates remain
controlled by G'MIC's own settings.

## Supported scope

- Input modes: active visible paint layer, all paint layers (including hidden
  ones), active and the immediately adjacent visible paint layer below, or
  active and the immediately adjacent visible paint layer above. Group and
  vector layers are not flattened into paint layers automatically.
- Output replaces the input layers in place. A filter may change their number,
  dimensions, names, opacity, blend modes and offsets, as part of one undoable
  operation. Separate new-document/new-layer output destinations are disabled.
- Selections limit the replacement and preserve unselected pixels, including
  soft selection edges. Locked input layers cause the result to be rejected.
- The native bridge transfers planar floating-point pixels. RGB U8/U16/F16/F32
  is handled directly; other color models are converted through RGB. An
  individual filter can alter alpha, clip values or assume a particular color
  range. Universal HDR or color-profile preservation across every G'MIC filter
  is not guaranteed.
- G'MIC operates on paint layers; it is not a Solstice filter mask, filter
  layer or brush engine. Large operations can require substantial RAM.

The CPU bridge avoids two temporary full-image copies when returning a
filter result and converts RGB pixels directly to and from G'MIC's buffers.
This can reduce transfer overhead and temporary memory use, especially for
large images. It does not speed up the filter algorithm itself; the benefit
depends on image size, color depth and the selected filter.

G'MIC settings normally use `%APPDATA%/Solstice/config/gmic-qt.ini`; downloaded
definitions and data use `%APPDATA%/Solstice/resources/gmic`. The configuration
folder can fall back to Local AppData as described in [Settings folder](settings-folder.md).
A Solstice profile override applies, but choosing a custom Resource Folder
for brushes does not move G'MIC data. The pinned source, licenses and Solstice
build patches ship under `share/gmic/source`.

## Development status

The CPU runtime and CPU bridge optimizations (output-copy reduction and direct
RGB conversion) were confirmed working by the user on 2026-10-15 in the local
Windows development build. Automated checks cover an
actual filter application, Undo/Redo, offline dialog loading, repeated
launch/cancel, conversions and layer synchronization. The Windows cloud build
and package checks passed for 0.2.0-beta.6 on 2026-10-16; cloud GUI/runtime tests
were disabled. Linux/macOS runtime packaging remains unverified. See the
[dated package check](agent/github-actions.md#effects-layer-comps-and-gmic-package-check-2026-10-16).

Implementation and regression instructions:
[G'MIC maintenance reference](agent/gmic.md).
