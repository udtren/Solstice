# Solstice

**Solstice is an open-source, GPU-focused digital painting application derived
from the Krita 6 codebase.**

It combines Krita's established painting workflow with Vulkan-powered layer
compositing, an experimental GPU pixel-brush path, and productivity tools
integrated natively into the application.

Solstice is independently maintained and desktop-only. It is not an official
Krita edition, and development no longer tracks upstream Krita.

[Highlights](#highlights) | [Development & downloads](#development-and-downloads) | [GPU engine](#gpu-engine) | [Benchmarks](#benchmarks) | [Supported environment](#supported-environment)

## Highlights

- **[Experimental Vulkan engine](docs/gpu-engine.md)**: GPU-resident image
  tiles, accelerated layer compositing and canvas data preparation.
- **GPU brush prototype**: opt-in GPU compositing for RGBA32F pixel brushes,
  including supported selection, alpha-lock and mirror-painting paths.
- **Native productivity tools**: [Quick Access Manager](docs/quick-access.md),
  [Asset Library](docs/asset-library.md), [Rest Note](docs/rest-note.md) and
  [Lazy Tools](docs/lazy-tools.md).
- **[Vision ML](docs/vision-ml.md)**: native tools for AI-assisted selection,
  background removal and Smart Fill.
- **[Puppet Warp](docs/puppet-warp.md)**: pose and reshape artwork with movable
  and rotatable pins in the Transform Tool.


## Development and downloads

> **Experimental software.** The Vulkan engine is optional and still under
> development. It does not yet replace every CPU processing path, and its
> supported hardware and platforms are limited.

There are currently no packaged downloads in [GitHub Releases](https://github.com/udtren/Solstice/releases). 
Solstice is currently available as source code for development builds.

The primary development and default branch is `krita-sol-gpu`. For source
builds, see the [development environment](AGENTS.md#shared-development-environment) and [build workflow](docs/agent/development-workflow.md#build). 
Check the [supported environment](#supported-environment) before building.

## Project page

**A dedicated Solstice website is planned but has not been created yet.**
Its link will be added to this README when the website is available.

For now, this [GitHub repository](https://github.com/udtren/Solstice) is the
project's home. The [highlights](#highlights) link to individual feature guides.

## Differences from Krita

Solstice follows its own development direction, centered on an experimental
Vulkan image engine and a separately maintained native feature set. Selected
Python-based extensions have been reimplemented in C++, with compatibility
for existing settings where practical. The Asset Library is also available
from the welcome page, and Puppet Warp is integrated into the Transform Tool.

Android support and its build and packaging infrastructure have been removed.
Upstream Krita synchronization has been discontinued; Solstice-specific
changes and support are maintained in this repository.

## GPU engine

The experimental engine uses Vulkan compute for supported RGBA floating-point
layer compositing and shares data with the OpenGL canvas for display. It is
disabled by default and falls back to CPU paths for unsupported operations.

An opt-in RGBA32F pixel-brush prototype also composites Normal, Alpha Darken and Erase
dabs on the GPU, including selected and mirrored painting. Normal/Erase
Wash previews and final merges also have GPU paths with selection and
CPU-compatible channel-lock handling. Dab generation,
filters and transforms still run on the CPU. This is an
ongoing rewrite, not a fully GPU-based painting pipeline.

See the [GPU Engine guide](docs/gpu-engine.md) for setup and limitations.
Prototype benchmark results below include development work that may not yet
be included in a published build.

## Benchmarks

**These numbers measure specific engine operations, not overall application
performance.** They are not frame rates or pen-to-screen latency measurements.

Measured on an NVIDIA RTX PRO 6000 Blackwell development system:

| Workload | CPU | GPU |
| --- | ---: | ---: |
| Full layer projection, 4096 x 4096 RGBA32F, 16 layers | 253 ms | 42 ms |
| Canvas data preparation, 4096 x 4096 RGBA32F, 8 layers | 1025 ms | 1.6 ms |
| Four mirror passes, 14 dabs of 73 x 73 pixels | 0.932 ms | 0.459 ms |
| Four mirror passes, 32 dabs of 256 x 256 pixels | 45.545 ms | 1.874 ms |

Projection measurements start with GPU-resident layers and exclude a full CPU
readback. Canvas preparation excludes the final OpenGL texture copies. Brush
measurements average five warmed updates, include uploads and GPU completion
waits, and compare against serial CPU painting; dab generation, job scheduling
and display are excluded. Results vary with the workload and hardware.

Projection and canvas methodology is recorded in the
[GPU development notes](docs/agent/gpu-engine.md#phase-31-measurements).
The mirror-painting results are from a development build measured on
October 3, 2026; they do not establish performance for a packaged release.

## Supported environment

- **Primary tested platform:** Windows 11, 64-bit desktop.
- **GPU engine target:** NVIDIA Blackwell GPUs (GeForce RTX 50 series and
  RTX PRO Blackwell). Validation and benchmarks use an RTX PRO 6000 Blackwell;
  other GPU architectures are unsupported by this experimental engine.
- **Graphics APIs:** Vulkan 1.3 and desktop OpenGL with Vulkan/OpenGL external
  memory and semaphore interoperability for the accelerated canvas path.
  ANGLE/Direct3D does not provide that canvas path.
- **Validation setup:** NVIDIA driver 596.86, Qt 6.8 and desktop OpenGL 4.6.
- **Other platforms:** Linux and macOS are not currently validated for this
  custom build. Android is not supported.

The GPU engine has narrower requirements than Krita itself. 
See the [GPU Engine guide](docs/gpu-engine.md#requirements) for the supported document
formats and fallback behavior.

## Relationship to Krita

**Solstice is not affiliated with, endorsed by, sponsored by, or supported by
the Krita project or KDE.** It is independently maintained and is not an
official Krita edition. Its source-code origins do not imply an organizational
relationship with those projects.

Please report Solstice-specific issues to this repository, not to the Krita
or KDE issue trackers. Credit for the original application belongs to Krita's
contributors. Existing copyright notices and license terms remain applicable;
see [COPYING](COPYING) and the notices in individual files.
