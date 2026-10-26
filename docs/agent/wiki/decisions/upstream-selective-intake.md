---
type: decision
updated: 2026-10-26
sources:
  - AGENTS.md ("Branch policy")
  - docs/agent/upstream-intake.md
  - AGENTS.md ("Commit authorship and Contributors")
  - \\192.168.1.37\share\upstream-krita-check (README.md, hermes/SKILL.md, reports/2026-W42.md)
related:
  - versioning.md
---

# Upstream Krita: weekly review, selective cherry-picks

**Decision (user, 2026-10-14).** Upstream merges stay discontinued, but
upstream Krita is reviewed every week. Hermes Agent on the DGX Spark writes a
report with a recommendation per item; the user decides; Claude cherry-picks
the approved items and records each decision in the ledger of
`docs/agent/upstream-intake.md`.

**Why.** `main` has an independent history (no common commit with
upstream) and diverges heavily (GPU engine, brush option model, Android and
Popup Palette removal, branding), so merges are no longer practical. Upstream
still fixes crashes and file-format issues that Solstice shares.

**How to apply.**
- Never add a persistent `upstream` remote. Do not fetch from the share
  mirror either: it is shallow, and a fetch from a shallow repository makes
  the receiver shallow. Apply patches with the share's `scripts/intake.sh`
  (`git am -3 --keep-non-patch` after importing the preimage blobs; plain
  `git am` drops subject prefixes such as `[psd]`). First used for
  2026-W42.
- Dry runs must test a commit both alone and after the earlier candidates of
  its branch: in the first run (2026-W42) ten commits conflicted alone but
  applied cleanly in series.
- A dry-run 3-way apply writes merged blobs into the object store even with
  `--cached`; `collect.py` redirects writes with `GIT_OBJECT_DIRECTORY` and
  reads both repositories through alternates.
- The share is owned by uid 1000, so Git on Windows needs
  `safe.directory`; pass it per command, not in the global configuration.
- Adopted commits are authored by the person running the intake, with the
  upstream author in an `Upstream-Author:` trailer (user decision,
  2026-10-19). `git am` keeps the upstream author, and GitHub lists every
  author of a commit on the default branch as a repository contributor: the
  nine W42 commits added four Krita developers to Solstice's Contributors.
  The general rule (only `udtren` as a human contributor; LLM agents
  allowed) is in `AGENTS.md`, "Commit authorship and Contributors".
  The helper on the share does not do this step; the amend command is in
  [upstream-intake.md](../../upstream-intake.md), step 4.
- Solstice was created from `krita/6.0`, but most candidates come from
  `master`. A conflict in a file Solstice never changed means a
  master-only prerequisite, and it can be a commit the user already
  rejected: in 2026-W44, `630b6a20d4` (KisClipboard rewrite) conflicted in
  `kis_custom_brush_widget.cpp` because it builds on U-2026W42-10
  `6d451f2e9d`. Compare the file at the Solstice snapshot, at the upstream
  parent and in Solstice before calling a conflict mechanical
  (`raw.githubusercontent.com/KDE/krita/<sha>/<path>` serves any version,
  `<sha>~1` the parent).
- A clean or clean-3way dry run says nothing about compiling against
  Solstice; see step 5 of [upstream-intake.md](../../upstream-intake.md).

## Rewrite of `main` (2026-10-19)

The user asked to remove the four Krita developers from Contributors, which
needed a rewrite of `main` and a force-push (`4e849ed12c` to `6dec99bbce`).
`git filter-branch` rewrote the 95 commits from `c822f0df82` onward: the
nine W42 commits took their committer (`toma_omito`) as author and gained an
`Upstream-Author:` trailer; every other commit kept author, committer, dates
and message. Each rewritten commit has the same tree as its original.

- GitHub's signatures on the PR merge commits were dropped, so those merges
  no longer show "Verified".
- Release tags were not moved: `v0.2.0-beta.3` to `v0.4.0` still point to
  the commits their published binaries were built from (the binaries embed
  that SHA), so those tags are no longer ancestors of `main`.
- SHAs in documents written before the rewrite name the old commits. The
  old commits stay reachable through the tags and the merged feature
  branches on `origin`; the local backup ref is
  `backup/main-before-author-rewrite`.
- Another checkout whose `main` still has the old history must reset it
  (`git fetch origin` and `git reset --keep origin/main` on `main`), and a
  branch based on the old `main` must be rebased with
  `git rebase --onto origin/main 4e849ed12c`.

| Commit | Before | After |
| --- | --- | --- |
| U-2026W42-01 | `c822f0df82` | `fa988b9f36` |
| U-2026W42-02 | `0c9a2cb628` | `00bdbc4e36` |
| U-2026W42-03 | `1bf931f228` | `e2b788f4cc` |
| U-2026W42-04 (4-point) | `bd85d92d6c` | `e536c7ebfa` |
| U-2026W42-04 (Qt5 build) | `b6a32e1e33` | `f5f9a771ad` |
| U-2026W42-05 | `8639d50eac` | `01c5c30ec0` |
| U-2026W42-06 (collapse state) | `d831de684b` | `6a385a0f23` |
| U-2026W42-06 (layer locking) | `50efcbd662` | `6cebbc0dc3` |
| U-2026W42-06 (unit test) | `d9e6bbff69` | `51e4754500` |
| `v0.2.0-beta.3` | `c89bc02475` | `02b851215d` |
| `v0.2.0-beta.4` | `3575bbb59c` | `852b077d9f` |
| `v0.2.0-beta.5` | `dd9e48c152` | `f87a86adf8` |
| `v0.2.0-beta.6` | `b7ee343e81` | `11f610aba8` |
| `v0.2.0-beta.7` | `53685b8aae` | `758179124c` |
| `v0.2.0-beta.8` | `4e92a41ee7` | `cc2d751c9f` |
| `v0.3.0` | `f947bc3142` | `7227f9d565` |
| `v0.4.0` | `88386550c1` | `ef983be72f` |
| PR #27 merge (old tip) | `4e849ed12c` | `6dec99bbce` |
