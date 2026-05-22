#pragma once

#include <windows.h>

// Initializes libvlc and starts looping playback of `initialPath` on `hwnd`.
// Populates the g_vlc / g_player / g_mediaList / g_listPlayer / g_media globals.
// Returns S_OK on success.
HRESULT InitVlc(HWND hwnd, PCWSTR initialPath);

// Stops playback and releases all libvlc objects (idempotent).
void ShutdownVlc();

// Pauses / resumes looped playback without tearing down the libvlc objects.
// Safe to call before InitVlc (no-op) and idempotent.
void PauseWallpaper();
void ResumeWallpaper();

// Applies the effective mute state (g_muted || g_autoMuted) to the active
// libvlc player. Safe to call before InitVlc (no-op).
void ApplyEffectiveMute();

// Switches the looped playlist to `path`. Safe to call after InitVlc succeeded.
HRESULT PlayWallpaperPath(PCWSTR path);
