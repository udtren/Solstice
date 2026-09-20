# Custom Krita Development Notes

## Project scope

- This repository is a custom, desktop-only Krita build. Android support is
  intentionally being removed. Do not restore Android sources, build rules,
  packaging, documentation, or conditional branches unless the user explicitly
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

## Documentation organization

- Treat `README.md` as a concise project index. Feature entries contain only a
  feature name and a link to the dedicated user document; do not put detailed
  workflows, screenshots, architecture, limitations, build notes, or roadmaps
  in the README.
- Put user-facing workflow, supported scope, screenshots, and user-visible
  limitations in a stable Markdown document directly under `docs/`, such as
  `docs/puppet-warp.md`.
- Put all agent-facing technical material under `docs/agent/`. This includes
  source locations, architecture, lifecycle rules, persistence, compatibility,
  build/install/test commands, implementation limitations, invariants, manual
  regression checks, and improvement plans.
- Do not duplicate feature-specific technical instructions in `AGENTS.md`.
  Keep this file as the global policy and index to the technical documents.
- When behavior or architecture changes, update both the relevant user document
  and agent document in the same change. Keep README links valid.

## Agent technical document index

- Quick Access: `docs/agent/quick-access.md`
- Rest Note: `docs/agent/rest-note.md`
- Asset Library: `docs/agent/asset-library.md`
- Vision ML: `docs/agent/vision-ml.md`
- Puppet Warp: `docs/agent/puppet-warp.md`
- Solstice visual branding checklist:
  `docs/agent/solstice-visual-branding-todo.md`

Before changing a listed feature, read its complete agent document. When a new
custom feature is added, create its `docs/agent/<feature>.md` technical document
and add it to this index.

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
  sources.
- Run `git diff --check` on touched tracked files.
- Preserve existing user changes and unrelated source/binary assets.
- Do not infer that compilation proves interactive input behavior. Inspect
  ownership, event-filter lifetime, configuration, press/release symmetry, and
  focus conflicts; run the feature's documented manual checks.
- Do not restore removed Android files or revert unrelated changes while
  cleaning up feature work.
