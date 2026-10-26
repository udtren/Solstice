# Upstream intake — agent reference

Solstice does not merge upstream Krita. Selected upstream commits are
cherry-picked one item at a time after the user approves them. This document
is the procedure for that work and the ledger of every decision.

## Roles

| Who | Does |
| --- | --- |
| Hermes Agent (DGX Spark) | Weekly research: collects new upstream commits, dry-runs them against `main`, writes a report with a recommendation per item. Never changes this repository. |
| User | Decides per item: adopt, reject or hold. |
| Claude | Adopts the approved items on `main` and records every decision in the ledger below. |

## Locations

The review lives outside this repository, on the DGX Spark share
`\\192.168.1.37\share\upstream-krita-check`:

- `reports/<YYYY-Www>.md` (Japanese, for the user) and
  `reports/<YYYY-Www>.json` (items, commits, dry-run results, notes for
  Claude);
- `krita.git`: shallow bare mirror of
  `https://invent.kde.org/graphics/krita.git` (`krita/6.0`, `master`);
- `scripts/collect.py`: the deterministic collector;
  `hermes/SKILL.md`: Hermes's instructions; `README.md`: layout.

Items are named `U-<YYYY>W<ww>-<nn>`, for example `U-2026W42-01`.

The upstream snapshot this repository was created from is `krita/6.0`
`e88c22892c80853dc1517fbe6ea2e292b1a3b30f` (2026-09-30). `main` has no
common commit with upstream (independent history), so upstream work can only
arrive as cherry-picks or patches.

## Timing

Upstream is still reviewed every week, but not every approved item is
adopted the week it is reported (user decision, 2026-10-26; reasons on
[the decision page](wiki/decisions/upstream-selective-intake.md)). Hermes
gives each item a `timing`:

| `timing` | Items | Adopt |
| --- | --- | --- |
| `now` | Small, self-contained fixes for crashes, data corruption, out-of-bounds access or security issues; every `krita/6.0` commit | The week it is reported. |
| `soak` | Other bug fixes that exist only on `master` | Held until `soak_until` (14 days after the report) passes with no revert or fix-up upstream, or earlier when backported to `krita/6.0`; at most 28 days, then adopt or reject. |
| `release` | Features, UI changes, large refactors on `master` | Held until an upstream release contains them, then evaluated together. |

Each report re-checks the `held` rows (backport, follow-ups, soak date) and
lists upstream follow-ups (reverts, fixes, backports, same bug number) of
held and recently adopted commits. A revert or fix of an adopted commit is
reported as its own item to adopt now. The user still decides every item;
`timing` is a recommendation. Hermes's instructions for this live in
`hermes/SKILL.md` on the share.

## Adopting an item

1. Read the item in `reports/<week>.json` and its section in the `.md`
   report, then read the agent documents of every Solstice feature the item
   touches (`solstice_files`, and the features in the report).
2. Do not `git fetch` from the share mirror: it is shallow, and fetching
   from a shallow repository makes this repository shallow too. Apply the
   commits as patches with the share helper, one upstream commit at a time,
   in the order the report gives (Git Bash, in the checkout):

   ```bash
   TMPDIR=<scratch-dir> bash //192.168.1.37/share/upstream-krita-check/scripts/intake.sh U-<YYYY>W<ww>-<nn> <full-sha>
   ```

   The helper copies the commit's preimage blobs into this repository (so
   `git am -3` can merge with local objects only), runs
   `git am -3 --keep-non-patch` (plain `git am` drops subject prefixes such
   as `[psd]`), and appends `(cherry picked from commit <sha>)`, the
   trailer `Upstream-Intake: <item>` and the attribution line. The share
   belongs to another user, so it passes `safe.directory` per command. A
   persistent `upstream` remote stays forbidden by `AGENTS.md`.

   Without the share (for example in a cloud session), do the same by hand
   (first used for 2026-W44): fetch the commits from the GitHub mirror
   `https://github.com/KDE/krita.git` into a separate bare repository in
   scratch space, never into this checkout
   (`git fetch --depth=2 --filter=blob:none <url> <sha>...`). Write each
   patch with `git format-patch -1 --full-index --stdout <sha>`, copy the
   preimage blobs named on its `index` lines into this repository
   (`git -C <scratch> cat-file blob <id> | git hash-object -w --stdin`),
   and run `git am -3 --keep-non-patch`. Then add the message lines the
   helper would add.
3. Keep one Solstice commit per upstream commit. When `git am` stops on a
   conflict, resolve it (step 5), `git am --continue`, and add the
   message lines by hand.
4. Re-author each Solstice commit with the local identity (the user or the
   agent) and keep the upstream author as a trailer (user decision,
   2026-10-19; rule in `AGENTS.md`, "Commit authorship and Contributors").
   `git am` keeps the upstream author, and GitHub lists every commit author
   on `main` under the repository's Contributors. Right after each helper run (or
   `git am --continue`), before applying the next upstream commit, run once:

   ```bash
   git commit --amend --no-edit --reset-author --trailer "Upstream-Author: $(git log -1 --format='%an <%ae>')"
   ```

   The command substitution reads the upstream author before the amend
   runs. Running it a second time would add a trailer with your own
   identity, so check the result with `git log -1 --format='%an <%ae>%n%B'`.
   Do not record the upstream author as `Co-authored-by`, because GitHub
   counts that trailer as co-authorship. Upstream backports to
   `krita/6.0` can already carry a `Co-authored-by:` line naming the
   original author; remove it from the message (the `Upstream-Author`
   trailer names the person). `--reset-author` takes the local
   `user.name` and `user.email`. The commits adopted in 2026-W42 were
   re-authored the same way by a one-time rewrite of `main` on 2026-10-19.
5. Resolve conflicts in favor of Solstice behavior: removed Android
   branches stay removed, application strings keep "Solstice", and the
   touch points in `feature-inventory.md` keep working. When the conflict
   is more than mechanical, stop and ask the user. A clean 3-way apply is
   not proof either: when `git am` falls back to 3-way, the upstream
   preimage contains later upstream changes, so read the merged result
   against the Solstice code it now runs in (in 2026-W44, `e09952bcad`
   merged cleanly but called `clear()` on storage that only an unadopted
   upstream refactor provides). Record any such adaptation in the commit
   message and the ledger note.
6. Format, build, test and install as the touched area requires
   (`development-workflow.md`, the feature documents), then run the item's
   `claude_notes` checks and list the manual checks for the user.
7. Record the decision in the ledger below in the same change, with the
   Solstice commit SHA after the amend of step 4. Update
   `feature-inventory.md` only when the adoption changes a Solstice touch
   point.

## Rejecting or holding an item

When the user rejects or holds an item, add its commits to the ledger with
`rejected` or `held` and the user's reason in the note. Nothing else
changes. A held item that is later adopted or rejected gets its row updated.

## Ledger

One row per upstream commit, full 40-character SHA. `collect.py` reads this
table from `main`: commits marked `adopted` or `rejected` (and upstream
backports whose `cherry picked from` line names them) are not reported
again. `held` rows stay candidates, so the next report re-checks them.

Decision values: `adopted`, `rejected`, `held`. The Solstice commit column is
the adopting commit (`-` otherwise). The 2026-W42 rows give the commits on
`main` after the 2026-10-19 author rewrite; the pre-rewrite SHAs are in
[the decision page](wiki/decisions/upstream-selective-intake.md).

| Item | Upstream commit | Branch | Decision | Solstice commit | Date | Note |
| --- | --- | --- | --- | --- | --- | --- |
| U-2026W42-01 | `653c3b6bb7f7bbb43821a886fb9d71de241e0d90` | krita/6.0 | adopted | `fa988b9f36` | 2026-10-14 | Relax an assert to a safe assert in KisResourceThumbnailCache |
| U-2026W42-02 | `efabcb3781ee37b7d9e83276a9b280f1c6700e00` | krita/6.0 | adopted | `00bdbc4e36` | 2026-10-14 | Disable left-click scrolling on animation curves |
| U-2026W42-03 | `65ad64022b39bede8697e13b7be354d5b7adac66` | krita/6.0 | adopted | `e2b788f4cc` | 2026-10-14 | Fix invalid widget class name |
| U-2026W42-04 | `c21eee1b878b2c24c86e1685960ee94412141e08` | master | adopted | `e536c7ebfa` | 2026-10-14 | Fix conditions for 4-point transformation |
| U-2026W42-04 | `3ee2270d8ead36966a055ab72265b8d767d162b8` | master | adopted | `f5f9a771ad` | 2026-10-14 | Make perspective transform build under Qt5 again |
| U-2026W42-05 | `4bdaa2f4ac08e0cba011f2c6e561e005d1639f23` | master | adopted | `01c5c30ec0` | 2026-10-14 | Fix horizontal scrolling with the second mouse wheel |
| U-2026W42-06 | `fa7e98c931d646e7d7f52188c5b414cf6b5ddff2` | master | adopted | `6a385a0f23` | 2026-10-14 | Save and load the collapse state of layer groups to PSD files |
| U-2026W42-06 | `496f786bdb7cbbe6db0657f757c46e023c9f47e3` | master | adopted | `6cebbc0dc3` | 2026-10-14 | Implement layer locking read/write in the PSD format |
| U-2026W42-06 | `4216dd347f843d59ca51c1e097a0dc8af7c753aa` | master | adopted | `51e4754500` | 2026-10-14 | [psd] Add a unittest for user-lock and collapsed PSD fields |
| U-2026W42-07 | `8972b631af37631af33c480741d18deb7b6b1565` | krita/6.0 | rejected | - | 2026-10-14 | Add miscellaneous frame icons; user: not adopted (recommendation consider) |
| U-2026W42-07 | `b4960514a46f9dec6004e87ea8294debb50149cb` | krita/6.0 | rejected | - | 2026-10-14 | Add isolate group / layer icons; user: not adopted (recommendation consider) |
| U-2026W42-07 | `fa1e3484ff8f1b06e654e64747c1594bc0fd62c6` | krita/6.0 | rejected | - | 2026-10-14 | Add brush option slider 1-5 icons; user: not adopted (recommendation consider) |
| U-2026W42-07 | `6ef66b905a44bc7a63911488ffd099eed91bf882` | krita/6.0 | rejected | - | 2026-10-14 | Add and revamp increase/decrease property icons; user: not adopted (recommendation consider) |
| U-2026W42-07 | `eb9f1bcabb882e0e1e8ef5ed1427843607eb0187` | krita/6.0 | rejected | - | 2026-10-14 | Remove trailing spaces from svg-icons.qrc; user: not adopted (recommendation consider) |
| U-2026W42-07 | `89e8c5d17a169d456cc6d16dadad44f5e6348aab` | krita/6.0 | rejected | - | 2026-10-14 | Add 16px frame icons for frame icons; user: not adopted (recommendation consider) |
| U-2026W42-07 | `a7809adb610ab565fd05b2d0fe2fbf2d71129699` | krita/6.0 | rejected | - | 2026-10-14 | Add pin to timeline icon; user: not adopted (recommendation consider) |
| U-2026W42-07 | `7c5fe72b750beda115ab2e5c0cdee4e9f2d87c85` | krita/6.0 | rejected | - | 2026-10-14 | Add wrap direction icons; user: not adopted (recommendation consider) |
| U-2026W42-08 | `303407ff4f8b79a5893328c9daeed047e0aec283` | master | rejected | - | 2026-10-14 | Use an image job for capturing recorder frames; user: not adopted (recommendation consider) |
| U-2026W42-08 | `9f92f4cde35b226fcc9a8ceb2d7cab7fba6b33d3` | master | rejected | - | 2026-10-14 | Allow more tool during recording; user: not adopted (recommendation consider) |
| U-2026W42-08 | `0ece3d797416a4e5ae96ee3c8c6903b6c7789703` | master | rejected | - | 2026-10-14 | Prevent writers from quiting while a job is pending; user: not adopted (recommendation consider) |
| U-2026W42-09 | `c9c44bee771357b8339c6a12d3022bb136d9e3b7` | master | rejected | - | 2026-10-14 | Added Bayer dithering option to Gradient Tool; user: not adopted (recommendation consider) |
| U-2026W42-10 | `6d451f2e9d5a48541bb7e2484fd74522779d2bb2` | master | rejected | - | 2026-10-14 | Consolidate brushtip export widgets; user: not adopted (recommendation consider) |
| U-2026W42-10 | `4c2d9a51dcb044a34877d061f8c84a5c5adc146c` | master | rejected | - | 2026-10-14 | Remove unused parameter; user: not adopted (recommendation consider) |
| U-2026W42-11 | `c42f07284f23f96d2fcbb0400133acef2d69c4cf` | master | rejected | - | 2026-10-14 | ENHANCEMENT: Expose the live brush position to Python scripts; user: not adopted (recommendation consider) |
| U-2026W42-11 | `5d79890f9e340138bd6e8532db0b35f85dd15d34` | master | rejected | - | 2026-10-14 | Fix formatting.; user: not adopted (recommendation consider) |
| U-2026W42-11 | `961b654161ded8f6321e4bb3955321d35c0d3578` | master | rejected | - | 2026-10-14 | Add Python API for input-aware brush strokes; user: not adopted (recommendation consider) |
| U-2026W42-11 | `c7b05604b38f4cc038a89d4569ddbc5b5564397a` | master | rejected | - | 2026-10-14 | Python: Rename paintStroke to paintPolylineStroke.; user: not adopted (recommendation consider) |
| U-2026W42-12 | `a89867be92a62d36272ceca066bce84a629edfcc` | master | rejected | - | 2026-10-14 | Remove all uncertain plural forms from translatable strings; user: not adopted (recommendation consider) |
| U-2026W42-13 | `b16bd1a171a66d534df5c7cd8f8cb08d62ab1132` | master | rejected | - | 2026-10-14 | Use popup flag for color picker popups on all platforms; user: not adopted (recommendation reject) |
| U-2026W42-13 | `fea0d876f7a943a3f27404f693e53b525bd50040` | master | rejected | - | 2026-10-14 | Fix positioning of the color preview popup on Wayland; user: not adopted (recommendation reject) |
| U-2026W42-14 | `8d9ed135b4a066f3aed2416aa49c4ccca71048ed` | krita/6.0 | rejected | - | 2026-10-14 | Update AppData to point to 5.3.4.1; user: not adopted (recommendation reject) |
| U-2026W42-15 | `1cb2aa1a7cbc6471c7da7977b0ad956b8ea19aab` | master | rejected | - | 2026-10-14 | Drop obsolete FOUND_VAR args to find_package_handle_standard_args; user: not adopted (recommendation reject) |
| U-2026W42-16 | `c523e544bcbb3b69fcce12abf9257faeac952a95` | master | rejected | - | 2026-10-14 | tmp commit; user: not adopted (recommendation reject) |
| U-2026W42-16 | `b116b2e79327e9dee30b0baa1ac99ebc40ab134c` | master | rejected | - | 2026-10-14 | Add brush option slider 1-5 icons; user: not adopted (recommendation reject) |
| U-2026W42-16 | `7de61db1d64bc2b2faa0a087417df1bde5f0cf4b` | master | rejected | - | 2026-10-14 | clean up svg-icons.qrc; user: not adopted (recommendation reject) |
| U-2026W44-01 | `630b6a20d476d697453fc24e1c952148e3007abb` | master | held | - | 2026-10-26 | Rewrite KisClipboard to handle non-transparent pixels; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `74fa6fec164e8c925f5f95e230eb9b02582c0a17` | master | held | - | 2026-10-26 | Add unittest for Clones and Masks copying; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `bf1cb53b03946138c6649b7b1a0d2c433d4cb874` | master | held | - | 2026-10-26 | Fix "Paste at cursor" action; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `28bbeb6483dfdb64d7800187a57ec45662b6f733` | master | held | - | 2026-10-26 | Fix copying of data from animated layers; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `435dd4989f6110a0ff76722206b58deb1ef2f1be` | master | held | - | 2026-10-26 | Fix pasting clips which are out-of-bounds of the image; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `879f1d35d492801243f0d8b57b65a2814227a74b` | master | held | - | 2026-10-26 | Fix build on Qt5; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `4fbea101d5002f15d8c5765e851376ce0636a5e0` | master | held | - | 2026-10-26 | Detach Custom and Clipboard Image dialog defaults; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `178da930544f69f0a12438d08a1f230382ddd8fd` | master | held | - | 2026-10-26 | Add handling of "application/x-qt-image-has-alpha"; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `0a84c23fe55be3928cb5b0bfeffbecfb05031e23` | master | held | - | 2026-10-26 | Don't create undo command for Clipboard Copy actions; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-01 | `903a17397f5fbe8a26c41562659febfa2f7fb5a5` | master | held | - | 2026-10-26 | Unbreak KisClipboardTest; user: adopt later in its own PR; needs a manual port (depends on rejected U-2026W42-10 `6d451f2e9d`; keep the Solstice empty-clipboard guard and test) (recommendation adopt-high) |
| U-2026W44-02 | `80f91a53ffacfa8eda3d4f3bef74753b31891379` | master | held | - | 2026-10-26 | Don't try to use mime data with pointers it is comes from clipboard; user: adopt with U-2026W44-01 in its separate PR (kis_mimedata.h depends on it) (recommendation adopt-high) |
| U-2026W44-02 | `820fee34f43f2d95f30fd5ccbda7907040c698a2` | master | held | - | 2026-10-26 | Don't try to read image pointer until PID is verified; user: adopt with U-2026W44-01 in its separate PR (kis_mimedata.h depends on it) (recommendation adopt-high) |
| U-2026W44-03 | `9735e24058edbfc90e774d4679f4fcf25196f58d` | master | adopted | `96a401669f` | 2026-10-26 | Fix crash in color palette when the palette is not initialized |
| U-2026W44-03 | `a21f7f930fa501716ab6c15dbd34b8fd9b9d5fb2` | master | adopted | `14fd8a4904` | 2026-10-26 | Fix (partially) enabled state of the buttons in Palette docker |
| U-2026W44-03 | `23d555e5912e01661580da9200fcb08c73e29fa2` | master | adopted | `f8f47f6d8e` | 2026-10-26 | Fix crash when trying to export non-existing palette |
| U-2026W44-04 | `58df841d9da730956b35558a459834f7d18eb6eb` | master | adopted | `d2fc389852` | 2026-10-26 | Fix possible out-of-bounds read when loading a Paint Shop Pro palette |
| U-2026W44-05 | `46f8ef51c5f55bef469682f4c824b33136084213` | master | adopted | `2daacf0a20` | 2026-10-26 | Avoid allocations bottle-neck on Widnows |
| U-2026W44-05 | `e09952bcadfcb7bc709448fc00f41c344c44004a` | master | adopted | `87ce85fb1d` | 2026-10-26 | Fix reference counting in KoCachedColorConversionTransformation; adapted: destructor resets the std::optional fast-path storage (upstream clear() needs the KisRotatingCache refactor) |
| U-2026W44-06 | `6f43b89f81387a49136222bdc247bf0671e00e75` | master | adopted | `813d933611` | 2026-10-26 | Fix updates on copy-paste of shape layers with resolution change |
| U-2026W44-06 | `5d08fd57d0f85e6167ba00b03291d716d0ce5181` | master | adopted | `4f6742c67a` | 2026-10-26 | Fix copy-pasting of shapes between vector layers and selections |
| U-2026W44-07 | `fb9e0cd8933f4e23fb61061de0053a7bd261396c` | master | adopted | `b2e5e2ee6e` | 2026-10-26 | Fix being impossibility to clone-paste frames multiple times |
| U-2026W44-08 | `7b12d5ce4e57da136c32c2f4cc63156238c6ec6e` | krita/6.0 | adopted | `91498a6a3e` | 2026-10-26 | Remove some extraneous padding from text properties docker.; backport of master 517524af30 |
| U-2026W44-09 | `cc294cad7493cb9c610a2336c3b1e0aa5b6efd4c` | master | adopted | `03c5106211` | 2026-10-26 | Fix autocomplete to be less aggressive in the font dropdown. |
| U-2026W44-10 | `06cec84485828e6a557bd0db433dca95fdd72b96` | master | rejected | - | 2026-10-26 | Add basic gamut mapping filter to Krita; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `375b7f34dadc6c7dab5e12efa938582306decd47` | master | rejected | - | 2026-10-26 | Implement basic Reinhard tonemapping.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `d1870b48993f34f177e9279b4547940d110ccd73` | master | rejected | - | 2026-10-26 | Add config widget to tone gamut map filter.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `90134ee75ba329d771e8dc67dab8a630b60e01aa` | master | rejected | - | 2026-10-26 | Implement extended reinhard, so we can control the white-brightness.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `d627c35dd41b060257e79a9dcf271113a1d1718c` | master | rejected | - | 2026-10-26 | Cleanup gamut mapping adjustments, implement ICtCp and OkLab gamut mapping.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `9f395628eb1fb0e963f52ca76d8552af07406708` | master | rejected | - | 2026-10-26 | Simplify the tonemapping adjustment; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `972152a26bc2301a5c1f136327d1ca9fe016615d` | master | rejected | - | 2026-10-26 | Implement piecewise filmic tone mapping.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `dd2d2236ab43b9f288c2bf39391b8b9f9f921700` | master | rejected | - | 2026-10-26 | Fix qt5 build.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `467c4b0f0af3bf11b5d44746b8c8b73cccbde286` | master | rejected | - | 2026-10-26 | Fix build on windows; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `49fa7615f71ad44d4b5b351fccf0cff805b22f12` | master | rejected | - | 2026-10-26 | Fix some bugs in piecewise filmic, improve ui with tool tips.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `b08e49cbada9d9d844745ce57eb5f1d7a9041557` | master | rejected | - | 2026-10-26 | Fix memory leaks in the gamut and tone map adjustments.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `a5d41cdf5a206f3989a55eda1908295a6d52a356` | master | rejected | - | 2026-10-26 | Add a cusp based gamut mapping method.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `59d97c3cc219c57332461797a97a24c4bac4cd3e` | master | rejected | - | 2026-10-26 | Ensure that generated cusp will use the full range of a channel.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `ac44c1630e784030ad159bd729b549d1ae2e2ff0` | master | rejected | - | 2026-10-26 | Remove delta E2000 implementation, as it is buggy and unused.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `f987fb4e735794b208fb17bc109de3c030f48b2a` | master | rejected | - | 2026-10-26 | Fix cusp mapping mode to work properly with magentas.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `c4efc7dcf44a72d0ffee3f5b43948eb14827366c` | master | rejected | - | 2026-10-26 | Move the cusp gamut map code so it works better with gradients.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `f0745da86eab4baf21101b4cd7ad811d830e5e17` | master | rejected | - | 2026-10-26 | Use relative colorimetric for cusp map creation.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `76b6e32208136cd6eb0681cd271223319b116bb4` | master | rejected | - | 2026-10-26 | Make the axis values on the tone curve widget configurable.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `7e5cdfd39e425e0bd583421a9d6e6d67de96e229` | master | rejected | - | 2026-10-26 | Fix qt5 build by using qmin correctly.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `eea041949d59bba6128df4ca73cad7d8c961046e` | master | rejected | - | 2026-10-26 | Try to fix magentas in LAB a little better.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `99c4c2226d4dfc18555ce1afeb057f827361ef40` | master | rejected | - | 2026-10-26 | Remove excessive copying in KoGamutMappingUtils::cuspForHue; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `5dfe4f8c6f9b66589d463f53e162f21a2734ca22` | master | rejected | - | 2026-10-26 | Fix allocations in tonemapping filter; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `c9fb938f24aaf23e24ba5235c14687af70bb9670` | master | rejected | - | 2026-10-26 | Remove usage of QVector<float> in the helper functions.; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `eb8d74773adba86fd2f77a4075b5381a2b9449d2` | master | rejected | - | 2026-10-26 | Fix slowdown in delta-e tonemapping method; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-10 | `2ca1afd42d4a8a5a5e937931bb8f9ce229a23048` | master | rejected | - | 2026-10-26 | Move KisRotatingCache to a separate file and add a unittest; user: not adopted; new CPU-only tone/gamut mapping filters, master only (recommendation consider) |
| U-2026W44-11 | `ec6a328c805b397dddbacd48d5e1a950540c0792` | master | held | - | 2026-10-26 | Prefer full matches over partial matches when searching fonts.; user: hold until the master-only prerequisite of KoFFWWSConverter.cpp is identified (recommendation consider) |
| U-2026W44-12 | `2687e5d4b4c7854439b6d327b2aac133a987c734` | master | rejected | - | 2026-10-26 | News notification Feature; user: not adopted; conflicts with the Solstice two-part News area (recommendation consider) |
| U-2026W44-13 | `ebc6d1074dd1a32997a9553a95116705e0a4d669` | master | rejected | - | 2026-10-26 | Expanding Recent Document; user: not adopted; conflicts with the Solstice welcome page (recommendation consider) |
| U-2026W44-14 | `256654ff31e91e38644599498570e86c01768dad` | master | rejected | - | 2026-10-26 | Use native color picking api for screen color sampling; user: not adopted; Qt's Windows QPA has no ColorPicking capability (recommendation reject) |
| U-2026W44-14 | `6dd0044ee17c12d23bd70af011a45ccecbcb6b17` | master | rejected | - | 2026-10-26 | Don't skip native color sampling if "real canvas sampling" is on; user: not adopted; Qt's Windows QPA has no ColorPicking capability (recommendation reject) |
| U-2026W44-14 | `da7c64398edcb3c2fb9cf1815f8ee06987df5301` | master | rejected | - | 2026-10-26 | Fix color space of the natively picked color; user: not adopted; Qt's Windows QPA has no ColorPicking capability (recommendation reject) |
| U-2026W44-15 | `1e79bbd698a76f72119dfe4ea27abece20f85f33` | master | rejected | - | 2026-10-26 | Refactor KoToolManager to belong to KisViewManager; user: not adopted; large refactor, conflicts with Document tool state (recommendation reject) |
| U-2026W44-16 | `c8028b3e2e0d985ae181a0d66aeafed698663445` | master | rejected | - | 2026-10-26 | Fix DBus (and global menu) initialization broken by probing; user: not adopted; Linux DBus initialization only (recommendation reject) |
| U-2026W44-16 | `be4c3b951ee5230d413e2a67698203946e19733f` | master | rejected | - | 2026-10-26 | Fix compilation on Qt5; user: not adopted; Linux DBus initialization only (recommendation reject) |
| U-2026W44-17 | `b5dc03a078f88bc7cd7db33f865b7ef38bcd9dcc` | krita/6.0 | rejected | - | 2026-10-26 | Add icons for timeline column actions; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-17 | `2fb4d3298af33f018ee6ab456fd72061ae23ede6` | krita/6.0 | rejected | - | 2026-10-26 | Add icons for set start/end time and clear cache; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-17 | `9fb9e652d64b146b2dfb2e36afa966ec9ae4959e` | krita/6.0 | rejected | - | 2026-10-26 | Restore SPDX header in svg-icons.qrc; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-17 | `b735f55abeeccfb1a703abfef0e5cc5470286ea3` | krita/6.0 | rejected | - | 2026-10-26 | Add 16 px copy/paste icons; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-17 | `3fe9854445b63059bbe5be183b5499e277f2cc39` | krita/6.0 | rejected | - | 2026-10-26 | Revamp open-recent icon; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-17 | `ce744c58a57ccac9ddf1e79d0151e8c673a428c7` | krita/6.0 | rejected | - | 2026-10-26 | Fix typo in icon file name; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-18 | `e3a173fdd9e77ac3edcd1ee071bcd494d9753a04` | master | rejected | - | 2026-10-26 | Add icon for script action group; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-18 | `c2142d5e1738f95857a506b4bd3fbde161cf2c82` | master | rejected | - | 2026-10-26 | Fix typo in svg-icons.qrc; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-18 | `ed72aaa35422ef2f89e31bc34eca976c150385f4` | master | rejected | - | 2026-10-26 | Fix typo in icon file name; user: not adopted; icons, same as U-2026W42-07 (recommendation reject) |
| U-2026W44-19 | `cab187c0245bbd640de5221214046311012af63e` | krita/6.0 | rejected | - | 2026-10-26 | Fix horizontal scrolling with the second mouse wheel; user: not adopted; backport of adopted U-2026W42-05 (already in Solstice) (recommendation reject) |
