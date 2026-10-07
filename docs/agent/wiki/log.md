---
type: log
---

# Wiki log

Append-only. One entry per ingest, lint pass or restructuring, newest last.
Format: `## YYYY-MM-DD <kind>: <subject>`, then what changed and the sources.

## 2026-10-07 setup: wiki created

- Created the index with conventions and workflows; `AGENTS.md` points to it.
- Source: the user's request after comparing Karpathy's LLM wiki pattern
  (gist 442a6bf555914893e9891c11519de94f) and nashsu/llm_wiki.

## 2026-10-07 restructure: GPU engine phase records

- Moved `docs/agent/gpu-engine.md` "Review status" and every phase record
  (phases 4.1-4.97, about 4,750 non-empty lines) verbatim into six
  `history/` pages. Headings were promoted one level, and relative links were
  adjusted. A line-by-line comparison with the previous version matched.
- `gpu-engine.md` keeps status, decisions, architecture, plan, build/test,
  source maps, configuration, invariants, checklist and risks. Its new
  "Phase history" section lists every moved section.
- Rewrote the anchor links in `README.md`, `docs/gpu-engine.md` and
  `paint-trace-baseline-runs.md`, and the section references in
  `feature-inventory.md`, `gpu-engine-handoff.md` and
  `gpu-work-priorities.md`.

## 2026-10-07 ingest: GPU affine and Liquify work, session pitfalls

- New pages:
  - `concepts/cpu-gpu-bit-parity.md`;
  - `concepts/krita-copy-semantics.md`;
  - `concepts/qt-numeric-and-geometry.md`;
  - `decisions/gpu-phase-numbering.md`;
  - `decisions/xmlgui-unchanged.md`;
  - `pitfalls/build-format-test.md`;
  - `benchmarks/transform-and-filter-costs.md`.
- Sources:
  - commits `29912c4daf` (GPU Liquify, phase 4.97) and `3828911e98` (GPU affine,
    phase 4.94);
  - `history/gpu-phases-4.93-.md`;
  - qtbase v6.8.0 `src/gui/painting/qpolygon.cpp`;
  - the installed Qt 6.8 `qnumeric.h`;
  - this session's build and test experience.
