---
type: pitfall
updated: 2026-11-11
sources:
  - libs/image/brushengine/kis_paintop_settings.cpp (KisPaintOpSettings::setProperty(), onPropertyChanged())
  - plugins/paintops/libpaintop/kis_brush_based_paintop_settings.cpp (KisBrushBasedPaintOpSettings::onPropertyChanged() clears m_savedBrush)
  - libs/ui/tool/kis_resources_snapshot.cpp (stroke preset clone, "At least 1 pixel")
  - docs/agent/brush-minimum-one-pixel.md
related:
  - paint-ops-read-temporary-target.md
---

# Writing a paint op setting clears the brush caches

`KisPaintOpSettings::setProperty()` is not a plain store. When the value
changes it marks the update listener dirty and records the key, and it always
calls `onPropertyChanged()`, which

- drops the settings' resource cache interface
  (`d->resourceCacheInterface`), the precomputed data that
  `KoCanvasResource::CurrentPaintOpPresetCache` hands to each stroke's
  preset clone, and
- in brush-based engines also drops the cached brush tip
  (`KisBrushBasedPaintOpSettings::m_savedBrush`).

Code that adds a stroke-only value to the preset clone a
`KisResourcesSnapshot` makes would therefore make every stroke rebuild its
brush tip and derived data, which is expensive for text, pipe and large
predefined tips.

**Do instead:** when the value does not affect the brush tip or the cached
data, write it with the base class setter,
`settings->KisPropertiesConfiguration::setProperty(key, value)`: no listener
notification, no dirty mark, caches kept. Do it on the clone, never on the
canvas's current preset (that would mark the user's preset as modified). The
"At least 1 pixel" option does this
([agent document](../../brush-minimum-one-pixel.md#where-the-setting-lives)).
