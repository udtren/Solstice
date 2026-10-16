# Settings folder

On Windows, Solstice normally stores its settings and data in this folder:

```
%APPDATA%\Solstice\
  config\      settings (kritarc, display, shortcuts, language, menus and toolbars)
  logs\        usage log, system information, crash log
  resources\   brushes, bundles, workspaces, templates, the resource database,
               Quick Access, Rest Note, Asset Library and Vision ML data
  cache\       brush stroke previews and Qt's interface cache (can be deleted
               at any time while Solstice is closed)
```

`%APPDATA%` is usually `C:\Users\<you>\AppData\Roaming`. Paste
`%APPDATA%\Solstice` into the Explorer address bar to open the folder.

Solstice and Krita use separate settings and default resource folders, and
separate "already running" checks. Both can be installed and run at the same
time. A custom resource folder can remain shared, as described below.

## First start

On the first start, Solstice looks for a Krita installation's settings
(`%LOCALAPPDATA%\kritarc` or a resource database in `%APPDATA%\krita`).
If it finds them, it asks:

- **Yes**: Solstice copies Krita's settings and resources into its own
  folder, then starts.
  - The message shows the approximate size; a progress window follows.
  - Krita's files are only read and never changed.
  - Old copies of the resource database (`resourcecache.sqlite.1~` and
    similar) are not copied.
- **No**: Solstice starts with its own default settings and resources.
- **Cancel**: Solstice does not start. It asks again next time.

If the import is cancelled or fails (for example for lack of disk space),
Solstice attempts to remove the partial settings and default resource copy
before quitting. After successful cleanup, it asks again on the next start.
Log and cache folders can remain.

If the Krita settings use a custom resource folder (the **Resource Folder**
setting in Krita's preferences), Solstice copies the settings only and
keeps using that folder. It remains shared if Krita still uses it, so resource
changes made later in either application can affect both.

Without Krita settings, Solstice starts with its defaults and does not ask.

## Defaults of a new profile

A new profile (no Krita settings, or **No** on the first start) starts with:

- the **Solstice Dark** theme, the **Solstice** widget style and the Solstice
  interface (docker titles, document tabs and toolbars);
- of the bundles that come with Solstice, only **Krita 4 Default Resources**
  enabled.
  - Krita 3 Default Resources, the SeExpr examples and the RGBA brushes are
    installed but disabled.
  - Enable them in **Settings > Manage Resource Libraries**.
- the GPU engine on (see the [GPU engine guide](gpu-engine.md)), and new
  documents in RGBA 32-bit float with the sRGB profile, which the GPU engine
  accelerates. Other color spaces stay available in the New Document dialog,
  which remembers your last choice;
- English as the interface language (change it with **Settings > Switch
  Application Language**).

An imported Krita profile keeps its own bundles and settings. Where the Krita
profile never chose a theme, widget style or interface option, the Solstice
defaults above apply.

## Starting over

To run the first start again with the normal layout, close Solstice and rename
`%APPDATA%\Solstice` to keep it as a backup. Solstice creates a new profile on
the next start. If configuration uses the separate Local folder described
below, rename that configuration folder too. A custom resource folder is
outside these folders and remains in place.

**Reset All Settings** (Settings menu) backs up `kritarc` as
`config\kritarc.backup`, replacing the previous backup, and reloads defaults
and the default workspace. Restart afterwards. This does not repeat the import
question or reset resource data, display settings, shortcuts or language files.

## Limitations

- If Windows stores Roaming and Local AppData on different drives, settings
  (including the first-start marker and menu/toolbar files) use
  `%LOCALAPPDATA%\Solstice\config`. Logs, default resources and cache stay
  under `%APPDATA%\Solstice`.
- An explicitly configured `QML_DISK_CACHE_PATH` can place the interface cache
  outside the profile.
- The first-start question described above is implemented on Windows. The
  current non-Windows startup imports a detected Krita profile without that
  question; this guide does not imply identical import UI on other systems.

- Some files are not Solstice's and stay where other programs keep them:
  - temporary files, including autosaves of unsaved documents, in `%TEMP%`;
  - the font cache of the font library (`%LOCALAPPDATA%\fontconfig`);
  - Python's own packages.
- Import leaves Krita's source files in `%LOCALAPPDATA%` and
  `%APPDATA%\krita` unchanged.
- Earlier Solstice versions used `%LOCALAPPDATA%\krita\cache`. It is no longer
  Solstice's default cache location, but Krita may still use it.
