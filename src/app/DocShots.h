// DocShots - hidden "--shot <scene> --out <png>" mode (phase 9-fix1).
//
// Regenerates the six README screenshots (docs/*.png) WITHOUT any real
// desktop content: every scene sits on an app-created solid-color backdrop
// window and the capture rect is cropped to the target window / menu only,
// so no wallpaper, taskbar, third-party window or file name can leak into
// the public repository.
//
// Scenes (invoked by tools/gen_docs_shots.ps1, combine with --theme):
//   main     - MainWindow with injected sample text over a solid backdrop
//   settings - SettingsWindow over a solid backdrop
//   popup    - PopupCard floating over an app-created white "source text"
//              placeholder window (a few lines of sample English)
//   overlay  - OverlayWindow + ControlBar over the same kind of white
//              placeholder window; translated panels shown
//   tray     - the tray context menu popped over a solid backdrop, capture
//              cropped to the menu rect (no taskbar, no other tray icons)
//
// Testability scaffolding in the spirit of --open-settings / --selftest:
// no production code path is altered.
//
// Note: PrintWindow(PW_RENDERFULLCONTENT) renders the DWM Acrylic/Mica
// backdrop as black for our top-level windows, so the equivalent
// "single-window rect crop over an app-owned solid backdrop" is used
// instead - same privacy outcome (window-only, never full screen).

#pragma once

#include <QStringList>

namespace docshots {

// Returns a process exit code: 0 = png written, 2 = bad arguments,
// 3 = capture or save failed.
int runShot(const QStringList &args);

} // namespace docshots
