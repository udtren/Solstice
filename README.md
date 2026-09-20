## Solstice

Solstice is an unofficial, desktop-focused custom build based on the Krita 6 development branch.
It preserves Krita's painting workflow while integrating project-specific interface and productivity changes.

## Table of Contents

- [Main differences from upstream Krita](#main-differences-from-upstream-krita)
- [Ported Plugin](#ported-plugin)
  - [Quick Access Manager](#quick-access-manager)
  - [Rest Note](#rest-note)
  - [Asset Library](#asset-library)
  - [Vision ML](#vision-ml)
  - [Lazy Tools](#lazy-tools)
- [New Features](#new-features)
  - [Puppet Warp](#puppet-warp)
- [Building](#building)

## Main differences from upstream Krita

- Solstice is desktop-only. Krita's Android application, Android build and packaging rules, donation integration, and related platform-specific code have been removed.
- Selected third-party Python plugins have been migrated into native C++ Krita plugins. They participate in Krita's normal plugin lifecycle and no longer depend on Python, PyQt, or private Python-side tool injection.
- The welcome page includes an **Asset Library** tab alongside **Recent Images**. It shares the native Asset Library configuration and features without embedding the docker itself.
- The Transform Tool includes a native **Puppet Warp** mode with movable and rotatable pins, artwork-aware mesh visualization, configurable expansion, and serialized transform state.
- Native AI-assisted selection, background removal, and smart fill are available through Vision ML, with CPU fallback and optional Vulkan GPU acceleration.
- Quick-access, color-selection, brush-adjustment, timer, and asset-management workflows are integrated as maintained native features of this custom build.

## Ported Plugin

The following external Python plugins have been reimplemented as native Krita features. The native versions preserve compatibility with their existing user configuration where practical.

### Quick Access Manager

[`Quick Access Manager documentation`](docs/quick-access.md)

### Rest Note

[`Rest Note documentation`](docs/rest-note.md)

### Asset Library

[`Asset Library documentation`](docs/asset-library.md)

### Vision ML

[`Vision ML documentation`](docs/vision-ml.md)

### Lazy Tools

[`Lazy Tools documentation`](docs/lazy-tools.md)

## New Features

### Puppet Warp

<img src="/images/puppet_warp_1.png" alt="Puppet Warp mesh" width="50%">

[`Puppet Warp documentation`](docs/puppet-warp.md)

## Building

Solstice uses Krita's standard desktop build system. Refer to the official
[Building Krita documentation](https://docs.krita.org/en/untranslatable_pages/building_krita.html)
for prerequisites and platform-specific instructions.
