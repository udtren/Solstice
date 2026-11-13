<p align="center">
  <img src="krita/data/splash/solstice-splash.png" alt="Solstice" width="480">
</p>

<p align="center"><b>A GPU-focused digital painting application for Windows, derived from Krita.</b></p>

<p align="center">
  <a href="https://github.com/udtren/Solstice/releases/latest"><img src="https://img.shields.io/github/v/release/udtren/Solstice?label=release" alt="Latest release"></a>
  <a href="COPYING"><img src="https://img.shields.io/badge/license-GPL--3.0-blue" alt="License: GPL-3.0"></a>
  <img src="https://img.shields.io/badge/platform-Windows%2011%20x64-lightgrey" alt="Platform: Windows 11 x64">
</p>

<p align="center">
  <b><a href="https://solstice.tidalboxstudio.com">Website</a></b> ·
  <b><a href="https://github.com/udtren/Solstice/releases/latest">Download</a></b> ·
  <b><a href="docs/README.md">User guides</a></b> ·
  <b><a href="docs/gpu-engine.md">GPU engine</a></b>
</p>

![Solstice main window painting the Solstice splash artwork, with the Brush Presets docker grouped by bundle, Quick Brush Adjustments, HueSVC, the Quick Access Palette, Layers and the Brush section of Tool Options](docs/images/solstice-main-window.webp)

**Solstice is an open-source digital painting application derived from the
Krita 6 codebase.** The [website](https://solstice.tidalboxstudio.com) has an
overview, the download page and pages for the main features; this repository
holds the source code, the releases and the [user guides](docs/README.md).

<table>
<tr>
<td width="33%" valign="top">
<h3>⚡ GPU acceleration</h3>
<p>Vulkan-powered layer compositing and canvas updates, and a GPU brush, on by default on NVIDIA Blackwell graphics cards: from pen input to the displayed frame, about four times faster than the CPU brush.</p>
</td>
<td width="33%" valign="top">
<h3>🎨 Workflows from Clip Studio Paint</h3>
<p><a href="docs/puppet-warp.md">Puppet Warp</a> with movable and rotatable pins, eye marks that put brush settings in <a href="docs/brush-editor.md#brush-options-in-tool-options">Tool Options</a>, and a <a href="docs/brush-stroke-layer.md">Brush Stroke Layer</a> that redraws its strokes sharply when scaled, like a vector layer.</p>
</td>
<td width="33%" valign="top">
<h3>🛠️ Krita, refined</h3>
<p><a href="docs/brush-stroke-preview.md">Brush Presets</a> with stroke previews, filters and grouping, a reworked <a href="docs/brush-editor.md">Brush Editor</a>, <a href="docs/photoshop-brushes.md">Photoshop brush import</a> with presets, textures and folders, <a href="docs/docker-locks.md">docker locks</a>, a live <a href="docs/overview-live-update.md">Overview</a> and a modernized <a href="docs/ui-modernization.md">interface</a>, together with native tools such as <a href="docs/quick-access.md">Quick Access</a> and the <a href="docs/asset-library.md">Asset Library</a>.</p>
</td>
</tr>
</table>

Solstice is independently maintained and desktop-only. It is not an official
Krita edition; upstream Krita changes are adopted selectively as needed. See
[Versions](docs/versioning.md) for its version numbering.

**Learning Solstice:** for the basics of using the application, see the
[Krita User Manual](https://docs.krita.org/en/user_manual.html). The
features Solstice adds are described in its [user guides](docs/README.md).

## Contents

- [What's new in 1.1.1](#whats-new-in-111)
- [Highlights](#highlights)
  - [New Features](#new-features)
  - [Ported Plugins](#ported-plugins)
  - [Other Major Changes](#other-major-changes)
- [Development and downloads](#development-and-downloads)
- [Differences from Krita](#differences-from-krita)
- [GPU engine](#gpu-engine)
- [Benchmarks](#benchmarks)
- [Supported environment](#supported-environment)
- [Relationship to Krita](#relationship-to-krita)

## What's new in 1.1.1

[Solstice 1.1.1](https://github.com/udtren/Solstice/releases/tag/v1.1.1) is a
performance and fix release for the CPU side. Finding a layer's bounds, at
the start of a Move or Transform for example, skips empty tiles (all layers
of one large painting: 3477 ms before, 72 ms now; see
[Benchmarks](#benchmarks)). Solstice starts faster because the Text
Properties docker loads when it is first shown, and it does less work while
idle. The resource cache database no longer keeps a copy of each preset's
settings in its thumbnails and is compacted at startup. **Reload Original
Preset** no longer reloads the stroke previews of all brushes.

It builds on [1.1.0](https://github.com/udtren/Solstice/releases/tag/v1.1.0),
which lets each [Layer Comp](docs/layer-comps.md) be exported with its own
[Fast Image Export](docs/lazy-tools.md) settings and added **Show in File
Explorer** to the Recent Images and [Asset Library](docs/asset-library.md)
menus, and on [1.0.0](https://github.com/udtren/Solstice/releases/tag/v1.0.0),
which moved more of the layer stack to the [GPU engine](docs/gpu-engine.md)
and starts the engine by itself only on NVIDIA Blackwell.

## Highlights

### New Features

<table>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/gpu-engine.md"><img src="docs/images/gpu_engine.png" alt="The GPU Engine (Vulkan) section of Settings > Performance, with the GPU engine and GPU brush options enabled" width="100%"></a>
<p><b><a href="docs/gpu-engine.md">GPU Engine</a></b><br>Vulkan layer compositing and a GPU brush, both on by default on NVIDIA Blackwell.</p>
</td>
<td width="50%" align="center" valign="top">
<a href="docs/brush-stroke-preview.md"><img src="docs/images/brush-presets.png" alt="Brush Presets grouped by bundle, with the Engines filter open" width="100%"></a>
<p><b><a href="docs/brush-stroke-preview.md">Brush Presets</a></b><br>Stroke previews, engine and bundle filters, and grouping.</p>
</td>
</tr>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/brush-editor.md#brush-options-in-tool-options"><img src="docs/images/tool-options-brush.png" alt="Brush Editor with eyes on brush settings, and the Brush section of Tool Options showing them under the stroke preview" width="100%"></a>
<p><b><a href="docs/brush-editor.md#brush-options-in-tool-options">Brush options in Tool Options</a></b><br>Eye marks put Brush Editor settings in Tool Options.</p>
</td>
<td width="50%" align="center" valign="top">
<a href="docs/clipping-mask.md"><img src="docs/images/clipping_mask.png" alt="A character painted inside an oval; in the Layers docker, Group 24 carries the orange clipping icon and bar and is clipped to the oval Paint Layer 46 below it" width="100%"></a>
<p><b><a href="docs/clipping-mask.md">Clipping Mask</a></b><br>Clip a layer to the layer or group directly below it, without a group.</p>
</td>
</tr>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/puppet-warp.md"><img src="docs/images/puppet_warp.png" alt="A painted figure and a copy reposed with Puppet Warp, with pins on the Transform Tool's mesh and the Puppet Warp Tool Options" width="100%"></a>
<p><b><a href="docs/puppet-warp.md">Puppet Warp</a></b><br>Repose artwork with movable, rotatable pins in the Transform Tool.</p>
</td>
<td width="50%" align="center" valign="top">
<a href="docs/brush-stroke-layer.md"><img src="docs/images/brush-stroke-layer.png" alt="A small figure sketch and a copy enlarged and drawn again at the new size, on two Brush Stroke Layers" width="100%"></a>
<p><b><a href="docs/brush-stroke-layer.md">Brush Stroke Layer</a></b><br>Strokes are redrawn at the new size when scaled, without blurring.</p>
</td>
</tr>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/effects-filters.md"><img src="docs/images/more_filter.png" alt="Nine versions of one portrait with Solstice effects such as Chromatic Aberration, Color Filter, Crystal, Drop Shadow, Color Reference and Film Grain" width="100%"></a>
<p><b><a href="docs/effects-filters.md">Effects &amp; Color</a></b><br>New native filters such as Film Grain, Glow and Color Reference.</p>
</td>
<td width="50%" align="center" valign="top">
<a href="docs/move-tool.md#duplicating-a-selection"><img src="docs/images/duplicate_in_same_layer.gif" alt="A four-pointed star on Paint Layer 1 is selected and Ctrl-dragged twice with the Move Tool; two copies are placed on the same layer, and the Layers docker still lists only Paint Layer 1 and Background" width="100%"></a>
<p><b><a href="docs/move-tool.md#duplicating-a-selection">Duplicate on the same layer</a></b><br>Ctrl-drag a selection with the Move Tool to place a copy on the same layer.</p>
</td>
</tr>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/reference-tool.md"><img src="docs/images/reference-tool-filters.png" alt="A selected reference drawing with Desaturate, Posterize and Glow in the Filter menu; Glow offers enable, edit, reorder and remove controls" width="100%"></a>
<p><b><a href="docs/reference-tool.md">Reference Tool filters</a></b><br>Non-destructive filter stacks and templates for reference images.</p>
</td>
<td width="50%" align="center" valign="top">
<a href="docs/remove-dust.md"><img src="docs/images/remove_dust.png" alt="A rectangle drawn with the Remove Dust Tool around specks on a sketch, and the same area afterwards without the specks" width="100%"></a>
<p><b><a href="docs/remove-dust.md">Remove Dust</a></b><br>Remove or select small specks, and fill small gaps, with a tool or a filter.</p>
</td>
</tr>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/layer-comps.md"><img src="docs/images/Layer_Comps.png" alt="The Fast Image Export dialog for phantom.kra with three saved exports, the first exporting the layer comp eye_closed, beside the Layer Comps docker with the comps eye_closed, eye_open and eye_open_tear" width="100%"></a>
<p><b><a href="docs/layer-comps.md">Layer Comps</a></b><br>Record, apply and export layer visibility states, each with its own export settings.</p>
</td>
<td width="50%" align="center" valign="top">
<a href="docs/through-layer-eraser.md"><img src="docs/images/through-layer%20eraser%20tool.gif" alt="A stroke of the Through-Layer Eraser Tool removing a corner of a red frame and a purple block painted on separate layers, with their Layers docker thumbnails updating" width="100%"></a>
<p><b><a href="docs/through-layer-eraser.md">Through-Layer Eraser</a></b><br>Erase through all visible layers, or the selected layers, with one stroke.</p>
</td>
</tr>
</table>

### Ported Plugins

Python plugins reimplemented natively in C++, with no Python installation
required.

<table>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/quick-access.md"><img src="docs/images/quick_access_manager/1_overview.png" alt="The Quick Brush Adjustments, HueSVC and Quick Access Palette dockers, and two pages of the Gesture Configuration dialog" width="100%"></a>
<p><b><a href="docs/quick-access.md">Quick Access Manager</a></b><br>Brush and action palettes, a color selector, brush adjustments and gestures.</p>
</td>
<td width="50%" align="center" valign="top">
<a href="docs/lazy-tools.md"><img src="docs/images/lazy_tool/cover.png" alt="The Fast Image Export dialog with three saved exports and the Layer comp setting, the Rename Layer dialog, the General &gt; Custom settings page and the Selection Masks dialog" width="100%"></a>
<p><b><a href="docs/lazy-tools.md">Lazy Tools</a></b><br>Quick layer renaming, selection masks, fast image export with saved exports per document and layer comp, and color shortcuts.</p>
</td>
</tr>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/asset-library.md"><img src="docs/images/ported_plugin/asset_library.png" alt="The Asset Library docker showing .kra character thumbnails grouped by folder" width="100%"></a>
<p><b><a href="docs/asset-library.md">Asset Library</a></b><br>Browse and open your image folders as thumbnails.</p>
</td>
<td width="50%" align="center" valign="top">
<a href="docs/rest-note.md"><img src="docs/images/ported_plugin/rest_note.png" alt="The Rest Note docker showing a working timer and the next eye break" width="100%"></a>
<p><b><a href="docs/rest-note.md">Rest Note</a></b><br>A work timer with eye-break and break reminders.</p>
</td>
</tr>
<tr>
<td width="50%" align="center" valign="top">
<a href="docs/vision-ml.md"><img src="docs/images/ported_plugin/vision_tool.png" alt="The Background Removal filter dialog with the BiRefNet model and CPU and GPU backends" width="100%"></a>
<p><b><a href="docs/vision-ml.md">Vision Tools</a></b><br>On-device AI selection, background removal and Smart Fill.</p>
</td>
<td width="50%"></td>
</tr>
</table>

### Other Major Changes

- **[Asset Library on the welcome page](docs/asset-library.md)**: browse and
  open assets from the welcome page before opening a document.
- **[Popup Palette removed](docs/quick-access.md)**: the right-click Popup
  Palette and the On-Canvas Brush Editor are replaced by Quick Access.
- **Native Export Region**: the export feature of the Export Region plugin is
  now built in as **File > Export Region...** (`Alt+Shift+E`), which exports
  the selection or the active layer's bounds.
- **[At least 1 pixel](docs/brush-minimum-one-pixel.md)**: a Freehand Brush
  option, on by default, that keeps thin, pressure-tapered lines at least
  1 pixel wide.
- **Performance improvements and bug fixes** across the application.

See the [user guides](docs/README.md) for every Solstice feature.

## Development and downloads

> **Experimental software.** The Vulkan engine is on by default on NVIDIA
> Blackwell graphics cards (on others it has to be allowed in the settings)
> and can be turned off. It does not yet replace every CPU processing path, and its
> supported hardware and platforms are limited.

Download the unsigned Windows x64 ZIP from
[the latest GitHub release](https://github.com/udtren/Solstice/releases/latest),
extract it and start `Solstice.exe`.

Source code and newer Windows x64 trial ZIPs are also available from successful
manual [GitHub Actions builds](https://github.com/udtren/Solstice/actions/workflows/windows-build.yml).
These temporary artifacts expire after seven days and are development builds,
not stable releases. See [Experimental Windows builds](docs/test-builds.md)
for download instructions and packaging limitations. To show `.kra`
thumbnails in Explorer, see [Explorer thumbnails](docs/windows-shell-thumbnails.md).
The application and document icons are described in
[Visual branding](docs/visual-branding.md).
For automated UI testing through MCP, see [Test control](docs/test-control.md).

The primary development and default branch is `main`. For source
builds, see the [development environment](AGENTS.md#shared-development-environment) and [build workflow](docs/agent/development-workflow.md#build).
Check the [supported environment](#supported-environment) before building.

## Differences from Krita

Solstice follows its own development direction, centered on an experimental
Vulkan image engine and a separately maintained native feature set. Selected
Python-based extensions have been reimplemented in C++, with compatibility
for existing settings where practical. The Asset Library is also available
from the welcome page, and Puppet Warp is integrated into the Transform Tool.

Android support and its build and packaging infrastructure have been removed.
The right-click Popup Palette and the On-Canvas Brush Editor have been removed
as well.
Solstice does not merge upstream Krita wholesale; upstream changes are
reviewed regularly and adopted selectively as needed. Solstice-specific
changes and support are maintained in this repository.

## GPU engine

The experimental engine uses Vulkan compute on RGBA 32-bit and 16-bit float
documents and shares its data with the OpenGL canvas for display. It is
enabled by default on NVIDIA Blackwell graphics cards; anything it does not
support runs on the CPU as in Krita. This is an ongoing rewrite, not a fully
GPU-based painting pipeline.

- **On the GPU:** layer compositing and the canvas display in nearly every
  blend mode; the pixel brush in Buildup and Wash, with image tips, pattern
  textures, selections, mirror painting and masking brushes; moved layers
  and groups, transparency masks and the outside Stroke layer style;
  Free Transform, Liquify and Puppet Warp; Gaussian Blur; filter layers
  and masks with Desaturate, and in 16-bit float with Posterize, channel
  curves or Color Reference.
- **Still on the CPU:** every other color space and depth (8/16-bit integer,
  CMYK, Lab, grayscale), other brush engines, most filters, Perspective,
  Warp, Cage and Mesh transforms, other layer style effects, and the
  preview while editing a transform.

Free Transform and Liquify results are identical to the CPU results, pixel
for pixel; Gaussian Blur matches within one rounding step.

See the [GPU Engine guide](docs/gpu-engine.md) for the complete lists of
[what runs on the GPU](docs/gpu-engine.md#what-runs-on-the-gpu) and
[what stays on the CPU](docs/gpu-engine.md#what-stays-on-the-cpu), setup and
limitations.

## Benchmarks

**These numbers measure specific engine operations, not overall application
performance.** They are not frame rates. Measured with an AMD Ryzen 9 9950X
and an NVIDIA RTX PRO 6000 Blackwell (driver 596.86), using local development
builds; results vary with workload and hardware.

CPU against GPU, RGBA 32-bit float (2026-10-11):

| Workload | CPU | GPU | |
| --- | ---: | ---: | ---: |
| Layer projection, 4096 x 4096, 16 layers | 270 ms | 60.6 ms | 4.5x |
| Transform Tool apply, scale + rotate, 2480 x 3508 | 502 ms | 61.5 ms | 8.2x |
| Liquify apply, 20 strokes, 2480 x 3508 | 253 ms | 63.5 ms | 4.0x |
| Gaussian Blur apply, radius 30, 2480 x 3508 | 212 ms | 68.0 ms | 3.1x |
| Pen input to displayed frame, 256 px Buildup (2026-10-10) | 20.6 ms | 4.8 ms | 4.3x |

Updates of the layer stack with the GPU engine, before and after 1.0.0
moved them to the GPU (2480 x 3508, 2026-11-12):

| Update | Before | 1.0.0 | |
| --- | ---: | ---: | ---: |
| Move Tool step, full-canvas layer | 166 ms | 8.5 ms | 19x |
| Dab on a layer with a 1 px Stroke layer style | 8.95 ms | 0.24 ms | 37x |
| Dab on a layer with a transparency mask | 2.19 ms | 0.31 ms | 7x |
| Dab below a Posterize filter layer (16-bit float, 1754 x 2480) | 11.5 ms | 0.30 ms | 38x |

On the CPU, for every color space (2026-11-13):

| Operation | Before | Now | |
| --- | ---: | ---: | ---: |
| Finding the bounds of all 26 layers of a 5319 x 4705 RGBA8 painting | 3477 ms | 72 ms | 48x |

**A complete short stroke with a small brush can still be slower on the
GPU** (6.2 ms on the CPU against 10.9 ms for a 64 px Wash stroke); large,
textured, masked or mirrored strokes favor the GPU. Input-to-display times
end at Qt's frame swap, not at light leaving the screen.

See [all results, how they were measured and their limits](docs/gpu-engine.md#current-benchmarks).

## Supported environment

- **Primary tested platform:** Windows 11, 64-bit desktop.
- **GPU engine target:** NVIDIA Blackwell GPUs (GeForce RTX 50 series and
  RTX PRO Blackwell). Validation and benchmarks use an RTX PRO 6000 Blackwell;
  other GPUs are untested, and the engine is used on them only after it is
  allowed in the settings.
- **Graphics APIs:** Vulkan 1.3 and desktop OpenGL with Vulkan/OpenGL external
  memory and semaphore interoperability for the accelerated canvas path.
  ANGLE/Direct3D does not provide that canvas path.
- **Validation setup:** NVIDIA driver 596.86, Qt 6.8 and desktop OpenGL 4.6.
- **Other platforms:** Linux and macOS are not currently validated for this
  custom build. Android is not supported.
- **Interface languages:** English and Simplified Chinese cover everything,
  including Solstice's own features (see [Interface languages](docs/languages.md));
  other languages use Krita's translations, with Solstice's features in
  English.

The GPU engine has narrower requirements than Krita itself.
See the [GPU Engine guide](docs/gpu-engine.md#requirements) for the supported document
formats and fallback behavior.

## Relationship to Krita

**Solstice is not affiliated with, endorsed by, sponsored by, or supported by
the Krita project or KDE.** It is independently maintained and is not an
official Krita edition. Its source-code origins do not imply an organizational
relationship with those projects.

Please report Solstice-specific issues to this repository, not to the Krita
or KDE issue trackers. Credit for the original application belongs to Krita's
contributors. Existing copyright notices and license terms remain applicable;
see [COPYING](COPYING) and the notices in individual files.
