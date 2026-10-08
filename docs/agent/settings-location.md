# Settings location: design (phase 0)

User guide: [`../settings-folder.md`](../settings-folder.md).

Status (2026-10-07): **complete.** Phases 1-5 are implemented and verified:
the path service, the Solstice profile and Krita import, the remaining Local
files, the Solstice defaults, and the automated and manual checks. Phases follow the order the
user approved.

User decisions (2026-10-07):

- All Solstice files go under `%APPDATA%\Solstice\`, split into `config\`,
  `logs\` and `resources\`.
- Existing Krita settings are **copied**, never moved or modified.
- On the first start, Solstice **asks** whether to take over the Krita
  settings.
- Logs also go to the roaming folder.
- Later (phase 4): Solstice's own defaults and initially enabled bundles.
- Open questions answered (2026-10-07, recommended options):
  - the cache goes to `%APPDATA%\Solstice\cache\`;
  - Krita 4 leftovers (`krita4.xmlgui`, `*.blacklist`, `taskset\`) are
    copied as they are.

## Current state (measured 2026-10-07 on the development machine)

**Solstice shares every file with a stock Krita install.**

- The application name is `krita` (taken from `krita.exe` before
  `KAboutData`), so `AppDataLocation` is `%APPDATA%\krita`.
- The main KConfig file is `applicationName() + "rc"` = `kritarc`.
- `GenericConfigLocation` and `GenericDataLocation` are `%LOCALAPPDATA%`
  itself; they are not scoped to the application.
- The single-instance key is `"Krita5" + home` (`krita/main.cc:256`). Stock
  Krita and Solstice therefore cannot run side by side: the second one hands
  its arguments to the first.

### `%LOCALAPPDATA%`

| File | Size | Written by | Notes |
| --- | --- | --- | --- |
| `kritarc` | 139 KB | KConfig main config (`KisConfig`, `KisImageConfig`, `KoResourcePaths`, Python plugin manager, `KoDocumentInfo` by name) | 18 lines hold drive paths (see "Paths inside kritarc") |
| `kritadisplayrc` | 1 KB | QSettings INI (`main.cc` before the application exists, `kis_config.cc`, `kis_opengl.cpp`, preferences, buginfo) | High DPI, renderer, interface scale |
| `kritashortcutsrc` | 4 KB | `KSharedConfig::openConfig("kritashortcutsrc")` (`kis_action_registry.cpp`, `KisShortcutsEditor.cpp`) | |
| `krita-scripterrc` | <1 KB | `plugins/python/scripter/scripter.py:28` (QSettings + GCL) | |
| `karboncalligraphyrc` | <1 KB | `KConfig("karboncalligraphyrc")` (Calligraphy tool) | |
| `klanguageoverridesrc` | <1 KB | `libs/widgetutils/xmlgui/kswitchlanguagedialog_p.cpp` (startup function), `main.cc:387` | key `[Language] krita=` |
| `krita.log`, `krita-sysinfo.log` | 25 / 19 KB | `KisUsageLogger` (GDL), opened at `main.cc:525` | |
| `kritacrash.log` | 373 KB | DrMingw, configured at `main.cc:762` (GCL) | crashes before that line are not logged |
| `krita\cache\` | 11.5 MB, 847 files | `CacheLocation`: brush stroke previews (`KisBrushStrokePreviewCache.cpp:69`), Qt's QML disk cache | regenerable |
| `kritarc_20260124`, `kritarc_20260206` | | the user's manual backups | not application files |

### `%APPDATA%\krita` (resources and user data, 2.7 GB)

- **Resources:** about 40 resource folders, bundles (`*.bundle`, `*.abr`),
  `*.bundle_modified` folders, `KRITA_RESOURCE_VERSION` (`6.0.5-prealpha`),
  `krita5.xmlgui`, `*.blacklist` files from Krita 4.
- **Resource database:** `resourcecache.sqlite` (419 MB).
- **Database backups:** `resourcecache.sqlite.1~`-`4~`, 1.1 GB together.
  Krita leaves them behind after schema updates.
- **Solstice features:**
  - `quickaccess\` (and the legacy `quick_access_manager\`);
  - `rest_note\`;
  - `krita_asset_library\`;
  - `lazy_tools\`;
  - `visionml\` (420 MB of models).
- **Python:** `pykrita\` (user plugins and their settings).
- **Other user data:**
  - `input\` profiles, `authorinfo\`, `workspaces\`, `sessions\`,
    `windowlayouts\`, `templates\`;
  - `shortcuts\` (schemes), `color\icc`, `predefined_image_sizes\`.

Without the database backups, a copy is about 1.6 GB.

### Resource database

- The schema is 0.0.18 (`version_information`; written by `5.3.0-prealpha`).
- Counts: 25 storages, 2678 resources, 44 tags, 1986 tag links.
- **Storage locations are relative to the resource folder** (e.g.
  `Krita_4_Default_Resources.bundle`, `bundles/Deevad_2021.bundle`). A check
  of every text column found no absolute paths.
- The database therefore stays valid when the whole resource folder is
  copied. Tags, enabled and disabled bundles (`storages.active`) and resource
  versions survive the copy.

### Paths inside `kritarc`

These lines point into `%APPDATA%\krita` and must be rewritten on import:

| Key | Format |
| --- | --- |
| `ResourceDirectory` | `C:/Users/<u>/AppData/Roaming/krita` |
| `AlwaysUseTemplate`, `FullTemplateName[$e]` | forward slashes; `$HOME/AppData/Roaming/krita/...` (KConfig `$e` expansion) |
| `pluginDevToolsSettings` (JSON) | forward slashes inside JSON |
| `mask_set` (pigment_o plugin) | `C:\\Users\\<u>\\AppData\\Roaming\\krita\\...` (escaped backslashes) |

The other drive paths are recent folders, export locations, `swaplocation`
(`%TEMP%`) and `blenderPath`. They stay as they are.

### Where paths are decided (code)

About 55 source files build these paths. The full inventory is
summarized here; `file:line` references are from 2026-10-07.

- **Main config:** `KSharedConfig::openConfig()` with no name. It is first
  opened by a static startup function (`KisAnimAutoKey.cpp:73` ->
  `KisImageConfig`, `kis_image_config.cpp:35`) inside the temporary
  `QCoreApplication` at `main.cc:384`, and then again at `main.cc:511`; all
  of this happens before `KisApplication` exists. `KConfig::setMainConfigName`
  is not called anywhere.
- **Named rc files:**
  - `"kritashortcutsrc"` (`kis_action_registry.cpp:155, 454`,
    `KisShortcutsEditor.cpp:226`);
  - `"karboncalligraphyrc"` (`KarbonCalligraphyOptionWidget.cpp:47`);
  - `KConfig("kritarc")` (`KoDocumentInfo.cpp:250`, bypasses the main
    config name).
- **Hard-coded `GenericConfigLocation + "/<file>"`:**
  - `kritadisplayrc`: `main.cc:369, 880`; `kis_config.cc` (6 places);
    `kis_opengl.cpp:464`; `kis_dlg_preferences.cc:394, 440, 3033`;
    `KisDlgCustomTabletResolution.cpp:79, 122`; `dlg_buginfo.cpp:66`;
  - `kritarc` for reset (`KisApplication.cpp:1361`);
  - the crash log (`main.cc:125`, `KisUsageLogger.cpp:342`,
    `DlgCrashLog.cpp:22`);
  - `klanguageoverridesrc` (`kswitchlanguagedialog_p.cpp:46-53`);
  - the scripter (`scripter.py:28`).
- **Logs:** `GenericDataLocation`
  - `KisUsageLogger.cpp:60-64`;
  - `DlgKritaLog.cpp:18`, `DlgSysInfo.cpp:18`;
  - `kbugreport.cpp:172-188`.
- **Resources:**
  - `KoResourcePaths::getAppDataLocation()`: `--resource-location`, then the
    `ResourceDirectory` key, then `AppDataLocation`;
  - `KoResourcePaths::saveLocation()`: `ResourceDirectory`, then
    `AppDataLocation`; it ignores `--resource-location`;
  - every resource, database, Solstice feature and Python path goes through
    one of these two.
- **KXmlGui local files:** `writableLocation(AppDataLocation)/kxmlgui5/krita/`
  (`kxmlguiclient.cpp:166`, `kxmlguifactory.cpp:161`, `kedittoolbar.cpp:709`,
  `kxmlguiversionhandler.cpp:297`); these ignore `ResourceDirectory`. Note:
  `KisMainWindow.cpp:638, 3748` keeps `krita5.xmlgui` in the resource folder
  instead.
- **Cache:** `CacheLocation` (`KisBrushStrokePreviewCache.cpp:69`; Qt's QML
  disk cache).

The xmlgui and language-switch code is built from this repository
(`libs/widgetutils/xmlgui/`), so it can be changed.

### Not redirected (by design or impossible)

| Item | Reason |
| --- | --- |
| `kdeglobals` (read only) | KConfig cascade; not a Solstice file and absent on Windows |
| `%LOCALAPPDATA%\fontconfig\cache` | set by the dependency's `fonts.conf`; shared with other fontconfig users |
| Python user site-packages (`%APPDATA%\Python`) | CPython's own; not Solstice data |
| `%TEMP%`: autosaves of untitled documents, swap, frame cache, single-instance lock, `krita-opengl.txt` | temporary by nature; swap and animation cache folders are user settings |
| Backup files, recorder output (`%USERPROFILE%\KritaRecorder`) | user documents, configurable |
| MSIX virtualization | OS-level; Solstice is not packaged as MSIX |
| Qt QML disk cache | inside Qt (`CacheLocation\qmlcache`). Check in phase 3 whether Qt 6.8 honours `QML_DISK_CACHE_PATH`; otherwise it stays in `%LOCALAPPDATA%\krita\cache` |

## Target layout

```
%APPDATA%\Solstice\
  config\     kritarc, kritadisplayrc, kritashortcutsrc, krita-scripterrc,
              karboncalligraphyrc, klanguageoverridesrc,
              kxmlgui5\krita\*.xmlgui, SOLSTICE_PROFILE (state marker)
  logs\       krita.log, krita-sysinfo.log, kritacrash.log
  resources\  everything that is in %APPDATA%\krita today, including
              resourcecache.sqlite and the Solstice feature folders
  cache\      brush stroke previews (and the QML cache if Qt allows)
```

- File names stay the same. Only the directories change, so the embedded
  defaults (`:/kconfig/kritarc`, matched by name) and existing code keep
  working.
- `cache\` was added to the user's three folders (approved 2026-10-07), so
  that the remaining `%LOCALAPPDATA%\krita` files also move.

## Design

### One path service (phase 1)

`KisSolsticePaths`, in `libs/global` (the lowest layer, usable before any
application object):

- `root()`: `%APPDATA%\Solstice`, from `SHGetKnownFolderPath(
  FOLDERID_RoamingAppData)`. It needs no Qt application. In test mode
  (`QStandardPaths::isTestModeEnabled()`) it is `qttest\<test program>\Solstice`
  (since 2026-10-08; before, one `qttest\Solstice` shared by all tests), so
  tests never touch real data or each other's resources. A development override, `SOLSTICE_PROFILE_ROOT`,
  allows manual trials in a scratch folder.
- `configDir()`, `logDir()`, `resourceDir()`, `cacheDir()`, and
  `configFile(name)` for absolute rc paths.
- Phase 1 returns **today's locations**: `%LOCALAPPDATA%` for config and
  logs, `%APPDATA%\krita` for resources, `CacheLocation` for the cache. All
  callers switch to the service without any change in behavior; tests
  confirm this. Phase 2 then flips the service in one place.

Callers to switch:

- every `GenericConfigLocation + "/kritadisplayrc"` site;
- the named rc files (as absolute paths);
- `KoDocumentInfo`'s `KConfig("kritarc")`;
- the config reset;
- the logs and the crash log;
- `klanguageoverridesrc`;
- the scripter (Python: through a small `Krita` API call, or an environment
  variable set at startup);
- the KXmlGui local directory;
- `KoResourcePaths`' `AppDataLocation` fallback (`getAppDataLocation()` and
  `saveLocation()`);
- `KisBrushStrokePreviewCache`.

**The main config cannot be absolute.** KConfig 6.7.0
(`KConfigPrivate::changeFileName()`) builds the main config file as
`writableLocation(GenericConfigLocation) + '/' + mainConfigName()`. It also
reads the defaults from `":/kconfig/" + fileName`. An absolute name breaks
both. About 300 call sites (and KDE Frameworks code) use
`KSharedConfig::openConfig()` without a name, so replacing them is not
practical either.

Instead, `KisSolsticePaths::kconfigName()` returns a name **relative to
GenericConfigLocation**. Phase 2 makes it `../Roaming/Solstice/config/kritarc`;
KConfig appends that to `%LOCALAPPDATA%`, and Qt resolves the `..`. Qt also
cleans the defaults path `:/kconfig/../Roaming/Solstice/config/kritarc` to
`:/Roaming/Solstice/config/kritarc`, so phase 2 registers the embedded
defaults under that path too. `KisSolsticePathsTest` verifies both behaviors.
`main()` calls `KConfig::setMainConfigName(KisSolsticePaths::mainConfigName())`
before the temporary `QCoreApplication`, whose startup functions open the
main config first.

If `%APPDATA%` and `%LOCALAPPDATA%` are on different drives (folder
redirection), no relative path exists. Phase 2 must detect this and fall back,
with the main config left in `%LOCALAPPDATA%\Solstice\config\`, and record it.

The single-instance key changes from `"Krita5"` to `"Solstice"` (phase 1),
so that stock Krita and Solstice can run at the same time.

### Phase 1 result (2026-10-07)

`libs/global/KisSolsticePaths.{h,cpp}`. Every function returns the location
used so far; `KisSolsticePathsTest` checks this, plus the two KConfig
behaviors above. Callers switched:

| Area | Files |
| --- | --- |
| Main config name, display and language settings, crash log, single-instance key (`"Solstice"`), `SOLSTICE_CONFIG_DIR` | `krita/main.cc` |
| `kritadisplayrc` | `kis_config.cc` (12 places), `kis_opengl.cpp`, `kis_dlg_preferences.cc`, `KisDlgCustomTabletResolution.cpp`, `dlg_buginfo.cpp` |
| Named rc files | `kis_action_registry.cpp`, `KisShortcutsEditor.cpp` (`kritashortcutsrc`), `KarbonCalligraphyOptionWidget.cpp` (6 places), `KoDocumentInfo.cpp` (`kritarc`) |
| Config reset | `KisApplication.cpp` |
| Language override | `kswitchlanguagedialog_p.cpp` |
| Logs | `KisUsageLogger.cpp`, `DlgCrashLog.cpp`, `DlgKritaLog.cpp`, `DlgSysInfo.cpp`, `kbugreport.cpp` |
| Resource folder fallback | `KoResourcePaths.cpp` (`getAppDataLocation()`, `saveLocationInternal()`), `kis_dlg_preferences.cc` |
| KXmlGui local files | `kxmlguiclient.cpp` (write path; lookup checks the local file first), `kxmlguifactory.cpp`, `kedittoolbar.cpp`, `kxmlguiversionhandler.cpp` |
| Cache | `KisBrushStrokePreviewCache.cpp` |
| Python scripter | `plugins/python/scripter/scripter.py` (reads `SOLSTICE_CONFIG_DIR`) |

Left as they are, on purpose:

- Android, macOS and Microsoft Store branches;
- reads of installed data (`ui_standards.xmlgui`, translations,
  `genericdata`);
- `KoResourcePaths`' `cleanup()`/`cleanupDirs()`, which drop
  `AppDataLocation` entries when the resource folder is elsewhere. In
  phase 2 they keep stock Krita's `%APPDATA%\krita` out of the resource
  search.

The only visible change in phase 1 is the single-instance key: Solstice and
stock Krita no longer hand files to each other.

Tests:

- `KisSolsticePathsTest` 5/5, `KisGlobalTest` 19/19, `TestResourceCacheDb`
  5/5, `QuickAccessCoreTest` 9/9.
- `TestResourceLocator` 26/27: the same failure as before this change (6
  resources instead of 7; the symbols loader is missing in the test
  environment).
- `TestResourceStorage` 4/6: it fails on a storage made from an empty string
  and on the missing patterns loader. It uses explicit test folders, not
  these paths, and was not run before the change.

### Phase 2 result (2026-10-07)

**Paths.** `KisSolsticePaths` returns the profile layout:

- `config\`; KXmlGui's local files go to `config\kxmlgui5`;
- `logs\`, including `kritacrash.log`;
- `resources\` (the default resource folder);
- `cache\`.

`kconfigName()` is relative to GenericConfigLocation. On another drive,
`configDir()` falls back to `%LOCALAPPDATA%\Solstice\config`.

Overrides for trials and tests:

- `SOLSTICE_PROFILE_ROOT` (the profile folder);
- `SOLSTICE_LEGACY_CONFIG_DIR` and `SOLSTICE_LEGACY_RESOURCE_DIR` (the
  Krita profile to import).

In test mode the profile is `%APPDATA%\qttest\<test program>\Solstice`. A shared
`qttest\Solstice` (until 2026-10-08) let tests inherit each other's resource
folders and database: `KisBrushModelTest` failed because an earlier test had
created the resource folder without `brushes\`.

**kritarc defaults.**

- `cmake/modules/SolsticeEmbedRcc.cmake` compiles `krita/kritarc-defaults.qrc`
  (`kritarc` at the root) with `rcc --binary`. It embeds the result as
  `solsticeKritarcDefaultsRcc` (via `SolsticeEmbedRccToCpp.cmake`).
- `KisSolsticePaths::registerMainConfigDefaults()` mounts it with
  `QResource::registerResource(data, mapRoot)` at the folder of the cleaned
  `":/kconfig/" + mainConfigName()`, for any profile location.
- The original `:/kconfig/kritarc` stays in `krita.qrc`.

**Profile and import.** `libs/global/KisSolsticeProfile.{h,cpp}`:

- State marker `config\SOLSTICE_PROFILE`: `importing` or `ready`; a missing
  marker means no profile.
- `legacyProfileExists()`: `%LOCALAPPDATA%\kritarc`, or `%APPDATA%\krita` with
  `resourcecache.sqlite`.
- `importConfiguration()`:
  - copies `kritarc`, `kritadisplayrc`, `kritashortcutsrc`,
    `krita-scripterrc`, `karboncalligraphyrc` and `klanguageoverridesrc`;
  - in `kritarc`, rewrites references to `%APPDATA%\krita` to
    `resources\` (`rewritePaths()`) — forward slashes, backslashes, escaped
    backslashes and `$HOME/...`, matching whole folder names only (spaces do
    not end a name, so `krita - Copy` is kept);
  - copies `%APPDATA%\krita\kxmlgui5` to `config\kxmlgui5`;
  - marks the profile `importing`, or `ready` when no resources follow.
- `resourcesToImport()`: true unless the Krita `kritarc` sets a custom
  `ResourceDirectory` (kept as it is, not copied, not rewritten).
- `planResourceCopy()`: every file except `resourcecache.sqlite.N~` and
  `kxmlgui5/`.
- `copyResources()`:
  - checks free space (size plus 64 MB);
  - copies in 4 MB chunks with progress and cancel;
  - keeps modification times (the resource database compares them);
  - marks the profile `ready`. On failure or cancel it removes `resources\`.
- `abandonImport()` removes `config\` and `resources\` (and so the marker).

**Startup** (`krita/main.cc`):

1. `registerMainConfigDefaults()`, then `prepareSolsticeProfile()` before
   `KConfig::setMainConfigName()` and the temporary `QCoreApplication`:
   - with no marker and a Krita profile, a `MessageBoxW` (Yes = import,
     No = fresh, Cancel = quit) whose text is built in, in Japanese or
     English, from the Krita language override or the Windows UI language;
   - with no Krita profile, a fresh profile;
   - batch runs (`--export*`) do not ask and create nothing.
2. `finishSolsticeImport()` after the single-instance check, before the splash
   and `KisApplication::start()`: copies the resources with a modal
   `QProgressDialog`. On failure or cancel it calls `abandonImport()`, warns,
   and quits.

`KoResourcePaths`' `cleanup()` drops `%APPDATA%\krita` (stock Krita's
`AppDataLocation`) from resource searches, since the resource folder is
elsewhere.

**Tests:**

- `KisSolsticePathsTest` 6/6: layout, override, relative KConfig name, and
  the main config with mounted defaults.
- `KisSolsticeProfileTest` 7/7: path rewriting (all forms, `krita - Copy`,
  `kritaX`), fresh profile, full import, custom resource folder, and a
  cancelled copy with rollback. It also checks that contents and
  modification times match, and that the Krita files are unchanged.
- The regression set as in phase 1 shows no change: `TestResourceLocator`
  26/27 and `TestResourceStorage` 4/6, the same failures as before.

**Manual checks (pending):**

- First start with the Krita profile, answering Yes: progress window; brushes,
  bundles (enabled and disabled), tags, workspaces, shortcuts, display and
  language settings, Quick Access, Rest Note, Asset Library and Vision ML
  models all as before; `Help > Show system information` shows the logs.
- Second start: no question.
- Answering No: Solstice's defaults (bundles from the installation).
- Cancel at the question, and cancel during the copy: on the next start the
  question appears again; `%APPDATA%\krita` is unchanged.
- Krita and Solstice running at the same time.

To start over during testing, close Solstice and delete
`%APPDATA%\Solstice`. For trials without touching it, set
`SOLSTICE_PROFILE_ROOT` (and the legacy overrides, pointing at copies).

### Phase 3 result (2026-10-07)

A scan after the first session with the phase 2 build found nothing new in
`%LOCALAPPDATA%` except `Temp`; `%APPDATA%\krita` was unchanged. Phase 3
covers what that session did not exercise:

- **QML disk cache.** Qt 6.8's `Qt6Qml.dll` reads `QML_DISK_CACHE_PATH`.
  `krita/main.cc` sets it to `cache\qmlcache` before any application object
  exists, unless the variable is already set. Before this, the cache was
  `%LOCALAPPDATA%\krita\cache\qmlcache`.
- **Crash log.** `tryInitDrMingw()` used
  `QCoreApplication::applicationDirPath()`, and was only called after
  `KisApplication`, `KAboutData` and the argument parsing. It now takes the
  folder from `GetModuleFileNameW()` and runs right after the profile is
  prepared (it creates `logs\` itself). Crashes during the rest of startup
  are logged to `logs\kritacrash.log` too.
- **`cache` resource type.** `KoResourcePaths::saveLocationInternal()` maps
  `CacheLocation` to `KisSolsticePaths::cacheDir()`. No caller uses it today;
  this keeps future ones out of `%LOCALAPPDATA%`.

Left in `%LOCALAPPDATA%`, by design (see "Not redirected"):

- `Temp`: autosaves of untitled documents, swap, single-instance lock;
- `fontconfig\cache`;
- Krita's own files.

`%LOCALAPPDATA%\krita\cache` (brush stroke previews and the QML cache of
earlier Solstice builds) is no longer used and can be deleted by the user.

Manual check passed (2026-10-07, user): after using the Text Properties
docker (QML), `cache\qmlcache` held 43 files; since the install only `Temp`
changed in `%LOCALAPPDATA%`, and `%LOCALAPPDATA%\krita` and `%APPDATA%\krita`
were unchanged.

### Phase 4 result (2026-10-07)

User decisions:

- of the installed bundles, only `Krita_4_Default_Resources` is enabled;
- everything else follows the recommendations below.

The defaults were inventoried first:

- Solstice options are mostly `readEntry()` fallbacks; none were in the
  embedded `kritarc`.
- The installation ships four bundles:
  - `Krita_3_Default_Resources`: 131 presets, 170 brushes;
  - `Krita_4_Default_Resources`: 117 presets, 79 brushes, 81 patterns;
  - `Krita_Artists_SeExpr_examples`: 34 SeExpr scripts;
  - `RGBA_brushes`: 6 presets.
- The ship also includes loose resources (39 presets, 112 patterns, 69
  templates, 9 workspaces and others).

| Default | Before | Now | Where |
| --- | --- | --- | --- |
| Enabled bundles in a new resource database | all except Krita 3 | Krita 4 only | `KisResourceCacheDb::disabledBundles` (+ SeExpr examples, RGBA brushes) |
| Theme | Krita dark | Solstice Dark | embedded `krita/data/kritarc` `[theme]` |
| Widget style | none (Breeze, then Fusion) | Solstice | embedded `kritarc` top-level `widgetStyle` |
| Solstice interface | off | on | embedded `kritarc` `Solstice/ModernInterface`; fallbacks in `KisMainWindow.cpp`, `kis_dlg_preferences.cc` (also its "restore defaults") |
| First-time resource message | "Krita is running for the first time..." | "Solstice is setting up its resources..." | `KisResourceLocator::firstTimeInstallation()` |
| GPU engine (user, 2026-10-07) | off | on | embedded `kritarc` top-level `Solstice/GpuEngine=true`; "restore defaults" in `KisGpuEngineUi.cpp`. The code fallback in `KisGpuEngineSettings::enabledInConfig()` stays `false`: test programs do not mount the embedded defaults, and a `true` fallback would enable the engine in every image test |
| New document (user, 2026-10-07) | RGBA 8-bit sRGB | RGBA 32-bit float, `sRGB-elle-V2-srgbtrc.icc` | embedded `kritarc` top-level `colorModelDef`, `colorDepthDef=F32`, `colorProfileDef`; `KisConfig::defaultColorDepth(true)`. The dialog still offers every color space, and `KisDocument` remembers the last choice |
| Language (user, 2026-10-07) | system language | English | `KisSolsticeProfile::createFreshProfile()` writes `[Language] krita=en_US` (QByteArray, as `kswitchlanguagedialog_p.cpp` does) to `klanguageoverridesrc`; new profiles only, imported ones keep theirs |

Kept as they are (recommended):

- docker locks off;
- no brush preset grouping;
- the upstream renderer, tablet (WinTab), cursor, autosave (7 min), undo
  (200), new document size (A4 at 300 ppi) and welcome-page news (off)
  defaults;
- the window layout from the embedded `[MainWindow] State`.

Effects:

- The bundle list applies when a storage is first registered: new profiles,
  and bundles a user adds with one of these names. Imported resource
  databases keep their own enabled and disabled bundles.
- Embedded `kritarc` entries are the fallback layer for every profile.
  Imported profiles that never set the theme, style or Solstice interface
  therefore get the Solstice ones; values that were set are kept.
- The first-start presets ("b) Basic-5 Size Opacity", "a) Eraser Circle")
  are in `Krita_4_Default_Resources`, so the first start still selects them.

Inconsistencies found and left for later:

- Quick Access settings dialog size: the fallback was 340x480 when read and
  550x650 on legacy import. Resolved (2026-10-08): 550x650 in both
  (`DefaultSettingsDialogWidth`/`Height` in `QuickAccessDock.cpp`).
- `OpenGLRenderer`: the fallback was `angle` in `main.cc` on Windows but
  `auto` in `kis_opengl.cpp` and `kis_config.cc`. This is resolved
  (2026-10-07): it is `auto` everywhere, and on Windows "Auto" prefers desktop
  OpenGL when the GPU engine is on, so the new default (engine on) gets GL
  interop (`docs/agent/gpu-engine.md`).

Tests:

- `TestResourceCacheDb` 5/5, `TestResourceModel` 19/19, `TestStorageModel`
  7/7.
- `TestResourceLocator` 26/27 and `TestBundleStorage` 8/9 fail only on
  missing resource loaders in the test environment ("Could not create loader
  for ..."), as before.

Manual check (pending): start a new profile without touching the real one,
by closing Solstice and starting it with `SOLSTICE_PROFILE_ROOT` and both
`SOLSTICE_LEGACY_*` variables pointing at empty scratch folders. Expected:

- no import question;
- Solstice Dark, the Solstice style and the Solstice interface;
- in the bundle manager, only Krita 4 enabled;
- "b) Basic-5 Size Opacity" selected.

### Phase 5: verification

**Automated (2026-10-07).** All passed:

| Area | Suites |
| --- | --- |
| Profile | `KisSolsticePathsTest` 6, `KisSolsticeProfileTest` 7 |
| Global | `KisGlobalTest` 19, `KisSignalAutoConnectionTest` 7, `KisSignalCompressorTest` 10 |
| Resources | `TestResourceCacheDb` 5, `TestResourceModel` 19, `TestStorageModel` 7, `TestTagModel` 15, `TestTagFilterResourceProxyModel` 12, `TestResourceTypeModel` 5, `TestFolderStorage` 7, `TestMemoryStorage` 8, `TestResourceLoaderRegistry` 3 |
| GPU engine | `KisGpuPaintDeviceTest` 251 (1 skipped: opt-in benchmark), `KisGpuProjectionTest` 199, `KisGpuEngineTest` 11, `kis_liquify_transform_worker_test` 15 |
| UI | `QuickAccessCoreTest` 9, `KisSolsticeStyleTest` 7 |

`TestResourceLocator` and `TestBundleStorage` kept their pre-existing
missing-loader failures at the time. They were fixed afterwards (2026-10-07):
the test programs lacked Qt's MIME database
(`docs/agent/wiki/pitfalls/build-format-test.md`, "Tests").

**Manual, already confirmed by the user (2026-10-07):**

- import with the real Krita profile, second start, side by side with Krita
  (phase 2);
- no new files in `%LOCALAPPDATA%` and the QML cache in the profile
  (phase 3);
- a new profile without a Krita profile, with the Solstice defaults
  (phase 4).

**Manual, confirmed by the user (2026-10-07).** Each ran on a scratch
profile.
`SOLSTICE_PROFILE_ROOT` is set in PowerShell, then `run-krita.bat` starts
Solstice. Starting `krita.exe` directly does not work without the
development environment.

1. Krita profile present, **No**: Solstice defaults; the real Krita and
   Solstice profiles are unchanged.
2. **Cancel** at the question: nothing is created; the next start asks again.
3. **Yes**, then **Cancel** during the copy: warning, then exit;
   `config\` and `resources\` are removed; the next start asks again.
4. **Reset All Settings** in a scratch profile: the backup is
   `config\kritarc.backup`; after a restart the Solstice defaults apply.
5. Help > Show system information, Show Krita log and Show crash log (for bug reports): they
   read from `logs\`.
6. Python: the Scripter remembers its settings (`config\krita-scripterrc`),
   and the Python Plugin Manager's choices persist (`kritarc` `[python]`).

### First start and import (design, phase 2)

The import decision has to come before any configuration is read.
`kritadisplayrc`, the language and `kritarc` are all read before
`KisApplication` exists (`main.cc:369`-`525`). Startup order:

1. **Top of `main()`:**
   - read `config\SOLSTICE_PROFILE`; if it says `ready`, continue normally;
   - otherwise, if a Krita profile exists (`%LOCALAPPDATA%\kritarc`, or
     `%APPDATA%\krita` containing `resourcecache.sqlite`), show a **native
     Windows task dialog** (no Qt needed): "Use your Krita settings and
     resources in Solstice?", with **Import** and **Start fresh**;
   - the dialog text is built in, in English and Japanese, chosen from the
     Krita language override or the Windows UI language;
   - with no Krita profile, start fresh without asking.
2. **Import, small files (immediately, still before Qt):**
   - copy the rc files into `config\`, rewriting the `kritarc` paths listed
     above to the new resource folder (all three formats); `ResourceDirectory`
     is rewritten only when it points to `%APPDATA%\krita`;
   - write `SOLSTICE_PROFILE` = `importing`;
   - the logs are not copied: Solstice starts new logs.
3. **Import, resources (after `KisApplication` exists, before
   `registerResources()` opens the database):**
   - copy `%APPDATA%\krita` to `resources\` with a progress dialog, skipping
     `resourcecache.sqlite.N~` backups (and later, if the user agrees,
     `krita4.xmlgui` and the Krita 4 `*.blacklist` files);
   - check free space first;
   - on success write `SOLSTICE_PROFILE` = `ready`.
   - On failure or cancel, remove `resources\` and the marker. The next
     start asks again. The Krita files are never touched.
4. **Start fresh:**
   - create the folders and write `ready`;
   - the existing first-time installation (`KisResourceLocator::
     firstTimeInstallation()`) fills `resources\` from the installed bundles;
   - phase 4 replaces those defaults with Solstice's.

A custom `ResourceDirectory` (outside `%APPDATA%\krita`) is kept as it is.
The import does not copy it, because the user chose that folder explicitly.
The log notes it.

### Remaining files (phase 3)

Implemented; see "Phase 3 result".

### Defaults and bundles (phase 4)

Inventory first, then the user chooses:

- installed resources: `share/krita/bundles` holds
  `Krita_3_Default_Resources`, `Krita_4_Default_Resources`,
  `Krita_Artists_SeExpr_examples` and `RGBA_brushes`, plus the loose resource
  folders;
- the embedded `krita/data/kritarc` defaults;
- the Solstice options (GPU engine, interface, docker locks, Quick Access
  layout).

Apply them only to fresh profiles: an imported profile keeps the user's
values. Rebrand `firstTimeInstallation()`'s "Krita is running for the first
time" message.

### Verification (phase 5)

Run each case with `SOLSTICE_PROFILE_ROOT` in a scratch folder and a copy of
the Krita profile, then once on the real profile:

- a Krita profile exists, and the user imports it;
- a Krita profile exists, and the user starts fresh;
- no Krita profile exists;
- the import is cancelled or fails halfway;
- second and later starts;
- stock Krita afterwards: its files are unchanged, and it runs side by side
  with Solstice;
- the configuration reset (`KisApplication.cpp:1361`) and
  `--resource-location`;
- Python plugins (`pykrita` paths, scripter);
- Help > Show system information shows the new log locations.
