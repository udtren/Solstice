# Solstice visual branding TODO

This checklist covers visual assets and visual-facing integration only. Internal
Krita compatibility identifiers, plugin APIs, MIME types, resource paths, and
configuration filenames are intentionally outside this checklist.

## Temporary gray placeholders (2026-10-03)

The user requested solid gray replacements while the final brand is undecided.
169 nonempty image assets now use opaque `#808080`, with their original pixel
dimensions or SVG width/height/viewBox preserved:

- All images in `krita/pics/branding/`, including default, Beta, Next, Plus,
  the legacy root ICO, and every Apple `.icon/Assets` source.
- All 30 images in `krita/pics/mimetypes/`, including the `krz` subdirectory.
- The 3840 x 1920 PNG splash, 1920 x 960 JPEG source, and 131 x 80 SVG banner.
- The 24 x 24 `krita/pics/svg/support-krita.svg` resource. Its upstream link
  and text remain unchanged.
- All MSIX images in the actual source directory
  `packaging/windows/msix/pkg/Assets/`.
- `packaging/macos/KritaIcon.icns` and `packaging/macos/krita_dmgBG.png`.

ICO files preserve all seven sizes (16, 32, 48, 64, 96, 128, 256). ICNS images
preserve the individual pixel sizes and Retina representations; the legacy
16/32-pixel ARGB entries use equivalent PNG entries (`icp4`/`icp5`). The Apple
layer composition configuration is unchanged, so macOS effects and final
rendering still need platform verification. No packaging identity was renamed.

`libs/ui/kis_splash_screen.cpp` leaves the splash artwork credit empty while
the solid fill is used. Restore the correct artist credit with final artwork.
Original artwork remains available in Git history. The empty seasonal splash
file has no dimensions and is unchanged; its code path remains disabled.
Third-party sponsor artwork, upstream links, and contributor attributions are
unchanged. Template/store-screenshot audits remain future work, not completed
by this mechanical image replacement.

The Windows build embeds Next's SVGZ and splash QRC in `krita.dll` and
generates application/file ICO files from the PNG sources. Rebuild `krita`,
`krita_windows_stub_exe`, `krita_windows_stub_com`, and `krafile_dummy_obj`;
install `libs/ui`, `krita`, and `krita/pics/branding/Next` locally with the
application closed. Generated `krita.ico`/`kritafile.ico` are installed under
`installer/` for packaging. NSIS uses those generated icons for shortcuts;
there is no repository-specific NSIS header bitmap to replace.

The Windows source lists in `krita/CMakeLists.txt` exclude 512/1024-pixel PNGs
from ICO generation only. Otherwise ECM's icon conversion embedded a 1024-pixel
PNG in a directory entry declaring 256 pixels. The high-resolution source and
installed PNGs are retained; generated Windows ICO frames must match their
declared dimensions as well as being gray.

Verification: compare source dimensions with the pre-change Git assets, decode
every ICO/ICNS representation, and check all raster pixels and SVG rectangles
are opaque gray. Also inspect generated installer icons after rebuilding.
The 169 source assets passed comparison with pre-change `HEAD`, including every
ICO/ICNS representation. The application and Windows stubs compiled successfully.
Both generated Windows ICOs passed pixel and declared-dimension checks at all
seven sizes (16, 24, 32, 48, 64, 128, 256). Installation completed after the user
closed `krita.com`; `krita.dll`, `libkritaui.dll`, both Windows stubs and both
installer ICOs match their build SHA-256 hashes. When checking for a running
application on Windows, check both process names `krita` and `krita.com`.
Interactive splash/About/taskbar appearance needs a restarted application;
macOS, Linux, MSIX packaging, icon-cache and scaling checks remain open.

User-facing status: [Temporary visual branding](../visual-branding.md).
The unchecked items below refer to final artwork and release acceptance;
gray placeholders do not complete them.

## Brand source package

- [ ] Create the canonical Solstice logo as an editable vector source.
- [ ] Create light, dark, monochrome, and high-contrast logo variants.
- [ ] Define minimum clear space, approved background colors, and small-size behavior.
- [ ] Record the logo font, colors, and asset license in this document or a neighboring
      brand guide.
- [ ] Keep source artwork separate from generated PNG, ICO, ICNS, and Apple icon assets.

## Application icon

- [ ] Replace the icon sources under `krita/pics/branding/default/`.
- [ ] Replace or retire the `Beta`, `Next`, and `Plus` branding variants; the current
      development build is configured with the `Next` variant, so changing only
      `default` will not change its icon.
- [ ] Generate and visually inspect the installed icon at 16, 22, 32, 48, 64, 128,
      256, 512, and 1024 pixels where the platform supports those sizes.
- [ ] Replace `krita/pics/branding/default/krita.ico` while retaining the filename until
      the executable and packaging rename is completed.
- [ ] Replace the Apple `krita.icon` asset package and verify its generated ICNS output.
- [ ] Update `branding.qrc` and the `krita-branding` resource alias only if a coordinated
      code migration is made; the alias may safely remain internal.
- [ ] Check the icon in the Windows taskbar, Alt+Tab, Task Manager, Start menu, desktop
      shortcut, file picker, and executable properties.
- [ ] Check the icon on light and dark Windows themes at 100%, 150%, and 200% scaling.
- [ ] Check Linux launcher, task switcher, and desktop-file icon rendering.
- [ ] Check macOS Dock, Finder, About panel, and application bundle rendering.

## Splash and About artwork

- [ ] Create new Solstice splash artwork with an explicit redistribution license.
- [ ] Replace `krita/data/splash/electrichearts_20250824A_kiki_4K.png`.
- [ ] Provide a correctly sized lower-resolution source if the desktop build later needs
      one; Android-specific splash work is not in scope for this distribution.
- [ ] Replace `krita/data/splash/banner.svg` with a Solstice wordmark.
- [ ] Verify that the round branding icon and wordmark align correctly in
      `KisSplashScreen` at 100%, 150%, 200%, and 300% display scaling.
- [ ] Update the splash artist credit in `libs/ui/kis_splash_screen.cpp`.
- [ ] Confirm loading text remains readable against the brightest and darkest parts of
      the new artwork.
- [ ] Verify the compact About-dialog form of the splash does not crop the logo,
      wordmark, links, or artwork credit.
- [ ] Decide whether seasonal splash support will use Solstice artwork, then replace or
      remove the empty `splash_holidays_dummy.png` placeholder.

## Document and file-type artwork

- [ ] Inspect every image under `krita/pics/mimetypes/` for the Krita logo or other
      upstream brand marks.
- [ ] Preserve `.kra`, `.krz`, `.kpp`, and other compatible format identities even when
      replacing branded artwork.
- [ ] If document icons are changed, generate the Windows `kritafile.ico` and verify
      Explorer thumbnails and fallback icons separately.
- [ ] Verify that a Solstice application icon is not confused with the `.kra` document
      icon at small sizes.
- [ ] Review brush preset, resource bundle, workspace, session, and shortcut-scheme
      icons shown in native file dialogs.

## Installer and package visuals

- [ ] Create a Solstice installer header/banner image if the Windows NSIS theme uses one.
- [ ] Replace installer, uninstaller, Start menu, and desktop-shortcut icons.
- [ ] Replace MSIX tile assets under `packaging/windows/msix/pkg/Assets` if MSIX packaging is
      retained.
- [ ] Test Windows pinned shortcuts before and after an upgrade; Windows may cache the
      old Krita icon.
- [ ] Replace macOS DMG background and volume icon if macOS packaging is retained.
- [ ] Replace AppImage, Flatpak, and Snap store imagery if those packages are retained.
- [ ] Capture new store screenshots only after the full visible-name pass is complete.

## In-application branded visuals

- [ ] Audit `support-krita` and other donation/community icons on the Welcome page.
- [ ] Keep upstream Krita links visually identified as upstream rather than presenting
      them as Solstice services.
- [ ] Review the About dialog sponsor artwork and decide whether to retain it as an
      explicitly labelled upstream Krita section.
- [ ] Search all embedded QRC resources for Krita wordmarks, logos, mascot artwork, and
      screenshots containing the old interface name.
- [ ] Review welcome-page banners under `share/krita/donation/`; the internal path may
      remain `krita`, but displayed artwork must be labelled accurately.
- [ ] Review example templates and bundled resources for preview images containing a
      visible Krita logo.

## Visual QA and release acceptance

- [ ] Test a clean install with no icon cache from an earlier Krita or Solstice build.
- [ ] Test an upgrade from the last custom Krita-branded build.
- [ ] Test side-by-side installation with official Krita.
- [ ] Confirm no Solstice shortcut, installer page, splash, About page, or OS application
      surface displays the Krita application logo as the current product logo.
- [ ] Confirm every retained occurrence of the Krita name is clearly a format,
      compatibility, historical, documentation, upstream-project, or attribution
      reference.
- [ ] Capture final reference screenshots of splash, About, Welcome, taskbar, Start menu,
      installer, and `.kra` file association states.
