#pragma once

#include <windows.h>

// Initializes libvlc and starts looping playback of `initialPath` on `hwnd`.
// Populates the g_vlc / g_player / g_mediaList / g_listPlayer / g_media globals.
// Returns S_OK on success.
HRESULT InitVlc(HWND hwnd, PCWSTR initialPath);

// Stops playback and releases all libvlc objects (idempotent).
void ShutdownVlc();

// Switches the looped playlist to `path`. Safe to call after InitVlc succeeded.
HRESULT PlayWallpaperPath(PCWSTR path);
