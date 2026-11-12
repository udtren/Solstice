---
type: decision
updated: 2026-11-12
sources:
  - libs/gpu/KisGpuContext.{h,cpp} (isVerified, acceptsDevice, create, probeDevice)
  - libs/image/gpu/KisGpuTileBackend.cpp (instance, unverifiedDeviceName)
  - libs/image/gpu/KisGpuEngineSettings.{h,cpp} (unverifiedDeviceAllowed)
  - libs/ui/KisGpuEngineUi.cpp (KisGpuEngineSettingsWidget)
  - docs/agent/gpu-engine.md ("Decisions approved by the user", "Feature gate and user interface")
  - docs/gpu-engine.md ("Requirements", "Turning it on or off")
related:
  - gpu-phase-numbering.md
  - ../pitfalls/gpu-readers-need-a-tile-data-reference.md
---

# The GPU engine starts by itself only on verified GPUs

**Decision** (user, 2026-11-12). The GPU engine is on by default only on
the hardware it was verified with: NVIDIA Blackwell (GeForce RTX 50 series,
RTX PRO Blackwell). On any other GPU that meets the Vulkan requirements it
stays off until the user checks "Also use it with graphics cards it was not
verified with" in the settings. GPUs that cannot run it (no Vulkan 1.3
features, software Vulkan) are handled as before.

**Why.**

- The user develops and tests on one Blackwell card and does not plan to
  get another GPU. Every result in the documents (parity tests, manual
  checks, benchmarks) comes from that card and one driver.
- The engine's parity with the CPU depends on details that drivers decide:
  how a shader compiler fuses multiply-add, how division rounds
  ([bit parity](../concepts/cpu-gpu-bit-parity.md)). They were measured on
  Blackwell only. On another vendor's GPU the engine may run and paint
  slightly different pixels with nobody having looked.
- An engine fault loses image data in the worst case ("If the GPU engine
  stops" in the user document). Default-on on untested hardware exposes
  users to that without their choice.
- Refusing other GPUs outright would take the engine away from users for
  whom it works. An option keeps it available and makes the risk a choice.

**What it replaces.** On 2026-10-14 the user decided that the documents say
"untested" for other GPUs and never that they fall back to the CPU, because
the engine did start on them. The engine no longer starts on them by
itself, so the documents now say that other GPUs use the CPU until the
option is checked.

**Consequences.**

- A profile that ran the engine on another GPU (up to Solstice 0.9.1) gets
  the CPU path after the update until the option is checked. The user
  document says so under "Requirements".
- `isVerified()` is `isBlackwell()` (NVIDIA device id 0x2B00 or above),
  which also matches NVIDIA generations after Blackwell. Untested too;
  narrow the rule when such a card exists and misbehaves.
- Verifying another GPU means running the GPU test suites and the manual
  checklist on it. Nobody is set up to do that; no report channel was
  asked for.

**How to apply.** Adding hardware to the verified set is a user decision
backed by test results from that hardware: change
`KisGpuDeviceInfo::isVerified()`, the tooltip and the two user documents
together. Do not widen it because a GPU "should work".
