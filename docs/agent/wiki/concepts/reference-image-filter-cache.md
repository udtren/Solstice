---
type: concept
updated: 2026-10-17
sources:
  - libs/ui/KisReferenceImage.cpp
related:
  - native-refactor-boundaries.md
---

# Reference image filter cache

Reference images are shapes backed by QImage, separate from paint layers.
Non-destructive filter stacks use temporary paint devices, then cache the result
for drawing and sampling. Keep transient preview settings outside serialized
state so autosave cannot commit an open dialog's changes. A revision number
prevents asynchronous results from overwriting newer settings; workers own data
without retaining the shape. See the owning [reference](../../reference-image-filters.md).
