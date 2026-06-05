#pragma once

#include <windows.h>

#include <vector>

// Manages the set of borderless renderer windows that host the Media Engine.
//
// One window is created per target rectangle (a whole-virtual-screen rect for
// Span, or one rect per monitor for Mirror / Primary / Specific). Each window is
// parented to the desktop wallpaper layer using the globals populated by
// GetWorkerW (g_workerw / g_raisedDesktop / g_progman / g_shellDefView), so the
// three shell layouts (classic WorkerW child, raised-desktop layered Progman
// child, and the no-desktop popup fallback) are all handled here.

// (Re)builds the renderer windows for `targets`. Any previously created windows
// are destroyed first. Returns the HWNDs of the freshly created windows, in the
// same order as `targets`.
std::vector<HWND> BuildRenderWindows(HINSTANCE hInstance, const std::vector<RECT>& targets);

// Destroys all renderer windows created by BuildRenderWindows. Safe to call when
// none exist.
void DestroyRenderWindows();

// The renderer windows currently active (empty when none have been built).
const std::vector<HWND>& RenderWindows();
