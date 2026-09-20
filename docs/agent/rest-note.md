# Rest Note — agent development notes

## Status and locations

The Rest Note migration from Python to native Krita is complete. Treat future
work as maintenance or explicitly requested refinement, preserve the native
architecture and visible layout, and keep compatibility with Python-era
configuration.

- Native implementation: `plugins/dockers/restnote/`
- Original reference: `<original-plugin-root>/krita-rest-note/rest_note/`
- Native icons: `plugins/dockers/restnote/resources/icons/`
- Original icons: `<original-plugin-root>/krita-rest-note/rest_note/icons/`
- Icons are embedded under `:/restnote/`.

Inspect the Python implementation directly when behavior is uncertain.

## Build and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritarestnotedocker -j 2"
```

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\restnote\cmake_install.cmake
```

## Configuration

Configuration lives at `%APPDATA%\krita\rest_note\config\main.json`. Preserve
all Python-era keys, including work/break durations, eye-break timing, idle
detection, toast geometry/fonts, and overlay fonts.

## Lifecycle and behavior

- The input-idle event filter is owned by the persistent native docker and must
  be removed when that docker is destroyed.
- The large break overlay is a child of the owning Krita `QMainWindow`, covers
  only Krita's client area, and follows window resize, minimize, and close.
  Never restore the monitor-wide always-on-top overlay unless explicitly asked.
- The small eye-break toast is a non-activating, input-transparent top-level
  notification on Krita's current screen.
- Preserve five timer states: running, paused, big break, eye break, and idle.
  Big-break timing takes priority over an active eye break.
- Idle detection pauses the work timer after configured inactivity and resumes
  it when activity returns. Explicit pause and break states must not be
  overridden by idle transitions.
- Big breaks reset both timers. Eye breaks keep the big-break timer running and
  may be skipped when a big break is near.
- Keep the original `pause.png`, `play.png`, `refresh.png`, `rest.png`, and
  `setting.png` artwork rather than substituting theme icons.
- Status, timer, secondary text, and four icon buttons scale with available
  docker size, matching the original layout.

Preserve existing source and binary assets under
`plugins/dockers/restnote/resources`.
