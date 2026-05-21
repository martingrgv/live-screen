#pragma once

#include <windows.h>

// Locates (or spawns) the WorkerW window that hosts the desktop wallpaper, and
// caches related state (Progman, SHELLDLL_DefView, raised-desktop flag) into
// the globals declared in App.h.
HWND GetWorkerW();
