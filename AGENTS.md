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
| `main` | Primary Solstice development and GitHub default branch (user decision, 2026-10-13), including the Vulkan GPU engine. Created from `krita-sol-gpu` at the start of the 0.2.0-beta release preparation. |
| `krita-sol-gpu` | Former primary branch (GPU engine development until 2026-10-13); retained for reference, no longer updated. See `docs/agent/gpu-engine.md`. |
| `krita-sol` | Historical pre-GPU development branch; retained for reference. |
| `krita/6.0` | Historical upstream snapshot; retained for reference, no longer synchronized. |

Make changes on a feature branch based on `main`, push that branch, and merge
it into `main` through a pull request (user decision, 2026-10-16). Do not push
directly to `main`, including release preparation and post-release documentation.
Use `codex/<topic>` for Codex feature branches. `main` remains the integration
and release branch; release builds use the merged commit.

Upstream Krita synchronization has
been discontinued for this repository. Do not restore the `upstream` remote
or resume upstream merges unless the user explicitly requests it. Selective
intake is allowed: Hermes Agent reviews upstream Krita weekly, and Claude
cherry-picks only the items the user approved and records every decision in
the ledger of `docs/agent/upstream-intake.md`. Apply upstream commits as
patches following that document; do not add a persistent remote. `origin`
is `https://github.com/udtren/Solstice.git`. The implementation workflow is
documented in `docs/agent/development-workflow.md`.

The former GitHub fork is retained at
`https://github.com/udtren/Solstice-upstream-archive` as a read-only archive.
The active `Solstice` repository is independent of the Krita fork network;
its three existing branch histories are preserved. Do not use the archive
as an upstream synchronization source.

## Releases and the website

Every new GitHub Release of Solstice is followed, the same day, by a website
update (user decision, 2026-10-14): in
`https://github.com/udtren/tidalboxstudio-web` (local checkout
`<projects-root>\tidalboxstudio-web`), update the version and release link
in `solstice/src/site.ts` on `develop`, check and build the site, and merge
`develop` into `main` to publish it. Publish the site only after the GitHub
Release exists, so the site never links to a missing release. The steps are
in `docs/agent/versioning.md` ("Releasing").

## Documentation organization

- Documentation sources stay in this repository (user decision, 2026-10-16).
  A future site renders these sources; it must not become a second editable
  copy. Follow [Documentation maintenance](docs/agent/documentation.md) for
  document roles, precedence, archival and review.
- Keep `README.md` in English as the project overview: identity, the
  website (`https://solstice.tidalboxstudio.com`, live since 2026-10-14),
  differences from Krita, GPU status, representative benchmarks,
  supported environment and non-affiliation. Keep individual feature entries
  as names and links; put detailed workflows, architecture and build notes in
  the dedicated documents.
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

### Upstream intake

`docs/agent/upstream-intake.md`: weekly upstream Krita review (Hermes Agent
reports on the DGX Spark share), the cherry-pick procedure, and the ledger of
adopted, rejected and held upstream commits.

### Task backlog

`docs/agent/todo.md`: task candidates the user has registered but not
started.

### General development guides

Read these before adding or updating any feature:

| Document | Use it for |
| --- | --- |
| `docs/agent/codebase-map.md` | Repository layout, library layering, key classes, and deciding which folder a change belongs in |
| `docs/agent/extension-points.md` | Recipes and required files for dockers, view plugins, tools, filters, file formats, settings, actions, and icons |
| `docs/agent/coding-rules.md` | Conventions, persistence/compatibility, undo and threading, lifecycle, formatting, and files to leave alone |
| `docs/agent/development-workflow.md` | Build, test, install, hand-off checklist, and selective upstream intake |
| `docs/agent/feature-inventory.md` | Every custom touch point in upstream files, undocumented changes, and the new-feature/document template |

### Feature documents

Feature status, active work and verification evidence belong in the linked
document, not in this index. See [Document roles](docs/agent/README.md) for
plans, historical records and references that still contain both.

- CPU-only effects and G'MIC: [development reference](docs/agent/cpu-effects-and-gmic-plan.md).
- Illustration filters: [development reference](docs/agent/illustration-filters-plan.md).
- Layer Comps: [development notes](docs/agent/layer-comps.md).
- Color Reference: [development notes](docs/agent/color-reference.md).
- Translations: [policy and tooling](docs/agent/translations.md).
- Document tool state: [development notes](docs/agent/document-tool-state.md).
- Quick Access: [development notes](docs/agent/quick-access.md).
- Rest Note: [development notes](docs/agent/rest-note.md).
- Asset Library: [development notes](docs/agent/asset-library.md).
- Vision ML: [development notes](docs/agent/vision-ml.md).
- Lazy Tools: [development notes](docs/agent/lazy-tools.md).
- Puppet Warp: [development notes](docs/agent/puppet-warp.md).
- Docker locks: [development notes](docs/agent/docker-locks.md).
- Overview live update: [development notes](docs/agent/overview-live-update.md).
- Versioning and releases: [development notes](docs/agent/versioning.md).
- Executable naming: [development notes](docs/agent/executable-name.md).
- Settings location and profile import: [development notes](docs/agent/settings-location.md).
- GPU Engine: [architecture and invariants](docs/agent/gpu-engine.md).
- GPU work order: [priorities](docs/agent/gpu-work-priorities.md).
- Brush Stroke Preview: [development notes](docs/agent/brush-stroke-preview.md).
- Brush preset grouping: [development notes](docs/agent/brush-preset-grouping.md).
- Brush Presets scroll to selection: [development notes](docs/agent/brush-preset-scroll.md).
- Visual branding: [checklist](docs/agent/solstice-visual-branding-todo.md).
- Brush option shared model: [development reference](docs/agent/brush-option-shared-model-plan.md).
- Brush options in Tool Options: [development notes](docs/agent/tool-options-brush.md).
- UI modernization: [development reference](docs/agent/ui-modernization-plan.md).
- GitHub Actions Windows builds: [development notes](docs/agent/github-actions.md).
- Explorer thumbnails: [development notes](docs/agent/windows-shell-thumbnails.md).
- Brush Stroke Layer: [development reference](docs/agent/brush-stroke-layer-plan.md).
- Photoshop brush import: [development reference](docs/agent/abr-import-plan.md).
- Welcome page News: [development notes](docs/agent/welcome-news.md).
- Popup Palette and On-Canvas Brush Editor removal: [rationale](docs/agent/popup-palette-removal.md).

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
- Solstice configuration: normally `%APPDATA%\Solstice\config\kritarc`;
  resolve it through `KisSolsticePaths` (fallbacks and legacy import are in
  `docs/agent/settings-location.md`).

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
