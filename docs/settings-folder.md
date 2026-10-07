# Settings folder

Solstice keeps all its own files in one folder, separate from Krita:

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

Solstice and Krita no longer share settings, resources or the "already
running" check. Both can be installed and run at the same time.

## First start

On the first start, Solstice looks for a Krita installation's settings
(`%LOCALAPPDATA%\kritarc` or `%APPDATA%\krita`). If it finds them, it asks:

- **Yes**: Solstice copies Krita's settings and resources into its own
  folder, then starts.
  - The message shows the approximate size; a progress window follows.
  - Krita's files are only read and never changed.
  - Old copies of the resource database (`resourcecache.sqlite.1~` and
    similar) are not copied.
- **No**: Solstice starts with its own default settings and resources.
- **Cancel**: Solstice does not start. It asks again next time.

If the copy is cancelled or fails (for example for lack of disk space),
Solstice removes what it had copied, quits, and asks again on the next start.

If the Krita settings use a custom resource folder (the **Resource Folder**
setting in Solstice's preferences), Solstice copies the settings only and
keeps using that folder.

Without Krita settings, Solstice starts with its defaults and does not ask.

## Starting over

To run the first start again, close Solstice and delete (or rename)
`%APPDATA%\Solstice`. **Reset All Settings** (Settings menu) resets the settings
file only.

## Limitations

- Some files are not Solstice's and stay where other programs keep them:
  - temporary files, including autosaves of unsaved documents, in `%TEMP%`;
  - the font cache of the font library (`%LOCALAPPDATA%\fontconfig`);
  - Python's own packages.
- Krita's files in `%LOCALAPPDATA%` and `%APPDATA%\krita` stay as they are.
  Delete them yourself if you no longer use Krita.
- Earlier Solstice versions also used `%LOCALAPPDATA%\krita\cache`. Solstice
  no longer uses it; you can delete it.

Technical notes: [`agent/settings-location.md`](agent/settings-location.md).
