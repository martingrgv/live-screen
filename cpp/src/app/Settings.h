#pragma once

#include <windows.h>
#include <string>

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
