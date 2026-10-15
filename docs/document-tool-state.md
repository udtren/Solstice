# Tools when switching documents

Solstice remembers tools separately for each open canvas and tablet input
device, including the pen tip and eraser end. Switching tabs restores the
destination canvas's tool for that device.

After using a pen to select the Brush Tool in document B, visiting document A
and returning to B keeps B's pen tool, even if that pen has not been used in A
yet. Clicking tabs or settings with a mouse does not discard the current pen
state.

If the destination canvas has no history for that device, it starts with that
canvas's default mouse tool and remembers subsequent tool choices separately.
If the restored tool is the Transform Tool, it can show the transform controls
for the selected layer. To leave it, select the Brush Tool before changing tabs.

This correction concerns switching between existing canvases. It does not
change transform modes, layer selection, saved documents, or tool shortcuts.
