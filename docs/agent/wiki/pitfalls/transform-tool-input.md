---
type: pitfall
updated: 2026-10-15
sources:
  - plugins/tools/tool_transform2/kis_tool_transform.cc (KisToolTransform::endActionImpl)
  - plugins/tools/tool_transform2/kis_simplified_action_policy_strategy.cpp (activateAlternateAction)
  - plugins/tools/tool_transform2/kis_warp_transform_strategy.cpp (setTransformFunction, acceptsClicks, cachedPuppetWorker)
  - krita/data/input/kritadefault.profile ([Alternate Invocation])
  - docs/agent/puppet-warp.md ("Interaction invariants")
related:
  - build-format-test.md
---

# Transform Tool input pitfalls

Found while fixing the Puppet Warp audit items of 2026-10-15
([puppet-warp.md](../../puppet-warp.md) has the resulting behavior).

## A click is committed only when the strategy accepts clicks

`KisToolTransform::endActionImpl()` calls the strategy's `endPrimaryAction()`
(and commits an undo step when it returns true) only if the mouse moved or
`acceptsClicks()` is true. A strategy that changes the arguments on press (a
pin deleted in `beginPrimaryAction()`) must accept that click, or the change
is never committed and undo skips it. The reverse also holds: a gesture that
only changes the selection must return false from `endPrimaryAction()`, or
every drag adds a no-op undo step. Gesture state that `endPrimaryAction()`
clears (Puppet's `rubberBandActive`) sticks after a plain click unless the
click is accepted.

## "Alt" in setTransformFunction() is not the Alt key

`KisSimplifiedActionPolicyStrategy` calls `setTransformFunction(pos,
perspectiveModifierActive, shiftModifierActive, altModifierActive)` with
alternate-action flags, not keyboard modifiers:

| Parameter | Alternate action | Default profile |
| --- | --- | --- |
| `perspectiveModifierActive` | SampleFgImage/SampleBgImage | Ctrl |
| `shiftModifierActive` | ChangeSize | Shift |
| `altModifierActive` | SampleFgNode/SampleBgNode | Ctrl+Alt |

Plain Alt has no Alternate Invocation binding in `kritadefault.profile`, so
"Alt-click" features of the warp strategies are Ctrl+Alt-click in practice
(other input profiles may differ). Check the profile before documenting a
modifier.

## Cache keys must hold every input of the cached result

The Puppet strategy cached its solved worker by mesh, pins and rotations, but
the worker also renders by pin order. Order changes then left the Fast
preview stale while Apply used the new order. When a cached object takes a
new constructor argument, add it to the key in the same change.
