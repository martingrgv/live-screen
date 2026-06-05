#pragma once

#include <windows.h>

// -----------------------------------------------------------------------------
// Media Foundation (IMFMediaEngine) wallpaper playback backend.
//
// Reimplements the player on the OS-native Media Engine so the app ships with
// no third-party runtime: decoding uses the codecs already present in Windows
// (H.264/H.265 in MP4/MOV out of the box). The engine renders directly to the
// wallpaper HWND in windowed mode and loops seamlessly via SetLoop.
//
// All player state (the engine, its D3D11 device and DXGI manager) is
// encapsulated inside MediaPlayer.cpp instead of being exposed as App.h globals.
// -----------------------------------------------------------------------------

// Initializes Media Foundation and starts looping playback of `initialPath` on
// `hwnd`. Returns S_OK on success; on failure any partial state is released.
HRESULT InitPlayer(HWND hwnd, PCWSTR initialPath);

// Stops playback and releases the Media Engine, the D3D11 device and Media
// Foundation itself. Idempotent.
void ShutdownPlayer();

// Pauses / resumes looped playback without tearing down the engine. Safe to
// call before InitPlayer (no-op) and idempotent.
void PauseWallpaper();
void ResumeWallpaper();

// Applies the effective mute state (g_muted || g_autoMuted) to the engine.
// Safe to call before InitPlayer (no-op).
void ApplyEffectiveMute();

// Switches looped playback to `path`. Safe to call after InitPlayer succeeded.
HRESULT PlayWallpaperPath(PCWSTR path);

// Seeks the current clip back to the start. Does not touch pause state, so an
// auto-paused wallpaper stays paused and the seek takes effect on resume.
// Safe to call before InitPlayer (no-op).
void RestartWallpaper();

// Applies the current g_fillScreen preference to the engine. When true, the
// video source is cropped to the window's aspect ratio so it fills non-16:9
// panels (e.g. 16:10 laptops) without distortion; when false, the engine
// letterboxes. `hwnd` is the wallpaper render window (used to read the window
// aspect). Safe to call before InitPlayer (no-op).
void ApplyAspectMode(HWND hwnd);
