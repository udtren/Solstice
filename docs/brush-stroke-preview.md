# Brush Stroke Previews

The **Brush Presets** docker shows each saved brush as a landscape stroke with
its name underneath. Previews use a fixed light color on a dark background,
so they remain readable in light and dark themes. Smudge, deform and filter
brushes use gray stripes to show their effect.

![Brush Presets with stroke previews and the multi-select engine filter](images/brush-presets.png)

The number of columns follows the docker's width, with additional presets
wrapping onto subsequent rows. Tags, search and display controls stay below
the grid. In a narrow docker, the controls wrap into multiple rows to keep them accessible.

The **Engines** and **Bundles** dropdowns contain checkboxes for multiple
selection. Checked entries within one dropdown are combined as alternatives;
engine, bundle, tag and search conditions are combined together. The dropdown
stays open while checking entries (mouse click, Space or Enter); Escape closes
it. **Select All** removes that restriction, and **Clear All** shows no matches
until an entry is checked again. The button shows the number selected.

Bundle filtering uses the preset's registered storage memberships. Identical
copies shared by multiple bundles match any checked member bundle and remain
a single item in the list. **Not in a bundle** covers
local/imported presets and other non-bundle storage; it does not infer which
bundle a copied preset originally came from. Filtering never changes bundle
activation. These filters are specific to each Brush Presets docker. New
docker instances start with all entries selected.

The Brush Presets docker always shows stroke previews; the icon view and its
Thumbnails/Details settings are not available there. The docker's display
menu has one setting, **Preview Size**, which changes the stroke cells
independently of the toolbar brush popup and the Brush Editor's preset list. Those lists
continue to show the original preset icons. The F5 live preview keeps its
existing appearance and live brush-setting behavior.

On first use, visible previews appear progressively. Generated images are
cached locally and reused after restarting Solstice. Scrolling and hiding
the docker stop work on previews that are no longer needed. Painting takes
priority: generation pauses while a document is busy and requests cancellation
when a new stroke starts. A paint engine already executing a job must reach
its cancellation boundary before it can stop.

Previews reflect **saved presets**. Unsaved brush edits do not replace the
preview; the existing modified-preset indicator remains visible. Saving the
brush requests a new preview. No preset file or stored icon is overwritten.

Round Marker, Experiment and Clone engines show an empty preview area with
the brush name. Failed or excessively slow previews also remain empty;
the stored icon is never substituted. A five-second timeout requests
cancellation, but an individual paint-engine job cannot be forcibly stopped.
Cache files are disposable, limited to 256 MB, and unused entries are removed
after 60 days. The feature uses the CPU and does not require the GPU engine.
