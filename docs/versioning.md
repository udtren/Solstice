# Versions

Solstice has its own version number, starting with **0.1.0-alpha**.

- **0.x** means Solstice is still in testing: features and behavior can
  change between versions.
- The second number increases for a set of new features, the third for
  fixes only.
- The label shows the stage: `alpha` now, later `beta` and release
  candidates. Version 1.0.0 will be the first release without a label.
- Development builds add the source revision, for example
  `0.1.0-alpha (git 2d48c0b)`.

The version appears on the splash screen, in **Help > About Solstice**, in
**Help > Show system information for bug reports** and in the file
properties of `krita.exe` on Windows.

## The Krita version

Solstice is based on Krita 6.0. System information shows that base as
"Based on Krita: 6.0.5-prealpha". Solstice keeps using that number where
compatibility depends on it:

- `.kra` files record it, so Krita and Solstice open the files as usual;
- the resource folder and Python scripts see it, so bundled resources and
  existing plugins keep working.

## Updates

Solstice does not check for new versions yet. Krita's update check is
turned off, because it would announce Krita releases. The welcome page news
option shows upstream Krita news only.
