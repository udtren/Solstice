# Reference Tool

Added in **Solstice 0.4.0**. Adjust a reference with multiple native filters
while retaining its original pixels, position, size and rotation. Save a useful
combination as a template for another reference, even in another document.

![A selected reference drawing with Desaturate, Posterize and Glow in the Filter menu; Glow offers enable, edit, reorder and remove controls](images/reference-tool-filters.png)

The example stacks **Desaturate → Posterize → Glow**. Every effect stays
editable: disable it to compare, change its settings, or reorder the stack.
Template commands sit in the same Filter menu.

## Adjust a reference

1. Select one image with the **Reference Images Tool**.
2. Right-click and choose **Filter → Add Filter…**, choose an effect, then
   click **Add Selected Filter** to append it.
3. Use **Manage Filters…** for settings, order and a combined preview.
   **OK** commits the edit; **Cancel** restores the previous stack.
4. Toggle **Enable Filters** to compare with the original, or disable individual
   effects. Stack edits and removal support Undo.

## Reuse a look

Choose **Save as Template…** and name the stack. On another reference, choose
**Apply Template → name** to replace its stack. Templates retain effect order,
parameters and enabled states across documents and application restarts.
Applied images keep independent copies of the settings.

## Scope

Native mask-compatible filters, one selected image at a time; no G'MIC.
Effects stay within the original image bounds. Saturation and Opacity remain
separate controls. Stacks are saved in `.kra` and `.krf`; templates live in the
Solstice profile. See [Reference Image Filters](reference-image-filters.md)
for the full workflow and compatibility limits.
