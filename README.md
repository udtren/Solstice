Solstice is an unofficial, desktop-focused custom build based on the Krita 6 development branch.
It preserves Krita's painting workflow while integrating project-specific interface and productivity
changes.

## Main differences from upstream Krita

- Android application, packaging, donation, and platform-integration code has been removed.
- [Quick Access Manager](https://github.com/udtren/krita-quick-access-manager) has been migrated from
  a Python plugin to a native Krita docker.
- Additional interface and workflow refinements are maintained for this custom build.

The native Quick Access implementation is located in
[`plugins/dockers/quickaccess`](plugins/dockers/quickaccess/).

## Branches

| Branch | Purpose |
| --- | --- |
| `krita-sol` | Custom Krita Sol development and builds |
| `krita/6.0` | Clean tracking branch for synchronizing with upstream Krita 6 |

Custom changes should be made on `krita-sol`. Keep `krita/6.0` aligned with upstream so future
updates can be integrated cleanly.

## Building

Solstice uses Krita's standard desktop build system. Refer to the official
[Building Krita documentation](https://docs.krita.org/en/untranslatable_pages/building_krita.html)
for prerequisites and platform-specific instructions.