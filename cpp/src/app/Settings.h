#pragma once

#include <windows.h>
#include <string>
#include <vector>

#include "app/App.h"

// Persistence for user-tunable settings.
//
// Storage: %APPDATA%\LiveScreen\config.ini, written as UTF-16 LE with a BOM so
// the Win32 private-profile API round-trips non-ASCII paths correctly.
//
// All functions are safe to call before the directory or file exist; the layer
// creates them on demand.

// Loads the last-used wallpaper path. Returns S_OK and fills `out` when a path
// is stored, S_FALSE when no path has been saved yet (or the file is missing),
// or an HRESULT failure when the file exists but cannot be read.
HRESULT LoadWallpaperPath(std::wstring& out);

// Persists `path` as the wallpaper to restore on next launch.
HRESULT SaveWallpaperPath(PCWSTR path);

// Loads the "fill screen" (crop-to-window) preference. Returns S_OK and fills
// `out` when a value is stored, S_FALSE when no value has been saved yet.
HRESULT LoadFillScreen(bool& out);

// Persists the "fill screen" (crop-to-window) preference.
HRESULT SaveFillScreen(bool value);

// Loads the multi-monitor layout mode. Returns S_OK and fills `out` when a value
// is stored, S_FALSE when no value has been saved yet (caller keeps its default).
HRESULT LoadMultiMonitorMode(MultiMonitorMode& out);

// Persists the multi-monitor layout mode.
HRESULT SaveMultiMonitorMode(MultiMonitorMode value);

// Loads the set of enabled monitor device names (Specific mode). Returns S_OK
// and fills `out` when a value is stored, S_FALSE when none has been saved yet.
HRESULT LoadEnabledMonitors(std::vector<std::wstring>& out);

// Persists the set of enabled monitor device names (Specific mode).
HRESULT SaveEnabledMonitors(const std::vector<std::wstring>& value);
