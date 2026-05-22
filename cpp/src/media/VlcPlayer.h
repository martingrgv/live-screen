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

// Seeks the currently playing item back to the start. Does not touch pause
// state, so an auto-paused wallpaper stays paused and the seek takes effect
// on resume. Safe to call before InitVlc (no-op).
void RestartWallpaper();

// Applies the current g_fillScreen preference to the active player. When the
// flag is true, libvlc is told to crop the video to the window's aspect ratio
// (fills non-16:9 panels like 16:10 laptops without distortion). When false,
// the crop is cleared and libvlc letterboxes as usual.
//
// `hwnd` is the wallpaper render window (used to read the window aspect).
// Safe to call before InitVlc (no-op).
void ApplyAspectMode(HWND hwnd);
