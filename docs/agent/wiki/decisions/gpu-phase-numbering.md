---
type: decision
updated: 2026-10-07
sources:
  - docs/agent/gpu-engine-handoff.md (phase 4.95 entry: "次のフェーズ4.96（メッシュ変形のGPU描画）")
  - docs/agent/gpu-work-priorities.md (phase 4.95 and 4.97 entries)
  - docs/agent/puppet-warp.md ("Mesh (phase 4.95)")
related:
  - ../history/gpu-phases-4.93-.md
---

# GPU engine phase numbering

**Decision.** Work on the Vulkan GPU engine is numbered as phases of the
overall plan (`docs/agent/gpu-engine.md`, "Phase plan"). Increments of phase
4 use `4.N` in order of start. A number mentioned as the planned next step
stays reserved for that step, even when other work happens first.

**State on 2026-10-07:**

| Phase | Work | State |
| --- | --- | --- |
| 4.94 | GPU affine transform passes | done, manual check OK |
| 4.95 | Puppet Warp mesh ARAP model (CPU) | committed (`298e67c6e4`); folded-joint issue open (`puppet-warp.md`) |
| 4.96 | **Reserved:** Puppet Warp mesh rendering on the GPU | not started |
| 4.97 | GPU Liquify grid warp | done, manual check OK |
| 4.98 | GPU Gaussian blur family | done, manual check OK |

The next new phase is **4.99**, unless the work is the reserved 4.96.

**Why.** The handoff and the priority documents already referred to 4.96
as Puppet Warp's GPU rendering. Reusing the number for Liquify would have
made those references point at the wrong work.

**How to apply.** Before naming a phase, search the agent documents for the
next number (`grep -rn "4\.9[0-9]" docs/agent`). Record the phase in the
newest `history/gpu-phases-*.md` page and in `gpu-work-priorities.md`.
