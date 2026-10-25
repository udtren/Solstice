---
type: pitfall
updated: 2026-10-25
sources:
  - libs/ui/canvas/kis_display_color_converter.cpp (Private::setCurrentNode(), findValidDevice())
  - libs/image/kis_pixel_selection.cpp (KisPixelSelection::compositionSourceColorSpace())
  - libs/image/kis_selection_based_layer.cpp (KisSelectionBasedLayer::paintDevice())
  - libs/image/kis_mask.cc (KisMask::paintDevice())
  - libs/widgets/KisVisualColorSelector.cpp (rebuildSelector(), colorChannelCount == 1)
  - plugins/dockers/quickaccess/QuickColorSelectorDock.cpp (selectorColorSpace(), slotUpdateColorSpace())
related:
  - build-format-test.md
---

# A node's color space can be GrayA

`KisDisplayColorConverter::nodeColorSpace()` is the composition source color
space of the current node's paint device. Filter layers, fill layers and
every mask paint on a `KisPixelSelection`
(`KisSelectionBasedLayer::paintDevice()`, `KisMask::paintDevice()`), whose
composition source is 8-bit GrayA. Selecting one of them in an RGB image
therefore reports a one-channel color space, and so does a grayscale layer.

`KisVisualColorSelector` rebuilds from the channel count: one channel gives a
single lightness bar, whatever shape configuration was set. A selector fed
`nodeColorSpace()` once, in `setCanvas()`, kept that bar until the next
canvas switch (HueSVC, user report 2026-10-25; see
[Quick Access](../../quick-access.md)).

How to avoid it:

- Re-read the color space on `KisDisplayColorConverter::displayConfigurationChanged`,
  which fires on current-node, node color space and node profile changes, as
  the Wide Gamut Color Selector does.
- A selector built for one color model (HueSVC is HSV) should not take a
  node color space of another model. HueSVC falls back to the image color
  space when it is RGBA, else `rgb8`.
- Changing a `KisVisualColorModel`'s color space resets its color to black;
  set the current color again afterwards.
