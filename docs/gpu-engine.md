# GPU Engine (experimental)

The GPU Engine is an ongoing rewrite of Solstice's image engine so that layer
compositing, filters, transforms, and painting run on the graphics card with
Vulkan instead of on the CPU. It is developed on the `krita-sol-gpu` branch.

## Current status

Layer compositing and the canvas display run on the GPU for RGBA floating
point documents. Painting normally runs on the CPU; an opt-in prototype can
composite supported pixel-brush blend modes on the GPU in RGBA32F
documents, including the temporary painting buffer used by Wash mode.
Filters and transforms still run on the CPU. The engine is off by default.

Accelerated layer blend modes include Normal, Multiply, Screen, Addition /
Linear Dodge, Subtract, Darken, Lighten, Difference, Overlay, Hard Light,
Exclusion, Linear Burn, Linear Light and Pin Light in RGBA32F and RGBA16F.
The same formats also accelerate Soft Light (SVG and Photoshop), Color Dodge,
Color Burn, Divide, Vivid Light, Hard Mix (including Photoshop and Softer
Photoshop), Grain Merge/Extract, Negation, Allanon, and the HSY Hue, Saturation,
Color, Luminosity, Darker Color and Lighter Color modes. The separately named
HDR variants of Dodge, Vivid Light and Hard Mix still use the CPU, as do the
HSI/HSL/HSV families.
Other layer blend modes continue through the CPU path.

Layer compositing also stays on the GPU when individual RGB channels are
disabled in the layer properties, with or without alpha locked, in both
RGBA32F and RGBA16F documents. The supported blend modes retain the CPU's
channel-preservation behavior. For unrestricted Normal blending, fully
transparent pixels retain their original hidden RGB; CPU SIMD processing
can produce different hidden RGB at zero alpha, without changing appearance.

Bulk pixel reads from GPU-backed documents now transfer tiles in batches,
including channel-by-channel reads used by some CPU consumers. This reduces
transfer waits when those paths need current GPU pixels. Filters and file
encoding still run on the CPU.

CPU-to-GPU transfers share temporary buffers across layers and use memory
suited to CPU snapshots. Completed transfers also release resources without
holding the shared cleanup lock. This reduces waits when an image first
enters the GPU path or returns to it after CPU processing.

Large textured brushes now reuse available GPU transfer buffers when the
memory limit prevents keeping all three buffers. Masked Brush also batches
GPU pixel reads before its CPU mask-compositing step. These changes reduce
avoidable waits, but texture generation and masking remain CPU operations;
large or densely spaced strokes can still take time to catch up.

Automated checks compare complete strokes and their layer projections with
CPU drawing, including Buildup/Wash, mirror painting, selections, alpha lock
and Undo/Redo. Supported Wash previews and final merges, including
selections and channel locks, can stay on the GPU with aligned tiles. Other Wash
previews and final merges batch GPU readbacks
to reduce transfer waits. Short-stroke measurements still show cases where the CPU is
faster. Tablet input and the time until a stroke appears on screen still need
separate measurement.

The [current benchmarks](#current-benchmarks) distinguish layer compositing,
canvas preparation and complete queued strokes. Faster compositing alone
does not establish lower pen-to-screen latency.

## Current benchmarks

Measured on October 4, 2026, in the local development build: Windows 11,
AMD Ryzen 9 9950X, RTX PRO 6000 Blackwell, driver 596.86 and Qt 6.8.
Vulkan validation was disabled for timing. Three fresh processes were run
sequentially for each workload. Values below are the median of their results;
ranges in parentheses show variation between processes, not latency percentiles.

| Engine operation | CPU | GPU |
| --- | ---: | ---: |
| Resident projection, 4096x4096 RGBA32F, 16 layers | 247 ms (242-248) | 56.6 ms (55.4-57.1) |
| Full canvas preparation, 4096x4096 RGBA32F, 8 layers | 1060 ms (1038-1115) | 1.62 ms (1.43-1.80) |
| 256x256 canvas update in that image | 5.36 ms (5.23-5.84) | 0.156 ms (0.133-0.156) |
| Four mirror passes, 14 dabs of 73x73 pixels | 0.958 ms (0.956-1.007) | 0.381 ms (0.352-0.384) |
| Four mirror passes, 32 dabs of 256x256 pixels | 45.14 ms (43.63-48.07) | 2.02 ms (1.96-2.04) |

Projection and canvas use five-sample medians per process. Mirror results
average five updates after warming all three brush staging slots. All GPU
times include completion. Canvas preparation starts with fresh GPU-resident
projection pixels for both paths, includes display color conversion and any
CPU download, and excludes projection work and final OpenGL texture copies.
Mirror timings include reflection and uploads, but exclude brush generation,
scheduling and display; their CPU reference is serial painting.

For the projection workload, initial GPU preparation takes 0.61-0.64 seconds,
the first refresh after CPU projection work takes 58-72 ms, and reading the
full projection back to CPU adds 46-49 ms. These are distinct operations and
are not included in the resident projection row.

Complete queued strokes show the remaining overhead. These tests use Normal
pixel brushes, a 1024x1024 RGBA32F document with four layers, four image workers
and 24 queued line segments, without mirroring or selections. Each process
takes five samples after warmup, alternating path order. Times include brush
generation, scheduling, Wash final merging and completed projection; tablet
events, canvas presentation and verification readback are excluded.

| Stroke | CPU only | GPU projection, CPU brush | GPU projection and brush |
| --- | ---: | ---: | ---: |
| 64px Buildup | 4.51 ms | 9.25 ms | 9.00 ms |
| 64px Wash | 5.53 ms | 10.80 ms | 12.88 ms |
| 256px Buildup | 8.04 ms | 13.33 ms | 13.76 ms |
| 256px Wash | 10.63 ms | 16.31 ms | 19.63 ms |

The CPU is faster for these short strokes. GPU brush painting remains opt-in;
these measurements do not claim a general drawing-latency improvement or a
fix for the deferred large mirrored Alpha Lock case. All measured paths
passed their CPU image comparisons. The canvas benchmark also compares the
actual OpenGL textures after timing. See the
[reproduction notes](agent/gpu-engine.md#current-build-benchmark-baseline-phase-442)
for exact tests and measurement boundaries.

## Turning it on

1. Open **Settings → Configure Solstice → Performance → General**.
2. In **GPU Engine (Vulkan)**, check **Use the GPU engine for RGBA float
   documents**.
3. Restart Solstice. The status line in the same place then shows the GPU in
   use once an RGBA float document is open.

Uncheck the option and restart to go back to the CPU engine.

## Documents in other color spaces

The GPU engine only accelerates documents in **RGBA 32-bit or 16-bit
float**. When you open a document in another color space (for example an
8-bit PNG, a 16-bit integer, CMYK, Lab, or grayscale document), Solstice asks
whether to convert it:

- **Convert** changes the document to RGBA 32-bit float. RGB documents keep
  their color profile, so they look the same. The conversion can be undone,
  and saving keeps the new color space.
- **Keep** leaves the document as it is. It works exactly as before, without
  GPU acceleration.

Check **Do not ask again** to remember the answer. You can change it later
in **Opening other documents** next to the GPU engine option (Ask, Convert,
or Keep). New documents are not converted: choose RGBA and a float depth in
the New Document dialog to use the GPU engine.

File layers are not converted, and in a document that is already RGBA float,
layers in other color spaces stay as they are; such layers are composited
on the CPU.

## If the GPU engine stops

If image data cannot be read back from the graphics card (for example after a
driver reset), the GPU engine stops and Solstice continues on the CPU. A
message tells you how many image tiles were affected: the most recent changes
in those areas are lost and show older content.

Afterwards, every save (Save, Save As, Export, and saving when closing; not
autosave) asks first. Choose **Save as New File** to keep your previous file
untouched, **Save Anyway** to overwrite it, or **Cancel** to check your
documents first. Restart Solstice to use the GPU engine again.

If the data loss happens while a document is being saved, that save fails
with a writing error and the existing file is left unchanged. Save again to
choose how to continue. (This covers most formats, including `.kra`; a few
export formats write the file directly and are not covered.)

## Requirements

- An NVIDIA Blackwell GPU (GeForce RTX 50 series or RTX PRO Blackwell) with a
  current driver. Other GPUs are not supported.
- Windows. The canvas must use desktop OpenGL (**Settings → Configure
  Solstice → Display → Preferred Renderer: OpenGL**), not ANGLE/Direct3D;
  otherwise compositing still runs on the GPU but the canvas display does
  not.

## Limitations

- The GPU tile cache has a memory limit. Idle tiles are copied back to RAM
  in batches before their GPU memory is reused, and can then use the normal
  disk swap. Busy tiles are skipped; if a transfer fails, their GPU copy is
  kept instead of being discarded to make room.
  If a processing job cannot fit, it continues through the CPU path. Undo
  data is preserved. This limit covers image tiles; display buffers and
  temporary processing buffers use additional memory.
- After a stroke, background preparation of CPU tile copies skips tiles whose
  latest pixels are only on the GPU. Undo and later CPU operations still read
  the current pixels when needed. With large brushes (around 200-300px and
  above), mirror painting with Alpha Lock may still continue catching up after
  pen release in both Wash and Buildup. Further optimization of this rare case
  is deferred; the delay is a known limitation.
- Canvas transfer buffers also have a separate limit. If queued display
  updates fill it, new updates use the CPU upload path until space is
  available. Unused shared buffers are reclaimed without discarding pending
  display updates.
- Before using accelerated canvas uploads, Solstice checks that pixels written
  by Vulkan can actually be read by OpenGL. If this check fails, the canvas
  automatically reads the projection through the CPU for the rest of the
  session. Layer compositing can continue on the GPU; no setting is changed.
- The brush prototype requires `KRITA_GPU_BRUSH=1` as well as the GPU engine.
  Large groups of brush dabs are split by their pixel-data size so they can
  stay within the GPU upload budget. A single oversized dab can still use
  the CPU. Up to three brush batches can be queued without waiting after each
  submission. Their reusable pixel, selection and table buffers share a
  64 MiB limit; buffer reuse and CPU pixel access wait for pending work when
  necessary. Tile uploads and other engine allocations are separate.
  Brush size, spacing and preset complexity can still cause drawing
  to lag behind input; GPU painting does not yet guarantee lower latency.
  It covers RGBA32F pixel brushes with matching dab/layer profiles. GPU blend
  modes are Normal, Multiply, Screen, Addition/Linear Dodge, Subtract, Darken,
  Lighten, Difference, Overlay, Hard Light, Exclusion, Linear Burn, Linear Light,
  Pin Light, the additional layer modes listed above, and Erase. They work in
  Buildup and in Wash's preview and final merge with aligned tiles, including
  selections, Alpha Lock and individual RGB locks. Alpha Darken dab compositing,
  including Wash's temporary painting buffer, is also supported with all
  channels enabled. Soft, inverted and moved
  selections are supported; their coverage is read on the CPU and applied
  on the GPU. Oversized selection snapshots use the CPU path. Channel locks
  follow the existing CPU behavior, including clearing hidden color values in
  fully transparent pixels for the added blend modes. Dab generation remains on
  the CPU. Horizontal and vertical mirror painting can also composite on
  the GPU. Both nearby and widely separated mirror passes can share one GPU
  submission, without allocating tiles in the empty space between reflections.
  Batches that exceed memory limits retain separate GPU/CPU passes. Reflected edges are clipped to the same
  painting regions as the CPU, including when a mirror axis lies between
  pixels. Performance still depends on the
  stroke and brush; the prototype does not yet establish full input latency.
  Erase supports selections, mirror painting and channel-flag combinations on
  the GPU. It reproduces the existing CPU eraser behavior: the Erase operation
  itself ignores channel flags and changes alpha while preserving RGB.
  Restricted channels with Alpha Darken, wrap-around,
  instant-preview LOD, other blend modes, and RGBA16F use the CPU path.
  It is still under development; filters and transforms are not accelerated.
- Soft proofing, channel selection in the Channels docker, and some display
  color profiles (LUT-based profiles, absolute colorimetric intent) use the
  CPU for the canvas display.
- The warning after a stop cannot tell which open document was affected.
