#pragma once

#include <windows.h>

// -----------------------------------------------------------------------------
// Media Foundation (IMFMediaEngine) wallpaper playback backend.
//
// Reimplements the player on the OS-native Media Engine so the app ships with
// no third-party runtime: decoding uses the codecs already present in Windows
// (H.264/H.265 in MP4/MOV out of the box). Each renderer window gets its own
// engine rendering directly into it in windowed mode, looping seamlessly via
// SetLoop.
//
// Multi-monitor layouts create one renderer window (and therefore one engine)
// per target rectangle. Media Foundation startup, the D3D11 device and the DXGI
// device manager are created once and shared across all engines.
//
// All player state (the engines, the shared D3D11 device and DXGI manager) is
// encapsulated inside MediaPlayer.cpp instead of being exposed as App.h globals.
// -----------------------------------------------------------------------------

#include <vector>

// Initializes Media Foundation and the shared D3D11 device, and records
// `initialPath` as the wallpaper to play. Does not create any engine yet; call
// SetRenderTargets with the renderer windows afterwards. Idempotent.
HRESULT EnsurePlayerStarted(PCWSTR initialPath);

// (Re)creates one looping engine per window in `windows`, all playing the path
// most recently passed to EnsurePlayerStarted / PlayWallpaperPath. Any existing
// engines are released first. An empty list tears all engines down (nothing
// plays). Requires EnsurePlayerStarted to have succeeded.
HRESULT SetRenderTargets(const std::vector<HWND>& windows);

// Shuts down and releases all engines (but keeps Media Foundation, the D3D11
// device and the current path). Used as the first half of a rebuild so engines
// stop referencing renderer windows before those windows are destroyed.
void ReleaseRenderTargets();

// Releases every engine, the shared D3D11 device and Media Foundation itself.
// Idempotent.
void ShutdownPlayer();

// Pauses / resumes looped playback on all engines without tearing them down.
// Safe to call before any engine exists (no-op) and idempotent.
void PauseWallpaper();
void ResumeWallpaper();

// Applies the effective mute state (g_muted || g_autoMuted) to all engines.
// Safe to call before any engine exists (no-op).
void ApplyEffectiveMute();

// Switches looped playback on all engines to `path` and remembers it for future
// SetRenderTargets rebuilds. Safe to call after EnsurePlayerStarted succeeded.
HRESULT PlayWallpaperPath(PCWSTR path);

// Seeks every clip back to the start. Does not touch pause state, so an
// auto-paused wallpaper stays paused and the seek takes effect on resume.
// Safe to call before any engine exists (no-op).
void RestartWallpaper();

// Re-applies the current g_fillScreen preference (crop-to-fill vs letterbox) to
// every engine, using each engine's own window aspect. Safe to call before any
// engine exists (no-op).
void ApplyAspectMode();
