# Settings location and profile import: maintenance reference

This is the current owner for profile paths, startup/import, configuration
defaults and reset behavior. User guidance is in
[Settings folder](../settings-folder.md). The complete original investigation
and phase results are preserved in
[development history](wiki/history/settings-location-development.md).

The migration is implemented. The original "shares every file with Krita"
baseline, provisional helper names and pending phase checklists are historical.
Keep maintenance on the native path/profile services; do not reintroduce the
old shared-profile layout.

## Profile layout and path API

On Windows the normal root is `%APPDATA%\Solstice`, resolved using
`SHGetKnownFolderPath(FOLDERID_RoamingAppData)`. The fallback when that lookup
is unavailable (also used outside Windows) is
`QStandardPaths::GenericConfigLocation`, with `Solstice` appended.
The application name remains `krita`; changing the executable name does not
select these folders. See [Executable naming](executable-name.md).

| Location | Contents / owning helper |
| --- | --- |
| `config/` | `kritarc`, `kritadisplayrc`, `kritashortcutsrc`, `krita-scripterrc`, `karboncalligraphyrc`, `klanguageoverridesrc`, `SOLSTICE_PROFILE`; `configDir()`, `configFilePath()` |
| `config/kxmlgui5/krita/` | Local menu/toolbar XMLGUI files; `xmlguiDataDir()` returns the configuration directory |
| `logs/` | `krita.log`, `krita-sysinfo.log`, `kritacrash.log`; `logDir()`, `logFilePath()`, `crashLogPath()` |
| `resources/` | Default brushes/bundles/database, workspaces and feature/Python data; `defaultResourceDir()` |
| `cache/` | Regenerable caches, including brush previews and `qmlcache/`; `cacheDir()` |

Use [KisSolsticePaths](../../libs/global/KisSolsticePaths.h) rather than
rebuilding these paths from QStandardPaths in callers. The service lives in
`kritaglobal` and is usable before a QCoreApplication exists.
`profileRoot()` is the root helper; the original proposal's `root()`,
`resourceDir()` and `configFile()` names are not the API.

**Different-drive configuration fallback:** `configDir()` checks whether a
relative path can be formed from GenericConfigLocation to the preferred
profile configuration directory. If not, configuration moves to
`%LOCALAPPDATA%\Solstice\config` on Windows. This includes the marker and
XMLGUI files, not just `kritarc`. Logs, default resources and cache stay under
`profileRoot()`. Always use the helpers, including with development overrides;
do not assume that renaming the roaming root alone resets every configuration.

### Resource and cache overrides

`KoResourcePaths::getAppDataLocation()` first honors the command-line
`--resource-location` override, then reads `ResourceDirectory`, with the
profile resource directory as fallback. It normalizes/checks configured paths
and can fall back when a location cannot be used. `saveLocationInternal()`
reads `ResourceDirectory` and then the profile default; it does not consult
the command-line override directly. Preserve this distinction when auditing a
new resource consumer. This document does not promise that all plugin data
follows a command-line override.

Resource search cleanup excludes the ordinary Krita AppDataLocation when the
selected resource directory differs. Installed resources and translations
remain searchable. Local XMLGUI configuration uses `config/` independently
of `ResourceDirectory`; the main-window `krita5.xmlgui` customization still
uses the resource path. See the [XMLGUI policy](wiki/decisions/xmlgui-unchanged.md).

At startup, `SOLSTICE_CONFIG_DIR` is set to `configDir()` for Python Scripter.
`QML_DISK_CACHE_PATH` is set to `cache/qmlcache` only if it is not already set.
An explicit environment override can therefore put QML cache elsewhere.
Brush preview cache ownership is in [Brush Stroke Preview](brush-stroke-preview.md).

Temporary files (including swap and unsaved-document autosaves), dependency
font caches, Python site-packages, user documents, recorder output and user
chosen storage paths are outside this redirection. It is not a process-wide
filesystem sandbox.

## Source map and caller rules

| Responsibility | Source / consumers |
| --- | --- |
| Paths, test roots and defaults mount | [KisSolsticePaths.cpp](../../libs/global/KisSolsticePaths.cpp) |
| Marker, legacy discovery, copying and path rewriting | [KisSolsticeProfile.cpp](../../libs/global/KisSolsticeProfile.cpp), [API](../../libs/global/KisSolsticeProfile.h) |
| Early profile preparation, dialog and deferred resource copy | [main.cc](../../krita/main.cc): `prepareSolsticeProfile()`, `finishSolsticeImport()` |
| Embedded main configuration | [kritarc-defaults.qrc](../../krita/kritarc-defaults.qrc), [SolsticeEmbedRcc.cmake](../../cmake/modules/SolsticeEmbedRcc.cmake), [SolsticeEmbedRccToCpp.cmake](../../cmake/modules/SolsticeEmbedRccToCpp.cmake), [krita CMake](../../krita/CMakeLists.txt) |
| Resource resolver and initial bundles | [KoResourcePaths.cpp](../../libs/resources/KoResourcePaths.cpp), [KisResourceCacheDb.cpp](../../libs/resources/KisResourceCacheDb.cpp), [KisResourceLocator.cpp](../../libs/resources/KisResourceLocator.cpp) |
| Settings reset | [KisApplication.cpp](../../libs/ui/KisApplication.cpp): `resetConfig()` |
| Logging and crash path | [KisUsageLogger.cpp](../../libs/global/KisUsageLogger.cpp), `tryInitDrMingw()` in main |
| Language override and local XMLGUI | [kswitchlanguagedialog_p.cpp](../../libs/widgetutils/xmlgui/kswitchlanguagedialog_p.cpp), [kxmlguiclient.cpp](../../libs/widgetutils/xmlgui/kxmlguiclient.cpp) and sibling XMLGUI writers |
| Scripter settings | [scripter.py](../../plugins/python/scripter/scripter.py) |

Configuration consumers include `kis_config.cc`, `kis_opengl.cpp`,
preferences, tablet-resolution and bug-info dialogs; named KConfig consumers
include the action registry, shortcuts editor, Calligraphy tool and
`KoDocumentInfo`. Help/log dialogs and `kbugreport.cpp` use the profile log
helpers. The original per-file migration inventory is retained in history;
new callers must use the current helpers instead of copying old line numbers.

### KConfig names and embedded defaults

For KConfig use `kconfigName(fileName)` or `mainConfigName()`, a path relative
to GenericConfigLocation. For QSettings and filesystem access use the absolute
`configFilePath(fileName)`. Do not pass an absolute main configuration name:
the KConfig behavior this integration supports resolves names against
GenericConfigLocation and looks up defaults at `:/kconfig/<name>`.

`solstice_embed_rcc()` compiles `kritarc-defaults.qrc` as a binary RCC and
embeds the bytes as `solsticeKritarcDefaultsRcc`. Before the first configuration
read, `registerMainConfigDefaults()` mounts them at the cleaned folder of
`:/kconfig/` plus `mainConfigName()`. A name containing `../` changes that
resource lookup location. The original `:/kconfig/kritarc` remains available.
The byte array must remain valid for the process lifetime.

`KConfig::setMainConfigName()` must run before even the temporary
QCoreApplication: startup functions can open the main configuration before
`KisApplication` is constructed. Setting it later silently selects the wrong
file/default layer. Keep the mount and path tests when changing this order.

## First start and import state machine

The marker is `configFilePath("SOLSTICE_PROFILE")`. Its trimmed content is
`ready` or `importing`; unreadable, missing or unrecognized content is
`Missing`. `setState(Missing)` removes the marker; it does not remove folders.

| State | Startup behavior |
| --- | --- |
| Missing | Prepare a fresh profile or import after legacy discovery |
| Importing | Skip the first-start question, then retry resource copying after application startup |
| Ready | Continue without importing again |

`legacyProfileExists()` checks for `kritarc` in `legacyConfigDir()` or
`resourcecache.sqlite` in `legacyResourceDir()`. An arbitrary empty legacy
resource directory alone does not trigger the question.

Startup sequence:

1. Mount main defaults, then call `prepareSolsticeProfile()`. On Windows,
   a detected legacy profile prompts with native `MessageBoxW`: Yes imports,
   No starts fresh, Cancel quits. This precedes Qt application construction
   and configuration reads. With no legacy profile it creates a fresh one.
   **The non-Windows branch currently chooses import without this dialog**;
   do not describe the Windows prompt as cross-platform behavior.
2. Set the main KConfig name, publish `SOLSTICE_CONFIG_DIR`, set the QML cache
   default and initialize DrMingw when compiled in. Read display/language
   settings and construct the application.
3. Apply the existing single-instance check. The key uses `"Solstice"` and
   the home path, distinct from Krita's key. It is not keyed by
   `SOLSTICE_PROFILE_ROOT`, so a scratch root alone does not isolate a second
   running Solstice instance.
4. `finishSolsticeImport()` handles `Importing` with a modal progress dialog
   before normal resource loading and `KisApplication::start()`.
   On success mark Ready; on failure/cancel abandon the import, warn and quit.

When the state is Missing, an argument starting with `--export` skips the
preparation/question. This is not a guarantee of zero later writes: startup
continues, and an already Importing profile still reaches deferred copying.

The pre-Qt messages/errors are built in, not loaded from translation catalogs.
A recognized legacy override selects Japanese (`ja*`) or Simplified Chinese
(`zh_CN`/`zh_SG`); otherwise Windows Japanese or Simplified/Singapore Chinese
display language is used, with English fallback. Even `en_US` is not a
recognized override for forcing English over that Windows fallback.
The fresh profile's later interface language is independently initialized to
`[Language] krita=@ByteArray(en_US)` if no value exists.

### Configuration and resource copying

`importConfiguration()` creates profile folders and copies existing files
from the explicit six-file list: `kritarc`, `kritadisplayrc`,
`kritashortcutsrc`, `krita-scripterrc`, `karboncalligraphyrc` and
`klanguageoverridesrc`. Logs and arbitrary manual rc backups are not imported.

If the legacy resource folder exists and `ResourceDirectory` is empty or
matches that folder (normalized, case-insensitive comparison),
`resourcesToImport()` is true. Only in this case does importing `kritarc`
rewrite old resource-folder references to the new default resource directory:

- forward slashes, native backslashes, escaped backslashes and `$HOME` forms;
- case-insensitive matching with folder-end delimiters, preserving names such
  as `krita - Copy` and `kritaX`;
- literal replacement, including paths embedded in JSON or expanded KConfig
  entries; unrelated export/recent/swap/tool paths remain untouched.

The rewrite is textual across `kritarc`, not a parser restricted to a fixed
key whitelist. Other copied rc files are copied unchanged.
If a custom ResourceDirectory is used, no default-resource copy or kritarc
rewrite occurs and the marker becomes Ready. That resource folder remains
shared if Krita also uses it; "Krita files are read-only during import" does not
mean later use of shared resources cannot change them.

Legacy `kxmlgui5/` is copied separately into the configuration directory
even when resources are custom. The resource-copy plan enumerates files,
excluding root-level `resourcecache.sqlite.N~` backups and `kxmlgui5/`.
It retains other old files, including `krita4.xmlgui`, blacklists and tasksets;
the original proposal to consider removing them was not adopted.

`copyResources()` clears the target default-resource directory before copying.
It checks available space when storage information is valid (planned bytes
plus 64 MiB), copies in 4 MiB chunks with cancellation/progress and attempts
to preserve modification times used by the database. The copied resource
database retains tags, versions and storage enabled states; migration does
not rebuild it or overwrite its bundle choices with new-profile defaults.

This is a fresh-destination import, not an incremental merge or atomic
transaction. A retry from Importing restarts the resource copy.
The low-level routine removes the destination on insufficient space or a file
copy failure/cancel; a final marker-write failure returns false without that
local cleanup. The startup caller invokes `abandonImport()` on failures;
that removes the resolved configuration and default-resource directories,
including the marker, but leaves logs/cache. Filesystem deletion can fail;
do not promise transactional rollback or deletion of every created folder.
Neither import nor cleanup deliberately writes to legacy source directories.

## Defaults and resets

Defaults come from several owners, not from one profile-creation function:

| Default | Owner / scope |
| --- | --- |
| Solstice Dark, Solstice style, modern interface | Embedded [kritarc](../../krita/data/kritarc) and interface read fallbacks; details in [Solstice interface](ui-modernization.md) |
| GPU engine enabled | Embedded `Solstice/GpuEngine=true`; code fallback stays false for tests without mounted defaults; [GPU engine](gpu-engine.md) owns runtime selection |
| New document RGBA F32, sRGB profile | Embedded `colorModelDef`, `colorDepthDef`, `colorProfileDef` and default-depth getter; the New Document dialog remembers later choices |
| English interface | Fresh-profile language initialization; imported override file is retained |
| Installed bundle activation | `KisResourceCacheDb::disabledBundles` disables Krita 3, SeExpr examples and RGBA bundles on initial storage registration; Krita 4 remains enabled |
| Initial window layout | Embedded `[MainWindow] State` and [Default.kws](../../krita/data/workspaces/Default.kws) |

Embedded values are the fallback layer for all profiles, including imported
ones with absent keys. Explicit saved values win. The initial disabled-bundle
list also applies when a matching bundle name is first registered later;
it does not reset activation in an imported database. The initial brush and
eraser are supplied by the enabled Krita 4 bundle.

Keep the embedded initial window state and Default workspace aligned.
For an authorized layout update, use the saved workspace's `<state>` in
`[MainWindow] State` and the workspace file named Default in `Default.kws`;
rebuild embedded defaults and install the workspace. Do not replace existing
profiles' saved layouts.

`KisApplication::resetConfig()` marks KConfig clean, backs up `kritarc` as
`kritarc.backup` (replacing the earlier backup), reloads/syncs defaults and
restores the configured/default workspace. The UI requests restart.
It does not reset the profile marker, resource database, other rc files or
custom resource directory contents. Language and display settings live in
separate rc files. Re-running first start is a distinct operation requiring
the actual resolved configuration/profile locations, with Solstice closed.

## Tests, development overrides and regression checks

| Variable | Purpose |
| --- | --- |
| `SOLSTICE_PROFILE_ROOT` | Override the profile root before startup |
| `SOLSTICE_LEGACY_CONFIG_DIR` | Override legacy configuration input |
| `SOLSTICE_LEGACY_RESOURCE_DIR` | Override legacy resource input |

Use separate scratch directories and copies for all three, not real profile
folders. Keep a scratch profile on a drive that permits the relative KConfig
path; otherwise configuration falls back outside that scratch root. Close
Solstice before manual trials because the single-instance key is shared by
profiles of the same user. Do not stop the user's process without approval.

In QStandardPaths test mode the normal Windows profile root is
`%APPDATA%/qttest/<executable base name>/Solstice`, unless explicitly
overridden. This isolates different test programs, not simultaneous runs of
the same executable. Legacy resource lookup uses `qttest/krita`; set explicit
legacy overrides for import fixtures. The previous shared `qttest/Solstice`
caused tests to inherit other programs' databases and resource folders.

Follow [Development workflow](development-workflow.md) for environment setup
and installation. For path/profile implementation changes build `kritaglobal`
and its consumers; startup/default-resource changes also require the owning
application/resource targets and embedded RCC regeneration. Install affected
libraries before interactive tests and restart fully.

```bat
cmake --build <krita-dev-root>\_build --target KisSolsticePathsTest KisSolsticeProfileTest
ctest --test-dir <krita-dev-root>\_build -R "KisSolstice(Paths|Profile)Test$" --output-on-failure
```

- [KisSolsticePathsTest](../../libs/global/tests/KisSolsticePathsTest.cpp):
  layout, override, relative KConfig names and mounted-default behavior.
- [KisSolsticeProfileTest](../../libs/global/tests/KisSolsticeProfileTest.cpp):
  rewrite boundaries/forms, fresh profile, full import with content/timestamp
  comparison, unchanged legacy inputs, custom resources, cancellation/cleanup
  and built-in language selection.
- For resource/default changes, run the applicable resource cache/model/storage,
  locator and bundle tests. For affected consumers, use the feature's regression
  checks (Quick Access, Rest Note, Asset Library, Vision ML, GPU engine).
  See [test environment pitfalls](wiki/pitfalls/build-format-test.md) for MIME
  database/resource setup; historical missing-loader failures are not waivers
  for a new regression.

Manual checks, on scratch profiles:

1. Legacy profile present: Yes imports settings, resources, tags, bundle states,
   workspaces, shortcuts, language, display and feature data; second launch
   does not ask. Verify source contents/timestamps remain unchanged.
2. No starts fresh; Cancel at the initial question quits without profile
   creation. Missing legacy inputs start fresh without asking.
3. Cancel/fail resource copying: verify configuration/default resources/marker
   cleanup, then the next interactive start. Exercise an interrupted Importing
   state, custom ResourceDirectory and a configuration-copy failure separately.
4. Check effective resource read/write paths with `--resource-location`;
   do not infer saveLocation behavior from the locator alone.
5. Reset All Settings: confirm the kritarc backup, default layout and restart
   behavior while other rc files/resources remain. Check log/help dialogs,
   Python Scripter and Python Plugin Manager persistence.
6. Use QML panels and brush previews; confirm resolved cache paths and an
   explicit QML cache override. Run alongside stock Krita using separate
   resource directories. Verify normal startup produces no unintended legacy
   configuration/resource writes.
7. Verify redirected/different-drive configuration placement separately; the
   current automated layout tests do not establish real redirected-folder or
   cross-platform behavior.

## Verification record

The 2026-10-07 development record reports automated path/profile and regression
runs plus user acceptance of real-profile import, repeated startup, side-by-side
Krita, QML/log paths, fresh defaults, cancellation, reset and Python settings.
Early "pending" lists in that record were superseded by its phase 5 results.
The initial missing-loader failures were subsequently fixed through test MIME
setup. The 2026-10-08 per-test-program root refinement and 2026-10-11 layout
alignment are retained in history; later built-in Chinese import-text coverage
is recorded in [Translations](translations.md).

This reference was extracted on 2026-10-16 against
`5ac246e24748d9768eb3b041b5439f4ac68c55ae`. Source and documentation validation
do not constitute a fresh native test run or interactive verification.
The recorded Windows checks do not establish Linux/macOS import UI parity,
different-drive folder-redirection testing or failure-free rollback.
