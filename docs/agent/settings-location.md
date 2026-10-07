# Settings location: design (phase 0)

User guide: [`../settings-folder.md`](../settings-folder.md).

Status (2026-10-07): **phases 1-2 implemented** (path service; Solstice
profile and Krita import); phases 3-5 pending. Phases follow the order the
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
  (`QStandardPaths::isTestModeEnabled()`) it is a `qttest` subfolder, so tests
  never touch real data. A development override, `SOLSTICE_PROFILE_ROOT`,
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

In test mode the profile is `%APPDATA%\qttest\Solstice`.

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

- Move `cacheDir()` (brush stroke previews).
- Test the QML cache override.
- Set the DrMingw log path earlier, so early crashes are logged too.
- Confirm by a file-system scan after a session that nothing new appears in
  `%LOCALAPPDATA%` except the items under "Not redirected".

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
