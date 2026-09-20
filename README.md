## Solstice

Solstice is an unofficial, desktop-focused custom build based on the Krita 6 development branch.
It preserves Krita's painting workflow while integrating project-specific interface and productivity changes.

## Main differences from upstream Krita

- Android application, packaging, donation, and platform-integration code has been removed.
- [Quick Access Manager](https://github.com/udtren/krita-quick-access-manager) has been migrated from a Python plugin to a native Krita docker.
- Additional interface and workflow refinements are maintained for this custom build.

The native Quick Access implementation is located in
[`plugins/dockers/quickaccess`](plugins/dockers/quickaccess/).

## New Features

### Puppet Warp
<img src="images/puppet_warp_1.png" alt="Puppet Warp mesh" width="80%">

Solstice adds a native **Puppet Warp** mode to Krita's Transform Tool for posing characters and reshaping artwork with pins. Puppet Warp generates a mesh that follows the artwork's visible shape, including the interiors of closed line art, and allows the mesh boundary to be expanded when extra working space is needed.

To use it:

1. Select the layer or area to transform, activate the Transform Tool, and choose **Puppet**.
2. Leave **Draw** active and click the artwork to place pins at joints and areas that should remain
   stable.
3. Click **Lock Points** when the pins are ready.
4. Drag a pin's center to move that part of the artwork.
5. Drag the pin's outer ring to rotate the artwork around it.
6. Alt-click a pin center to remove it, then apply or reset the transform normally.

Untouched pins anchor their surrounding regions. Rotation also propagates through an unpinned branch:
for example, rotating a terminal hip pin can turn the upper body while pins below it continue to hold the legs. Placing another pin farther along that branch creates a new boundary and limits the effect.

The **Show mesh** option toggles only the overlay. **Expansion** controls how far the generated mesh extends beyond the detected artwork; at 0 px, the overlay is clipped closely to the artwork rather than extending by whole grid cells.

Implementation notes, current limitations, testing instructions, and the future improvement roadmap are documented in [`docs/puppet-warp.md`](docs/puppet-warp.md).

## Branches

| Branch | Purpose |
| --- | --- |
| `krita-sol` | Custom Krita Sol development and builds |
| `krita/6.0` | Clean tracking branch for synchronizing with upstream Krita 6 |

Custom changes should be made on `krita-sol`. Keep `krita/6.0` aligned with upstream so future
updates can be integrated cleanly.

## Building

Solstice uses Krita's standard desktop build system. Refer to the official
[Building Krita documentation](https://docs.krita.org/en/untranslatable_pages/building_krita.html)
for prerequisites and platform-specific instructions.
