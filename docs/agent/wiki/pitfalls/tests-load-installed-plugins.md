---
type: pitfall
updated: 2026-11-12
sources:
  - libs/image/filter/kis_color_transformation_filter.h (createGpuColorTransform, phase 4.115)
  - libs/image/tests/KisFilterLayerBenchmark.cpp
  - docs/agent/development-workflow.md
related:
  - build-format-test.md
  - ../benchmarks/filter-layer-updates.md
---

# Tests load the installed plugins

**What happened.** Phase 4.115 added a virtual function to
`KisColorTransformationFilter`. `kritaimage` and the two filter plugins
that override it were rebuilt, and a test in the build tree crashed inside
`KisColorTransformationConfiguration::gpuColorTransform()` at the first
filter layer.

**Why.** Tests take the libraries from `_build/bin`, but
`KisFilterRegistry` loads the filter plugins from the installed tree
(`_install/lib/kritaplugins`). The installed plugins were built against
the old class and had no slot for the new virtual function.

**Fix.** After a change to a class that plugins derive from (a new or
moved virtual function, a changed member), build everything and install
(`cmake --install <krita-dev-root>\_build`) before running tests or the
application. Every plugin deriving from the class has to be rebuilt, not
only the ones that were edited: eight filter plugins derive from
`KisColorTransformationFilter`.

**How to notice.** An access violation with a "nearby symbol" in the
library that makes the virtual call, right after a header of a plugin
interface changed.
