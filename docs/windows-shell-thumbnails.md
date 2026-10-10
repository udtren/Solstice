# Explorer thumbnails for .kra and .krz files (Windows)

Windows Explorer, and file managers that use Windows thumbnails (for example
XYplorer), show the preview stored inside `.kra` and `.krz` files only when a
thumbnail provider for these file types is registered. Krita's installer
registers its shell extension; Solstice has no installer, so its Windows
builds ship the same extension in a `shellex` folder for you to register.

Without it, Solstice still saves the preview correctly, but Explorer shows a
generic icon for new files (older files may keep a thumbnail from Windows'
thumbnail cache).

## Turning thumbnails on

1. Open the `shellex` folder of your Solstice folder (next to `bin`).
2. Run `register-thumbnails.cmd`.

This registers the thumbnail provider for your Windows account only. No
administrator rights are needed and other accounts are not affected. New
thumbnails appear right away; for a file Explorer already cached without a
thumbnail, refresh the folder or clear the thumbnail cache in Disk Cleanup.

## Turning thumbnails off, moving or deleting Solstice

Run `unregister-thumbnails.cmd` from the same `shellex` folder. It removes
only the registration that this folder made.

The registration points at the files in this folder. Before you move or
delete the Solstice folder, run `unregister-thumbnails.cmd`; after moving,
run `register-thumbnails.cmd` again from the new place. If Windows reports
that `kritashellex64.dll` is in use when you delete the folder, sign out or
restart Explorer first.

## With Krita installed

If Krita is installed with its shell extension, your per-user registration
takes precedence while it exists; both use the same extension. Unregistering
returns your account to Krita's registration. The scripts never change
which program opens `.kra` files when you double-click them.

## Scope and limitations

- Thumbnails only: the details pane's image dimensions (Krita's property
  handler) need a machine-wide registration with administrator rights and
  are not registered.
- `.kra` and `.krz` only; OpenRaster (`.ora`) files are not registered.
- The extension is the Krita Shell Extension 1.2.4d by Alvin Wong, under the
  MIT license (`shellex/COPYING.txt`). Solstice ships it unmodified.
