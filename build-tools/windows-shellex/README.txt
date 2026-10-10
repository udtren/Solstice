Explorer thumbnails for .kra and .krz files
===========================================

This folder contains the Krita Shell Extension by Alvin Wong (version
1.2.4d, MIT license; see COPYING.txt). It lets Windows Explorer and other
file managers that use Windows thumbnails show the preview stored in .kra
and .krz files.

Solstice has no installer, so the extension is not registered
automatically:

- register-thumbnails.cmd    registers it for the current Windows user.
                             No administrator rights are needed.
- unregister-thumbnails.cmd  removes that registration again.

Run unregister-thumbnails.cmd before you move or delete this Solstice
folder, then register again from the new location. If Explorer keeps the
DLL in use, sign out or restart Explorer before deleting the folder.

If Krita is installed with its own shell extension, the per-user
registration takes precedence for your account and is removed cleanly;
Krita's registration stays in place. The scripts do not change which
program opens .kra files.

More information: docs/windows-shell-thumbnails.md in the Solstice
repository (https://github.com/udtren/Solstice).
