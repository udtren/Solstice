# Versions

Solstice has its own version number, starting at 0.1.0-alpha. See the
[project overview](../README.md) for the latest-release summary and
[GitHub Releases](https://github.com/udtren/Solstice/releases) for published builds.

- **0.x** is pre-1.0 development: features and behavior can change between
  versions. It does not by itself mark a GitHub release as a prerelease.
- The second number increases for a set of new features, the third for
  fixes only.
- A label such as `alpha`, `beta.8` or `rc.1` marks a prerelease.
  The user selected **0.3.0** as the first release without a label; publication
  status and available downloads are listed in GitHub Releases.
- Development builds add the source revision, for example
  `0.3.0 (git abc1234)`.

The version appears on the splash screen, in **Help > About Solstice**, in
**Help > Show system information for bug reports** and in the file
properties of `solstice.exe` on Windows.

## The Krita version

Solstice is based on Krita 6.0. System information shows that base as
"Based on Krita: 6.0.5-prealpha". Solstice keeps using that number where
compatibility depends on it:

- `.kra` files record it, so Krita and Solstice open the files as usual;
- the resource folder and Python scripts see it, so bundled resources and
  existing plugins keep working.

## Updates

Solstice does not check for new versions yet. Krita's update check is
turned off, because it would announce Krita releases.

The welcome page's News area has two parts: upstream Krita news at the top
and Solstice releases (from the Solstice GitHub repository) below. Turn them
on with **Enable Krita news and Solstice releases** in the News menu; until
then Solstice does not connect to krita.org or GitHub for them.
