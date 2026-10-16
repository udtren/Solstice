# Solstice interface: maintenance reference

This document owns the current interface theme, window decoration, widget
style, scale and font contracts. User controls and limitations are in
[Solstice interface](../ui-modernization.md). The complete
[development history](wiki/history/ui-modernization-development.md) preserves
the original proposal, screenshots inventory and dated implementation results.

The theme, interface option, Fusion-based style, scale and bundled font are
implemented. Early statements that the interface defaults off or that Fusion
is only a candidate are historical. Icon size/stroke unification remains
unimplemented; the old plan does not authorize additional UI work.

## Settings and defaults

These choices are independent. Changing the color theme must not silently
change the widget style, interface option, scale or custom font.

| Choice | Stored setting | Current default / application |
| --- | --- | --- |
| Theme | `kritarc`, `[theme] Theme` | Embedded defaults select `Solstice Dark`; changing the theme updates the palette |
| Widget style | `kritarc`, top-level `widgetStyle` | Embedded defaults select `Solstice`; application startup and the Styles menu instantiate it |
| Window decoration and preferences sidebar | `kritarc`, `Solstice/ModernInterface` | Embedded default and read fallbacks are `true`; Restore Defaults also enables it |
| Interface scale | `kritadisplayrc`, `SolsticeInterfaceScale` | 100%; UI range 75–200% in steps of 5; applied at next startup |
| UI font | `kritarc`, `use_custom_system_font`, `custom_system_font`, `custom_font_size` | Use the configured custom font when valid; otherwise Cantarell if available, at the system UI font size |

The defaults are in [krita/data/kritarc](../../krita/data/kritarc). Configuration
files are resolved through `KisSolsticePaths`; on Windows they normally live
under `%APPDATA%\Solstice\config`. The
[settings/profile reference](settings-location.md) owns path fallbacks,
embedded-default registration, importing Krita profiles and reset behavior.
Do not duplicate that migration procedure here.

Existing explicit values take precedence over defaults, including an imported
profile's custom font, theme, style and disabled interface option. An absent
`Solstice/ModernInterface` key now resolves to true; do not interpret an
imported profile lacking that key as a request to keep it off.
`KisConfig::widgetStyle()` itself still has an empty-string fallback: programs
that do not mount application defaults can behave differently from a normal
fresh profile. `KRITA_NO_STYLE_OVERRIDE` bypasses the startup style override.

## Source map

| Responsibility | Source |
| --- | --- |
| Palette values and theme installation | [SolsticeDark.colors](../../krita/data/themes/SolsticeDark.colors), [theme CMake list](../../krita/data/themes/CMakeLists.txt) |
| Palette/theme notifications and icon cache | [thememanager.cpp](../../libs/ui/thememanager.cpp) |
| Main-window style menu, document tabs and toolbars | [KisMainWindow.cpp](../../libs/ui/KisMainWindow.cpp) |
| Startup style selection | [KisApplication.cpp](../../libs/ui/KisApplication.cpp) |
| Docker header painting and margins | [KoDockWidgetTitleBar.cpp](../../libs/widgets/KoDockWidgetTitleBar.cpp) |
| Common control style and clone factory | [KisSolsticeStyle.h](../../libs/widgetutils/KisSolsticeStyle.h), [implementation](../../libs/widgetutils/KisSolsticeStyle.cpp) |
| Interface checkbox, scale controls and compact sidebar | [kis_dlg_preferences.cc](../../libs/ui/dialogs/kis_dlg_preferences.cc) |
| Early display settings and font registration | [main.cc](../../krita/main.cc), [KisUiFont.cpp](../../libs/ui/KisUiFont.cpp) |
| Bundled font resources and notices | [font directory](../../krita/data/fonts/), [OFL license](../../LICENSES/OFL-1.1.txt) |
| QML theme integration | [KisQQuickWidget.cpp](../../qmlmodules/widgets/KisQQuickWidget.cpp) |
| Style regression coverage | [KisSolsticeStyleTest.cpp](../../libs/widgetutils/tests/KisSolsticeStyleTest.cpp) |

## Palette and window decoration

`SolsticeDark.colors` uses the existing color-scheme loader and is installed
under `share/color-schemes`. Preserve existing theme names and files.
The scheme owns the actual colors; the original plan's comparative palette
table is historical, not a second editable palette definition.

For ordinary color schemes, `ThemeManager::slotChangePalette()` clears the
icon cache **before** changing the application palette, then notifies theme
consumers. Preserve this ordering so widgets receiving PaletteChange use
icons for the new background. The System-theme path resets the palette and
emits the theme-change signal separately.

`KisMainWindow` supplies the interface flag to `KoDockWidgetTitleBar` before
creating dockers. `customizeTabBar()`, reached through existing configuration
and theme-change paths, updates existing headers, the document tab bar and
each toolbar. The low-level header class holds a static flag and does not
read `KisConfig` or depend on `kritaui`.

| Area | Implemented behavior |
| --- | --- |
| Docker header | Palette-derived band and 1 px bottom separator; margins left/top/right/bottom 6/3/4/3 when enabled, 2/0/2/0 when disabled; custom painting replaces `CE_DockWidgetTitle` |
| Document tabs | Existing close-button QSS retained; non-expanding title-width tabs, selected highlight underline, palette-derived inactive/hover/selected surfaces |
| Toolbars | Local QSS adds border, padding and separators; disabling clears that toolbar QSS |
| Preferences sidebar | `KPageDialog::Tree` when enabled, `List` otherwise; Tree has no root decorations/indentation, 20 px icons, 5/6 px item padding and fixed width 220 px |

The preferences dialog chooses its sidebar at construction; reopen it after
changing the setting. Other window decorations update when settings are
accepted. Keep docker IDs, lock/float/close behavior, tab reordering, unsaved
markers, workspace restoration and existing actions unchanged.

Document-tab QSS is deliberately local, not a rule for every QTabBar.
The interface option does not style the main window's docker-group tabs;
the Solstice widget style handles their common tab shape. Toolbar QSS also
affects child widgets, so test slider/combo/custom controls inside toolbars.

## Common widget style and ownership

`KisSolsticeStyle` is a `QProxyStyle` backed by a newly created Fusion style.
It changes painting, leaving Fusion's metrics, content sizes and subcontrol
rectangles intact. This invariant applies to the widget style; the independent
interface option intentionally changes header margins, tab sizing and sidebar
layout.

| Paint entry point | Customized controls |
| --- | --- |
| `drawPrimitive()` | Push/tool buttons, framed line edits, checkboxes and radio indicators |
| `drawControl()` | Rounded menu selection highlight and flat tab shapes with an accent on the content edge |
| `drawComplexControl()` | Spin-box frames/buttons and scrollbar tracks/handles; Fusion supplies arrow/sign primitives |

Unchanged painting delegates to the base style. Spin-box inner editors retain
the appropriate base behavior; slider spin boxes still paint their own slider
fill. In menus, checked items show a plain text-colored check/dot rather than
a colored checkbox panel; unchecked items have no mark. Disabled, focused,
hovered, pressed and toggled states must remain distinguishable.

The saved/menu key is `Solstice` (case-insensitive lookup), but
`objectName()` and `currentUnderlyingStyleName` report `fusion`. Spin boxes
use that identity for geometry adjustments. Do not change the reported name
merely to match the visible Styles menu label.

Startup and menu switching hand a new style to `QApplication`. Local proxy
styles must wrap their own instance, never share the application's owned
instance. `KisSolsticeStyle::createStyle(name)` returns a new Solstice style
only when the application currently uses it and the requested name is
`fusion`; otherwise it delegates to `QStyleFactory`. The caller owns the
result. Its current consumers are:

- `libs/widgetutils/KisMenuStyleDontCloseOnAlt.cpp`
- `libs/widgetutils/config/krecentfilesaction.cpp`
- `libs/ui/widgets/kis_color_filter_combo.cpp`
- `plugins/dockers/layerdocker/LayerBox.cpp`
- `plugins/dockers/storyboarddocker/StoryboardView.cpp`

These local styles are created with their widgets. Some existing lists/menus
retain the previous style until restart; changing the application style does
not recreate every local proxy. Preserve ownership and destruction order, and
avoid stacking wrappers on repeated switches.

## Scale, fonts and QML

`main.cc` reads `kritadisplayrc` before constructing the application, clamps
the saved scale to 75–200 and sets `QT_SCALE_FACTOR` to scale/100 only when the
scale is not 100 and that environment variable is unset. Preferences round
saved values to steps of 5. A user-supplied environment variable takes
precedence, including when the stored scale is 100. Scale multiplies OS
display scaling and affects the canvas too; restart is required.

Windows display-scale rounding and the existing fractional-scaling preference
remain in the startup path. Do not infer that a fractional interface factor
has passed Vulkan-to-OpenGL display verification merely because ordinary
Widgets render correctly.

Cantarell Regular/Bold/Italic/BoldItalic are embedded by
`krita/data/fonts/fonts.qrc` under `/solstice/fonts` (in `krita.dll` on
Windows). `registerBundledFonts()` runs before setting the application's font.
`normalFont()` preserves a valid user override; otherwise it starts from the
system UI font and substitutes Cantarell if the family is available. Missing
glyphs use font fallback; this does not replace the text-tool font selection.
Keep the font assets and their OFL notices together, including installation of
`share/krita/fonts/Cantarell-OFL.txt`.

QML panels use their own Quick Controls shapes. `KisQQuickWidget` defaults
Quick Controls to Fusion unless `QT_QUICK_CONTROLS_STYLE` is supplied, and
updates its clear color on theme notification. Theme-aware QML controls use
palette roles. Neither Widgets QSS nor `KisSolsticeStyle` implements a
Solstice Quick Controls style.

## Verification and build scope

Follow [Development workflow](development-workflow.md) and the feature-branch
policy in [AGENTS](../../AGENTS.md#branch-policy). The archived
`krita-sol-gpu` work instruction is no longer current.

For implementation changes:

- Build/install affected `kritawidgetutils`, `kritawidgets` and `kritaui`
  libraries. Rebuild the application/QRC owner when embedded fonts or defaults
  change; reconfigure/install theme or font-notice changes through their CMake
  rules. Restart fully after installing DLLs; never terminate the user's
  running application without approval.
- Run `KisSolsticeStyleTest` for style changes. It checks key/identity, local
  factory behavior, widget/menu size parity with Fusion and rendering paths.
  Render checks are smoke tests, not pixel-perfect reference comparisons.
- `SOLSTICE_STYLE_DUMP=<existing directory>` produces `fusion.png`,
  `solstice.png`, `menu-fusion.png` and `menu-solstice.png` for visual review.
  `QT_QPA_PLATFORM=offscreen` is supported; the original environment did not
  show fonts in that mode, so inspect text on the real platform too.
- For defaults/path/import changes, use the profile reference's tests,
  including `KisSolsticePathsTest` and `KisSolsticeProfileTest`.
  Test with scratch profiles, preserving the user's configuration.

From the configured build environment, for a style change:

```bat
cmake --build <krita-dev-root>\_build --target KisSolsticeStyleTest
ctest --test-dir <krita-dev-root>\_build -R "KisSolsticeStyleTest$" --output-on-failure
```

Manual regression checks:

1. Toggle the interface option, dark/light themes and Solstice/Fusion styles;
   reopen preferences and restart. Check multiple existing and new windows.
2. Exercise narrow/floating/tabbed/locked dockers, restoring a workspace,
   tab switch/reorder/close and unsaved prompts with long Japanese filenames.
3. Inspect normal, hover, pressed, selected, focused and disabled controls,
   checked/unchecked menus, toolbox selection, toolbar children, numeric
   typing/dragging/wheel and Tab navigation. Check press/release and focus.
4. Check Quick Access, Rest Note, Asset Library, Brush Editor and Puppet Warp
   for preserved interactions; their feature documents own lifecycle rules.
5. Compare font override on/off, missing-glyph text, scale changes after restart
   and environment-variable precedence. Compare 100/150/200% as well as the
   original 125% OS scale; include canvas rendering at fractional factors.
6. Compare startup, resizing, theme switching and painting with the same
   document/window/workspace. Investigate reproducible stalls or flicker;
   compilation alone does not establish interaction or visual correctness.

### Evidence and remaining limits

The original record reports user acceptance on 2026-10-07 of the theme,
interface option, common style and first individual refinements (spin boxes
and local proxies). It records implementation of the compact sidebar, scale
and font without a separate explicit manual acceptance for each. The
[profile reference](settings-location.md) records fresh-profile/defaults
verification on the same date.

The recorded visual environment was Windows, Qt 6.8.0, OS scale 125%, with
3840×2160 monitors. This is dated evidence, not a claim about the current
machine. Screenshots listed in history were outside the repository; this
extraction does not turn them into shipped screenshots.

Unverified scope includes other display scales, fractional GPU presentation
and Linux/macOS visual behavior. QML control shapes and icon stroke/size
unification are not implemented by this feature. Canvas-border color remains
a Display setting, independent of the theme. Logo/splash/file-icon work belongs
to [visual branding](solstice-visual-branding-todo.md).

This reference was extracted on 2026-10-16 against
`c1cdf8ef5e9827bb4871c51a372d12edab74e6d2`. Documentation validation and source
inspection are not a new native build or interactive acceptance. Future
maintenance updates belong here and in the user guide; dated results and
superseded proposals belong in history.
