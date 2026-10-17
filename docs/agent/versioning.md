# Versioning: technical notes

User guide: [`../versioning.md`](../versioning.md). Decision record:
[`wiki/decisions/versioning.md`](wiki/decisions/versioning.md).

Added 2026-10-07 at the user's request: Solstice shows its own version,
starting at `0.1.0-alpha`.

## Two versions

| Version | Value | Defined in | Used for |
| --- | --- | --- | --- |
| Solstice version | `0.4.0` | top-level `CMakeLists.txt`: `SOLSTICE_VERSION_MAJOR/MINOR/PATCH/LABEL/BUILD` -> `SOLSTICE_VERSION_STRING` | everything shown to users |
| Krita compatibility version | `6.0.5-prealpha` | top-level `CMakeLists.txt`: `KRITA_VERSION_STRING` and related | file formats, resources, scripting, library versions |

`libs/version/kritaversion.h.cmake` defines both. `KritaVersionWrapper`
provides:

- `versionString(checkGit)`: the Krita compatibility version (unchanged API);
- `solsticeVersionString(checkGit)`: the Solstice version, with
  `" (git <sha>)"` when `checkGit` is true and the build has a git hash.

## Where each is used

**Solstice version:**

- `KAboutData` in `krita/main.cc`, and so `qApp->applicationVersion()`: the
  splash screen and About dialog, the KBugReport dialog, the network
  User-Agent, Spriter export's `generator_version`;
- bug information (`plugins/extensions/buginfo/dlg_buginfo.cpp`) and the usage
  log (`libs/global/KisUsageLogger.cpp`), each followed by
  "Based on Krita: <compatibility version>";
- XMP `CreatorTool` of exported images ("Solstice <version>",
  `libs/metadata/kis_meta_data_filter_p.cc`);
- Windows `versioninfo.rc`:
  - `FILEVERSION`/`PRODUCTVERSION` are `MAJOR,MINOR,PATCH,BUILD`;
  - the version strings carry the label and git hash;
- the application manifests (`krita/krita*.exe.manifest.in`,
  `kritarunner*.exe.manifest.in`).

**Krita compatibility version (keep it):**

- `.kra` `kritaVersion` attribute. `plugins/impex/libkra/kra_converter.cpp`
  writes it, and `KisKraLoader` chooses compatibility behavior from it. A
  lower number would make files look like they came from an old Krita, in
  Solstice and in Krita.
- `KRITA_RESOURCE_VERSION` in the resource folder
  (`libs/resources/KisResourceLocator.cpp`): bundled resources are
  reinstalled only when the application version is greater than the stored
  one.
- The resource database `version_information` table
  (`KisResourceCacheDb.cpp`) and bundle `generator` metadata.
- Python `Krita.version()` (`libs/libkis/Krita.cpp`): scripts compare it.
- Library `VERSION`/`SOVERSION` (`GENERIC_KRITA_LIB_VERSION*`), the
  `kritaversion` command-line tool, `KisManualUpdater`. The updater compares
  with krita.org releases, is not used for Solstice, and is left unchanged.
- The unused NSIS/MSIX/AppImage packaging inputs.

## Bumping the version

Edit only the `SOLSTICE_VERSION_*` values in the top-level `CMakeLists.txt`:

- `MINOR` for a milestone, `PATCH` for fixes only (pre-1.0 semantic
  versioning);
- `LABEL`: empty for a release (including 0.3.0); use `alpha`, `beta.N`
  or `rc.N` only for an explicitly designated prerelease;
- `BUILD`: the fourth Windows file-version digit, for rebuilds of the same
  version; otherwise 0.

Do not change `KRITA_VERSION_STRING` unless the compatibility consequences
above are intended. The Windows file version is numeric, so the label only
appears in the version strings.

## Releasing

1. On a feature branch based on `main`, bump the version (above), record it
   under History, commit and push that branch. Merge its pull request into
   `main`. Do not push directly to `main` (user decision, 2026-10-16).
2. Run the "Windows x64 trial build" workflow on `main`
   (`gh workflow run windows-build.yml --ref main`) and wait for it to
   succeed (`docs/agent/github-actions.md`).
3. Create the GitHub Release `v<version>` at the built commit, as a
   prerelease while the version has a label. Attach the run's application
   ZIP, `-dbg.zip`, `SHA256SUMS.txt`, `BUILD-INFO.txt` and
   `windows-dependencies.lock.json`, and follow the previous release's
   notes (what's new, download, highlights, supported environment, build
   commit, run and SHA-256).
   Verify the downloaded ZIPs and checksums before publishing. Keep README's
   new-feature summary marked `In development` until the release exists;
   then replace it with the released version and link, update the user-guide
   index, and publish those documentation changes through another feature-branch
   pull request. Keep only the latest release
   summary in README; older release notes remain in GitHub Releases.
4. Update the website the same day (user decision, 2026-10-14), in
   `https://github.com/udtren/tidalboxstudio-web`:
   - on `develop`, set `links.release`, `version` and `versionName` in
     `solstice/src/site.ts` (the only place the version appears);
   - in `solstice/`, run `npm run check` and `npm run build`, and confirm
     the new version in `dist/index.html` and `dist/download/index.html`;
   - commit and push `develop`, then, after the GitHub Release exists,
     merge its pull request into `main`. Cloudflare Workers Builds
     publishes `main` to `https://solstice.tidalboxstudio.com`; check the
     live Download page shows the new version and its link opens the
     release.

## History

| Version | Date | Reason |
| --- | --- | --- |
| `0.4.0` | 2026-10-17 | Reference Tool native non-destructive filter stacks and cross-document templates (PRs #22 and #23), with an illustrated overview and Highlight 09. Regular GitHub Release `v0.4.0` at `88386550c1dd` (version PR #24, run 38998728479); [package verification](wiki/history/release-0.4.0.md). Krita compatibility version is unchanged. |
| `0.3.0` | 2026-10-17 (JST) | User-requested release without the Beta label. Includes PR #5 (GPU Clone Layer offsets and Move Tool X/Y guard), #7 (partial-tile GPU projection clears), #8 (provisional layer thumbnails), and repository documentation maintenance/checks. Released as regular GitHub Release `v0.3.0` at `f947bc31421b` (PR #20, run 38915897544); [package verification](wiki/history/release-0.3.0.md). Krita compatibility version is unchanged. |
| `0.2.0-beta.8` | 2026-10-16 | Eighth beta (user request): share Asset Library services and separate responsibilities in Quick Access, Grid Edit, Quick Brush Adjustments, gestures, Rest Note, Lazy Tools and Brush Stroke Layer. Existing behavior/settings retained and user-confirmed. Released as GitHub Release `v0.2.0-beta.8` (prerelease, merge commit `4e92a41ee78c`, PR #3, run 38872500400). Feature branches now merge into main through pull requests. |
| `0.1.0-alpha` | 2026-10-07 | First Solstice version. |
| `0.2.0-alpha` | 2026-10-10 | Milestone (user request): GPU brush by default, masking brush on the GPU, ABR import improvements, Solstice icons and splash, Explorer thumbnails. |
| `0.2.0-beta` | 2026-10-13 | Beta for publication (user request), after the pre-release review; only the label changes. Released 2026-10-14 as GitHub Release `v0.2.0-beta` (prerelease, commit `a442a5ce25`); its CI build lacked the Python plugins. |
| `0.2.0-beta.2` | 2026-10-14 | Second beta (user request): the CI build includes the Python plugins (Python Plugin Manager, pykrita) again; new Puppet Warp and Brush Stroke Layer images. GitHub Release `v0.2.0-beta.2`. |
| `0.2.0-beta.3` | 2026-10-14 | Third beta (user request): first upstream Krita intake (2026-W42: thumbnail cache crash, animation curves scrolling, second-wheel horizontal scrolling, perspective transform conditions, PSD group collapse and layer locks). Released 2026-10-14 as GitHub Release `v0.2.0-beta.3` (prerelease, commit `c89bc02475`, run 38503291774); website updated the same day. |
| `0.2.0-beta.4` | 2026-10-14 | Fourth beta (user request): fixes the crash in `KisImage::enableDirtyRequests()` during repeated Python `Node.save()` (thread-safe projection updates filter stack), and Python `Node.save()`, `Filter.apply()`, `Scratchpad.setScale()` return `bool`. Released 2026-10-15 as GitHub Release `v0.2.0-beta.4` (prerelease, commit `3575bbb59c`, run 38584970834); website updated the same day. |
| `0.2.0-beta.5` | 2026-10-15 | Fifth beta (user request): RGBA16F document conversion choices, document tool-state restoration, Quick Access hold ownership, and the Brush Stroke Layer and Puppet Warp audit fixes. Released 2026-10-15 as GitHub Release `v0.2.0-beta.5` (prerelease, commit `dd9e48c152`, run 38669973004); website updated the same day. |
| `0.2.0-beta.6` | 2026-10-16 | Sixth beta (user request): Effects & Color, Color Reference with image drag and drop, Layer Comps, and the bundled CPU G'MIC runtime. Released as GitHub Release `v0.2.0-beta.6` (prerelease, commit `b7ee343e81fe`, run 38833086246); website updated the same day with Effects & Color and Layer Comps pages. |
| `0.2.0-beta.7` | 2026-10-16 | Seventh beta (user request): complete Simplified Chinese interface (machine-translated `zh_CN` catalog, built-in Chinese texts for the Krita-profile import, translated Quick Access and Vision ML error details) and the translation tooling; the website adds Simplified Chinese pages under `/zh-cn/`. Released 2026-10-16 as GitHub Release `v0.2.0-beta.7` (prerelease, commit `53685b8aae`, run 38851516022); website updated the same day. |

## Checks

- After reconfiguring, check the CMake output for
  `-- Solstice version: <version>`, and check that
  `_build/libs/version/kritaversion.h` defines both versions.
- `(Get-Item <install>\bin\solstice.exe).VersionInfo`: `ProductName` is
  `Solstice`, and the versions are the Solstice version.
- Manual checks: the splash screen and Help > About show the Solstice version;
  Help > Show system information shows both versions; a saved `.kra` still has
  `kritaVersion="6.0.5-prealpha"` in `maindoc.xml`.

## Related build fix

The full build with the version change also compiled the Python bindings,
which had failed since 2026-10-04. `KisPresetChooser::eventFilter()` was
`private`, and sip subclasses the widget. It is now `protected`
(`libs/ui/widgets/kis_preset_chooser.h`).
