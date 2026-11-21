# Docker Tray

The Docker Tray is a popup that shows several dockers at once, right at the
cursor. You choose which dockers it shows, arrange them in columns and set
the size of each one. Dockers you have hidden in the main window work on the
tray too, so you can keep the main window clean and still reach Layers,
Brush Presets or Undo History with one click.

It takes the place of the docker area of Krita's right-click Popup Palette,
which Solstice removed.

![The Docker Tray opened over the canvas with the Toolbox, Quick Brush Adjustments, Tool Options, the Quick Access Palette and Layers in three columns, while their places in the main window show that the docker is open in the Docker Tray; the dockers are resized on the tray, and they return to the main window when it closes](images/docker_tray.gif)

## Opening and closing

- **Right-click on the canvas** with a painting tool (brushes, shapes, fill,
  gradient and the other painting tools). Tools that have their own
  right-click menu, such as the selection tools, Transform, Crop, Path and
  Reference Images, keep that menu.
- **Shortcut:** the **Show Docker Tray** action has no key by default. Assign
  one in **Settings → Configure Solstice… → Keyboard Shortcuts**.

The tray opens centered on the cursor and stays on the screen. It closes
when you:

- click or right-click on the canvas,
- press **Esc**,
- press the shortcut again (this also works after clicking inside the tray),
- switch to another application.

When the tray closes, every docker goes back to its place in the main window,
visible or hidden as before.

## Choosing and arranging dockers

Click the gear button in the top-right corner of the tray to open
**Configure Docker Tray**. Without any setting, the tray shows only Layers.

- **Available dockers** (left) lists every docker of the window that is not
  on the tray, including hidden ones.
- **Dockers on the tray** (right) lists the tray's columns ("Column 1",
  "Column 2", …), each with its dockers and their width and height.
- **Add** (or double-click a docker) puts the chosen docker at the end of the
  selected column. **Remove** sends it back to the list on the left.
- **Move Up / Move Down** move a docker within its column; past the top or
  bottom it moves into the previous or next column. With a column selected,
  they move the whole column left or right.
- **Add Column** and **Remove Column** add a column or delete one together
  with its dockers.
- **Width** and **Height** set the selected docker's size, from 50 to 4000
  pixels. The size includes the docker's title.
- **Restore Defaults** goes back to Layers only.

**OK** saves the layout and updates the open tray in place; **Cancel**
changes nothing. If none of the tray's dockers is available, the tray opens
with a short hint and the gear button, so you can always reach the settings.

## Resizing on the tray

You can also resize dockers directly on the tray:

- Drag the thin strip to the **right of a column** (↔ cursor) to change the
  width of every docker in that column.
- Drag the thin strip **under a docker** (↕ cursor) to change only that
  docker's height.

The tray grows or shrinks as you drag. A docker cannot become smaller than
its own minimum size. The new size is saved when you release the mouse
button; the settings dialog shows it and the tray uses it next time. If the
tray grows past the edge of the screen, it moves back onto the screen.

A column is always as wide as its widest docker. When the whole tray would
not fit on the screen, Solstice shrinks the dockers to fit.

## Notes

- While a docker is on the tray, its place in the main window shows "Docker
  is open in Docker Tray". A docker can only be in one place at a time: if
  the toolbar docker box or Quick Brush Adjustments' Tool Options panel shows
  the same docker, the tray shows "Docker is open in …" for it until the
  other one lets it go.
- Layers thumbnails, Quick Brush Adjustments, Quick Access and Rest Note work
  on the tray even while their docker is hidden.
- The Tool Options panel of Quick Brush Adjustments opens only next to the
  Quick Brush Adjustments docker itself, not from the tray.
- The tray stays open when you click elsewhere in the main window (dockers,
  menus); use Esc, the shortcut or a canvas click to close it.
- The layout is shared by all windows.
- Dockers added by Python plugins that rebuild their contents while they are
  on the tray are not supported.
