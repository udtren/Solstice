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
- Responsibility separation (2026-10-16): `RestNoteConfig.*` owns JSON settings;
  `RestNoteTimer.*` owns countdowns and state transitions;
  `RestNoteSettingsDialog.*` edits a configuration value;
  `RestNoteNotifications.*` implements the overlay, progress button and toast.
  `RestNoteDock.*` coordinates them with the existing Qt timer, input filter,
  configuration notifier and docker controls (826 to 346 source lines).

Inspect the Python implementation directly when behavior is uncertain.

## Build, test and install

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && cmake --build <krita-dev-root>\_build --target kritarestnotedocker RestNoteCoreTest RestNoteWidgetsTest -j 2"
```

```bat
cmd.exe /d /s /c "call <krita-dev-root>\env.bat && set QT_QPA_PLATFORM=offscreen&& set QT_QPA_FONTDIR=C:\Windows\Fonts&& ctest --test-dir <krita-dev-root>\_build -R plugins-dockers-restnote- --output-on-failure --timeout 60"
```

```bat
cmake -DCMAKE_INSTALL_LOCAL_ONLY=1 -P <krita-dev-root>\_build\plugins\dockers\restnote\cmake_install.cmake
```

## Enable setting

- `Solstice/RestNoteEnabled` in kritarc, default `false` (Configure Solstice >
  General > Custom, `chkRestNoteEnabled` in `libs/ui/forms/wdggeneralsettings.ui`,
  read/written/reset in `GeneralTab` of `libs/ui/dialogs/kis_dlg_preferences.cc`).
  Added 2026-10-10 at the user's request; manually checked 2026-10-10.
- `RestNotePlugin` registers `RestNoteDockFactory` only when
  `RestNoteDock::isEnabled()` holds, so a disabled docker has no timer, idle
  event filter or break windows. Enabling needs a restart.
- A loaded docker follows `KisConfigNotifier::configChanged`
  (`applyEnabled()`): off stops the timer, removes the application event
  filter, cancels the overlay and toast, hides the docker and disables its
  toggle action; on again starts a fresh work period. The event filter is
  installed only while active (`m_active`), and the destructor removes it only
  then.

## Configuration

Configuration uses `KoResourcePaths::saveLocation("data", "rest_note/config/")`
and `main.json`: normally `%APPDATA%\Solstice\resources\rest_note\config\main.json`
(formerly `%APPDATA%\krita\rest_note\config\main.json`; see
`settings-location.md`). The resolver and relative path are unchanged. Preserve
all Python-era keys, including work/break durations, eye-break timing, idle
detection, toast geometry/fonts, and overlay fonts.

- `RestNoteConfig::load()` and `save()` accept an explicit path for tests;
  production defaults to the original resolver. All 16 JSON keys and defaults
  are unchanged. An unreadable/missing file triggers an attempt to save current
  values; malformed/non-object JSON leaves current values intact without a
  rewrite. Absent/wrongly typed fields keep current values. Loading does not
  clamp numbers to dialog ranges. Saving still replaces the known-key object,
  using the original QFile/JSON format and error behavior.
- The dialog edits the passed value only on acceptance. The dock still saves
  it and adjusts countdowns after the dialog exits; cancellation writes nothing.

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
- `RestNoteTimer` has no widget, application event filter or clock lookup.
  The dock supplies `QDateTime::currentDateTime()` for idle checks and activity.
  Each delivered one-second timeout decrements once; delayed ticks do not catch
  up elapsed wall time. Preserve the inclusive idle/eye-break skip thresholds.
- Idle transitions affect only Running/Idle. Resume, reset, re-enable and the
  end of a big break refresh activity time. An eye-break completion does not.
  Pausing an eye break cancels its toast without resetting the eye countdown;
  resuming can therefore immediately request another eye break, as before.
- Applying settings while Running caps both remaining counters at the new
  intervals; in every other state it resets the counters but keeps the state
  and current notification. Do not silently change these existing semantics.
- The timer returns a transition for the dock to present. Only the dock owns
  notification pointers and callbacks. `cancelEyeBreak()` clears the completion
  callback before the existing fade-out; cancelling during fade-out also clears
  it. Overlay parentage, focus, animations and deferred deletion are unchanged.
- `kritarestnotecore` is a private static library for config/timer tests,
  linking Qt Core and `kritaresources` for the original path resolver. There is
  no new installed shared library or persisted state identifier.
- Keep the original `pause.png`, `play.png`, `refresh.png`, `rest.png`, and
  `setting.png` artwork rather than substituting theme icons.
- Status, timer, secondary text, and four icon buttons scale with available
  docker size, matching the original layout.

Preserve existing source and binary assets under
`plugins/dockers/restnote/resources`.

## Regression coverage

`RestNoteCoreTest` covers the legacy JSON round trip, defaults, malformed/partial
files, save failure, all five states, simultaneous break deadlines, eye-break
skip boundaries, activity/pause interactions, disabled reminders and settings
changes in each state. No real-time wait or user profile is needed.

`RestNoteWidgetsTest` checks every settings field and accept/cancel behavior,
overlay ownership/resize/progress/skip/host destruction, toast flags/geometry,
normal completion, repeated cancellation and cancellation during fade-out.
It also constructs the actual dock and tests disabled startup, enable, break,
disable, re-enable, pause/reset and destruction cleanup. A temporary profile and
explicit KConfig path keep it away from user preferences. Synthetic input must
use a real `QKeyEvent`/`QMouseEvent`, not a base `QEvent` carrying their type.

Validation on 2026-10-16: docker and both test targets built; both CTest suites
passed offscreen (4.03 seconds). Notification class bodies and translated
strings were compared against the original and are unchanged. The plugin was
installed into the development test installation, with matching SHA-256 hashes
for the build and installed DLLs. The user confirmed that the refactored
Rest Note works correctly on 2026-10-16.

## Manual regression checklist

1. Restart with Rest Note enabled; confirm saved durations, fonts and geometry.
   Edit settings, cancel once, then save and restart to confirm persistence.
2. Pause/resume and reset; let idle detection pause the countdown, then resume
   with mouse, keyboard and tablet activity.
3. Take a break now, resize/minimize/restore the window, skip and reset.
   The overlay must remain inside its owning Solstice window.
4. Allow an eye break to finish, pause during one and reset during one. Verify
   notification placement and no focus stealing; let a full break supersede it.
5. Disable Rest Note during a notification, re-enable, and close a window with
   an active notification. Confirm no delayed callback restarts the old cycle.
