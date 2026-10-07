# Solstice Development Notes

## Project scope

- This repository is an independent, desktop-only application derived from
  Krita. Android support is intentionally being removed. Do not restore Android
  sources, build rules, packaging, documentation, or conditional branches unless the user explicitly
  reverses that decision.
- The working tree is intentionally very dirty. Existing modifications and
  deletions belong to the user. Never discard, reset, or rewrite unrelated
  changes.
- Quick Access Manager, Rest Note, and Asset Library have completed their
  Python-to-native migrations. Treat future work as maintenance, bug fixing, or
  explicitly requested refinement; preserve native architecture, established
  behavior, and legacy configuration compatibility.
- Puppet Warp is an established native Transform Tool feature. Preserve its
  preview/final-render consistency, serialized state, and current interaction
  unless the user requests a behavior change.

## Branch policy

| Branch | Purpose |
| --- | --- |
| `krita-sol-gpu` | Primary Solstice development and GitHub default branch, including the Vulkan GPU engine. See `docs/agent/gpu-engine.md`. |
| `krita-sol` | Historical pre-GPU development branch; retained for reference. |
| `krita/6.0` | Historical upstream snapshot; retained for reference, no longer synchronized. |

Make custom changes on `krita-sol-gpu`. Upstream Krita synchronization has
been discontinued for this repository. Do not restore the `upstream` remote
or resume upstream merges unless the user explicitly requests it. `origin`
is `https://github.com/udtren/Solstice.git`. This policy supersedes the older
branch and synchronization instructions in `docs/agent/development-workflow.md`;
that document's build, test and installation instructions still apply.

The former GitHub fork is retained at
`https://github.com/udtren/Solstice-upstream-archive` as a read-only archive.
The active `Solstice` repository is independent of the Krita fork network;
its three existing branch histories are preserved. Do not use the archive
as an upstream synchronization source.

## Documentation organization

- Keep `README.md` in English as the project overview: identity, the planned
  website, differences from Krita, GPU status, representative benchmarks,
  supported environment and non-affiliation. Keep individual feature entries
  as names and links; put detailed workflows, architecture and build notes in
  the dedicated documents. Do not invent a website URL before it exists.
- Put user-facing workflow, supported scope, screenshots, and user-visible
  limitations in a stable Markdown document directly under `docs/`, such as
  `docs/puppet-warp.md`.
- Put all agent-facing technical material under `docs/agent/`. This includes
  source locations, architecture, lifecycle rules, persistence, compatibility,
  build/install/test commands, implementation limitations, invariants, manual
  regression checks, and improvement plans.
- Do not duplicate feature-specific technical instructions in `AGENTS.md`.
  Keep this file as the global policy and index to the technical documents.
- Keep cross-cutting knowledge in the agent wiki, `docs/agent/wiki/`:
  concepts, decisions with their reasons, pitfalls, benchmarks, saved answers
  and archived phase records. Its `index.md` defines the page types,
  frontmatter and the ingest/query/lint workflows. Read it before starting
  work. After each committed task, add what the work taught, and append to
  `wiki/log.md`. Wiki pages link to feature documents instead of copying them;
  the feature document wins in a conflict.
- When behavior or architecture changes, update both the relevant user document
  and agent document in the same change. Keep README links valid.

## Agent technical document index

### Agent wiki

`docs/agent/wiki/index.md`: cross-cutting concepts, decisions, pitfalls,
benchmarks, and the GPU engine phase history.

### General development guides

Read these before adding or updating any feature:

| Document | Use it for |
| --- | --- |
| `docs/agent/codebase-map.md` | Repository layout, library layering, key classes, and deciding which folder a change belongs in |
| `docs/agent/extension-points.md` | Recipes and required files for dockers, view plugins, tools, filters, file formats, settings, actions, and icons |
| `docs/agent/coding-rules.md` | Conventions, persistence/compatibility, undo and threading, lifecycle, formatting, and files to leave alone |
| `docs/agent/development-workflow.md` | Build, test, install, hand-off checklist, and upstream synchronization |
| `docs/agent/feature-inventory.md` | Every custom touch point in upstream files, undocumented changes, and the new-feature/document template |

### Feature documents

- Quick Access: `docs/agent/quick-access.md`
- Rest Note: `docs/agent/rest-note.md`
- Asset Library: `docs/agent/asset-library.md`
- Vision ML: `docs/agent/vision-ml.md`
- Lazy Tools: `docs/agent/lazy-tools.md`
- Puppet Warp: `docs/agent/puppet-warp.md`
- Docker locks: `docs/agent/docker-locks.md`
- Overview live update:
  `docs/agent/overview-live-update.md`
- Versioning (Solstice version vs. Krita compatibility version):
  `docs/agent/versioning.md`
- Settings location (`%APPDATA%\Solstice`; phases 1-4 of 5 done):
  `docs/agent/settings-location.md`
- GPU Engine (branch `krita-sol-gpu`): `docs/agent/gpu-engine.md`
- GPU Engine work order (priority 1 measurement work in progress):
  `docs/agent/gpu-work-priorities.md`
- Brush Stroke Preview:
  `docs/agent/brush-stroke-preview.md`
- Brush preset grouping:
  `docs/agent/brush-preset-grouping.md`
- Solstice visual branding checklist:
  `docs/agent/solstice-visual-branding-todo.md`
- Brush option shared model (phase 1 implemented for Deform, manual check pending):
  `docs/agent/brush-option-shared-model-plan.md`, findings in
  `docs/agent/brush-option-shared-model-phase0.md`
- UI modernization (phases 0-3 done; phase 4 first part done):
  `docs/agent/ui-modernization-plan.md`
- GitHub Actions Windows trial builds: `docs/agent/github-actions.md`

Before changing a listed feature, read its complete agent document. When a new
custom feature is added, create its `docs/agent/<feature>.md` technical document,
add it to this index, and add its touch points to
`docs/agent/feature-inventory.md`.

## Shared development environment

The source checkout and build tree are separate:

- Source: `<repository-root>`
- Build: `<krita-dev-root>\_build`
- Test installation: `<krita-dev-root>\_install`
- Environment script: `<krita-dev-root>\env.bat`
- Clang-format: `<toolchain-root>\bin\clang-format.exe`
- Active Krita configuration: `%LOCALAPPDATA%\kritarc`

Feature-specific targets, tests, installation scripts, configuration paths, and
runtime dependencies are documented under `docs/agent/`.

If a change affects a shared Krita library, install that library too. For
example, changes under `libs/widgets` require:

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\libs\widgets\cmake_install.cmake
```

Krita must be fully restarted after installing rebuilt DLLs. A running Krita
process locks native plugin DLLs on Windows, so never terminate it without the
user's approval; ask the user to close Krita if installation is blocked.
Continue to format, compile, test, and install native changes incrementally
before handing them off for interactive testing.

## Shared architecture and lifecycle rules

- Avoid calling `KisPart::instance()` or accessing a view manager from plugin
  constructors before a main window exists. This previously caused the
  `KisActionPlugin.cpp` `m_viewManager` assertion.
- Follow native Krita observer and ownership patterns. Feature-specific object
  ownership and event-filter lifetime requirements are in the corresponding
  `docs/agent/` document.

## Editing and verification discipline

- Use `apply_patch` for source and documentation edits.
- Use the configured clang-format executable for modified C++ headers and
  sources. Format new files entirely, but only the changed lines of existing
  upstream files (see `docs/agent/coding-rules.md`).
- Run `git diff --check` on touched tracked files.
- Preserve existing user changes and unrelated source/binary assets.
- Do not infer that compilation proves interactive input behavior. Inspect
  ownership, event-filter lifetime, configuration, press/release symmetry, and
  focus conflicts; run the feature's documented manual checks.
- Do not restore removed Android files or revert unrelated changes while
  cleaning up feature work.
