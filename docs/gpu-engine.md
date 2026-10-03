# GPU Engine (experimental)

The GPU Engine is an ongoing rewrite of Solstice's image engine so that layer
compositing, filters, transforms, and painting run on the graphics card with
Vulkan instead of on the CPU. It is developed on the `krita-sol-gpu` branch.

## Current status

Layer compositing and the canvas display run on the GPU for RGBA floating
point documents. Painting normally runs on the CPU; an opt-in prototype can
composite Normal, Alpha Darken and Erase pixel-brush dabs on the GPU in RGBA32F
documents, including the temporary painting buffer used by Wash mode.
Filters and transforms still run on the CPU. The engine is off by default.

Automated checks compare complete strokes and their layer projections with
CPU drawing, including Buildup/Wash, mirror painting, selections, alpha lock
and Undo/Redo. Normal and Erase Wash previews and final merges, including
selections and channel locks, can stay on the GPU with aligned tiles. Other Wash
previews and final merges batch GPU readbacks
to reduce transfer waits. Short-stroke measurements still show cases where the CPU is
faster. Tablet input and the time until a stroke appears on screen still need
separate measurement.

Measured on an RTX PRO 6000 Blackwell with 4096x4096 images: compositing
16 layers takes about 42 ms instead of 253 ms, and preparing the canvas
display data for the whole image (8 layers) takes about 2 ms instead of
about 1 s. The final copy into the display textures, which also happens on
the GPU, is not included in these numbers.

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
  before their GPU memory is reused, and can then use the normal disk swap.
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
  the CPU. Brush size, spacing and preset complexity can still cause drawing
  to lag behind input; GPU painting does not yet guarantee lower latency.
  It covers RGBA32F pixel brushes using Normal, Alpha Darken or Erase compositing,
  with matching dab/layer profiles. Buildup and Wash are supported; Wash's
  preview and final merge can run on the GPU when using Normal or Erase
  blending with aligned tiles, including selections and channel locks. Soft, inverted and moved
  selections are supported; their coverage is read on the CPU and applied
  on the GPU. Oversized selection snapshots use the CPU path. Normal Wash
  respects Alpha Lock and individual RGB locks, including their combination
  with selections. Direct painting also supports selections, including
  soft edges and inverted selections; their coverage is read on the CPU
  and applied during GPU compositing. Normal direct painting also supports
  alpha lock and individual RGB channel locks. Dab generation remains on
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
