---
type: pitfall
updated: 2026-11-03
sources:
  - "libs/image/kis_node.cpp (KisNode::setDirty(), KisNode::add())"
  - "libs/image/kis_selection_based_layer.cpp (needProjection())"
  - "libs/image/kis_layer.cc (KisLayer::projection(), fillMergedLayerTemplate())"
  - "libs/image/kis_layer_utils.cpp (RefreshHiddenAreas, refreshHiddenAreaAsync())"
  - "libs/libkis/Node.cpp (Node::mergeDown(), Node::addChildNode())"
  - "plugins/python/channels2layers/channels2layers.py (process(), mergeDown())"
  - "https://bugs.kde.org/show_bug.cgi?id=426349"
related:
  - sip-return-types.md
  - group-projection-outside-canvas.md
---

# Nodes outside the image never render

A node that is not attached to the image's graph gets no projection updates,
and nothing reports it. `KisNode::setDirty()` only forwards to
`graphListener`, and a node gets that listener from its parent in
`KisNode::add()`. `Document.createGroupLayer()` makes a group that belongs to
the image (`node.image()` is set) but has no listener until it is added, and
children added to it have none either. Update walkers never visit them.

A node whose `projection()` is the same device as `original()` does not
notice: a plain paint layer has no separate projection. A node with
`needProjection()` or an effect mask does. A fill layer always has one,
because its internal selection makes `KisSelectionBasedLayer::needProjection()`
true: the generator fills `original()`, and `projection()` stays empty. Measured
through libkis on `40f129a3df`:

| Case, in a group outside the document | Result |
| --- | --- |
| Fill layer, `setGenerator()`, `waitForDone()`, first read of `projectionPixelData()` | Transparent |
| The same, 0.4 s of processed events before the merge | Merge leaves the layer below unchanged |
| Group attached just before the merge | Merge leaves the layer below unchanged |
| Group attached, then `refreshProjection()` or `setGenerator()` again | Fill layer renders |
| Paint layer with a transparency mask of opacity 128 | Projection keeps full opacity |
| Group attached before any child is added, visible or hidden | Merge is correct |

So this is not the timing problem of
[bug 426349](https://bugs.kde.org/show_bug.cgi?id=426349): waiting does not
help, and neither does `waitForDone()`.

Merge Down still runs on such a node, which is what hides the mistake.
`KisLayer::fillMergedLayerTemplate()` paints `projection()`, an empty fill
layer paints nothing, and the result is a copy of the layer below with no
error. `RefreshHiddenAreas` does not help: it refreshes only what lies outside
the image bounds, because a walker is assumed to have prepared the rest.

## Rule

Add a node to the document before an operation that reads its projection.
Build scripts in a group that is already attached; hide it if intermediate
states should not show. `Filter.apply()` and `setPixelData()` work on the
paint device and are not affected, which is why a script can look half right.

Reading `projection()` from a test can also change the outcome:
`KisSafeNodeProjectionStore::getDeviceLazy()` clones `original()` the first
time, so read a projection only after the step under test.

Unverified: whether Channels to Layers ever worked with its group outside the
document. The discussion of bug 426349 reports a similar script as correct in
Krita 4.3; neither was run for this page. Krita 5.2.6, 5.2.14 and `master`
still attach the group last, so they should fail the same way, also not run.
