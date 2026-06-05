#pragma once

// Rebuilds the wallpaper to match the current layout state (g_multiMonitorMode,
// g_enabledMonitors) and desktop hooking globals.
//
// It computes the render-target rectangles for the active mode, recreates the
// renderer windows, and rebuilds the Media Engines bound to them. Engines are
// released before the old windows are destroyed, and new engines are created
// after the new windows exist, so no engine ever references a dead HWND.
//
// Requires EnsurePlayerStarted to have been called (so a wallpaper path and the
// shared D3D/MF state exist) and GetWorkerW to have populated the desktop-host
// globals. Safe to call repeatedly (tray mode changes, display changes).
void RebuildWallpaper();
