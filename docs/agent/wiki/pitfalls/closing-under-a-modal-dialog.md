---
type: pitfall
updated: 2026-11-03
sources:
  - libs/ui/KisPart.cpp (KisPart::closeSession)
  - libs/ui/KisGpuEngineUi.cpp (KisGpuEngineUi::offerConversion, the stack QMessageBox)
  - libs/ui/KisMainWindow.cpp (slotFileQuit, closeEvent, slotFileNew)
  - libs/ui/tests/KisPartCloseSessionTest.cpp
  - "qtbase v6.8.0 src/gui/kernel/qguiapplication.cpp (QGuiApplicationPrivate::processCloseEvent)"
  - "qtbase v6.8.0 src/corelib/kernel/qcoreapplication.cpp (deferred delete levels in sendPostedEvents)"
related:
  - synthetic-canvas-input.md
---

# Closing a window under its modal dialog

Most Krita dialogs run on the stack of the function that shows them,
parented to the main window:

```cpp
QMessageBox box(QMessageBox::Question, ..., parent);
box.exec();
```

If that window is destroyed while `exec()` runs, `QObject` deletes its
children with `delete`, including the dialog on the stack. On Windows
`RtlFreeHeap` stops the process with `0xC0000374` (heap corruption); with
another dialog the result was `0xC0000005`. The window's deferred delete is
processed inside the dialog's own event loop, because it was posted at a
deeper loop level than that loop (`QCoreApplicationPrivate::sendPostedEvents`).

Stack captured with lldb (2026-11-03, `40f129a3df` build): `RtlFreeHeap` <-
`QObjectPrivate::deleteChildren()` <- `~QWidget` <- `~KisMainWindow` <-
`QObject::event` (DeferredDelete) <- `QDialog::exec()` <-
`KisGpuEngineUi::offerConversion()` (`KisGpuEngineUi.cpp:188`).

## Who can close the window

Qt keeps user input away from a window that a modal dialog blocks:
`QGuiApplicationPrivate::processCloseEvent()` drops close requests for it,
and shortcuts of a blocked window do not match. Checked on Windows with the
GPU engine's conversion prompt open (application-modal): `WM_CLOSE` and
`SC_CLOSE` posted to the disabled main window, and the quit shortcut posted
to the prompt or the main window, changed nothing; closing every top-level
window (`taskkill` without `/F`) closes the prompt first and exits 0; a
session end (`WM_QUERYENDSESSION`, `WM_ENDSESSION`) exits 0. A second main
window is disabled too.

Code is not blocked: `QAction::trigger()`, `QWidget::close()` and
`KisPart::closeSession()` close the window anyway. That covers test
control's `action_trigger file_quit`, Python scripts, and one user path:
a **window-modal** dialog (`Qt::WindowModal`: File > New
(`KisDlgCreateNewDocument`), `KoFileDialog`, the named-autosave recovery
dialog) blocks only its own window, so File > Quit in a second main window
closes the first one under its dialog (`0xC0000005`, reproduced 2026-11-03).

## Fix and rule

`KisPart::closeSession()` returns false while
`QApplication::activeModalWidget()` is set, unless it keeps the windows
(session switching): File > Quit and the last window's close are refused as
Qt refuses user input. Answer or close the dialog first. A script or test
harness that quits with a dialog open now sees Solstice keep running instead
of crashing.

`KisPartCloseSessionTest` calls `closeSession()` from inside a dialog's
`exec()`, application-modal and window-modal; without the check both rows
fail. It cannot create a `KisMainWindow`: `KISTEST_MAIN` makes a plain
`QApplication`, and `KisExtendedModifiersMapper::queryExtendedModifiers()`
casts `qApp` to `KisApplication` (access violation in the input manager's
constructor). The crash itself was checked in the application (Windows,
2026-11-03): the GPU prompt, Image Properties and the two-window File > New
cases no longer crash, and Solstice exits 0 once the dialog is answered.

When writing code that closes or deletes a main window, a view or a
document, assume a modal dialog may be running in a nested event loop above
it. Code after `exec()` must not trust objects that a nested loop can
delete: guard them with `QPointer` (as `QDialog::exec()` does for the
dialog itself).

Not fixed (test control or scripts only): closing the document the GPU
prompt is about (`document_close`) and then choosing Convert in the prompt
hangs the GUI thread, because `offerConversion()` converts the image of a
document that no longer exists. A person cannot reach it: the prompt is
application-modal.
