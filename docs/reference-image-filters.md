# Reference Image Filters

Select one reference image with the Reference Images Tool, then right-click
and open **Filter**. **Add Filter…** opens the native chooser. Choose and
configure an effect, then click **Add Selected Filter**. Effects run from top
to bottom; the same filter can appear more than once.

**Manage Filters…** edits settings, toggles individual effects and reorders or
removes them. The canvas previews the whole stack. **OK** commits one undoable
change; **Cancel** restores the previous settings. **Enable Filters** bypasses
the stack without losing settings. **Clear Filters** removes it (undoable).

Original pixels and placement remain unchanged. Effects are saved in `.kra`
and `.krf`. Existing Saturation and Opacity apply after the stack. The color
picker samples the displayed result.

## Reusable templates

Use **Filter → Save as Template…** to name the current stack. On another image
or in another document, choose **Filter → Apply Template → name**. This replaces
the stack in one undoable operation. Templates survive restarting Solstice.

Templates include effect order, parameters, individual enabled flags and the
whole-stack enabled state. Pixels, placement, Saturation and Opacity are not
included. Applying creates an independent copy: later template edits/deletion
do not affect images already using those settings.

The manager also provides **Save as Template…** and **Apply Template…**. Saving
there captures the edited stack immediately; Cancel restores image edits but
does not remove an explicitly saved template. Applying there is previewed until
OK and can be cancelled. Save with the same name to overwrite (confirmation
required). Names ignore surrounding spaces and case. **Delete Template → name**
deletes after confirmation. Template writes are not part of document Undo.

Templates are stored in the active profile's configuration directory under
`reference-image-filter-templates`. Invalid/newer unsupported files are kept
but omitted from menus. External filter resources are not bundled.

## Supported scope

- Native mask-compatible filters; no G'MIC. One selected image at a time.
- Processing uses the existing 8-bit sRGB reference image representation.
- Every effect is clipped to the source rectangle, including blur edges.
- Large images update asynchronously; the previous result stays visible until
  computation finishes. Moving or zooming does not rerun filters.
- Linked originals are never overwritten. Embedding remains an independent
  option. Resource-based filters may need installed resources on other systems.
- Missing filters are skipped, labeled unavailable and their settings preserved.
- Copy continues to copy original pixels, not the effect stack.
- Older applications may discard effect settings when saving the document.

Interactive validation is pending.
