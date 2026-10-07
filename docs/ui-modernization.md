# Solstice interface

Solstice is gradually refining Krita's interface: calmer colors, clearer
docker headers, document tabs and toolbars, without moving any controls or
changing how they work. Every change is optional, and the original look stays
the default.

## Solstice Dark theme

**Settings → Themes → Solstice Dark** selects a dark color theme with neutral
graphite surfaces:

- The window, input fields and buttons use three distinct shades, with
  buttons slightly lighter so clickable controls stand out.
- Focus and hover use a muted blue that is kept apart from the selection
  color.

Switch back with **Settings → Themes → Krita darker** (or any other theme).

## Solstice interface option

**Settings → Configure Solstice → General → Window → Solstice interface
(docker titles, document tabs, toolbars)** changes three parts of the window:

- **Docker titles** get a header band with a separator line and a little more
  padding, so each docker's title reads as its header. The lock, float and
  close buttons stay where they are.
- **Document tabs** are sized to their titles, so the close button sits next
  to the title. The current tab is lighter and underlined in the selection
  color; other tabs use dimmer text.
- **Toolbars** are separated from the area below by a line, with slightly more
  padding.

The colors follow the current theme, so the option works with light and dark
themes. Clear the check box to return to the original look; the change applies
when the dialog is closed with **OK**.

## Limitations

- Only the main window's docker titles, document tabs and toolbars change.
  Buttons, input fields, menus and dialogs keep their current look.
- The Settings dialog still uses larger text and icons than the main window.
- The color of the area around the canvas comes from **Configure Solstice →
  Display → Canvas Border Color**, not from the theme.
- The theme and the Solstice interface option have been checked on Windows
  at 125% display scaling only.

Agent-facing notes are in
[`agent/ui-modernization-plan.md`](agent/ui-modernization-plan.md).
