# Layer Comps - implementation plan

Status (2026-10-16): LC1-LC4 implemented, built, tested and installed;
user-confirmed operation and resize layout refinement.
The user authorized implementation after the 2026-10-15 review.

The maintained architecture, compatibility contract, tests and manual checks
now live in [Layer Comps development notes](layer-comps.md). The user guide
is [Layer Comps](../layer-comps.md).

The implementation extends the existing Compositions docker and preserves
its ID, actions, stored records and export selection. It includes undoable
apply, multiple selection, drag ordering, previous/next, a per-document
transient last-state row, and a persisted new-layer visibility policy.

Review corrections incorporated: synchronization covers the list and flag
as well as each comp; graph reads use the image barrier; only the added
subtree is recorded; zero-comp documents persist the flag; list edits mark
the document modified; double-click consistently applies; Undo/Redo and
multiple views share the transient state; export does not replace it.

LC5 remains optional and unimplemented: copied-image asynchronous export,
cancellation and additional output formats.
