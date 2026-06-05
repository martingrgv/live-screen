#pragma once

#include <windows.h>
#include <shellapi.h>

// -----------------------------------------------------------------------------
// Tray / menu identifiers
// -----------------------------------------------------------------------------
constexpr UINT WM_TRAYICON              = WM_USER + 1;
constexpr UINT ID_TRAY_EXIT             = 1001;
constexpr UINT ID_TRAY_CHANGE_WALLPAPER = 1002;
constexpr UINT ID_TRAY_MUTE             = 1003;
constexpr UINT ID_TRAY_AUTOSTART        = 1004;
constexpr UINT ID_TRAY_FILL_SCREEN      = 1005;
constexpr UINT ID_TRAY_RESTART          = 1006;

// SetTimer id used by the presence/occlusion fallback poll. Borderless
// fullscreen toggles inside an already-focused window don't always emit
// EVENT_SYSTEM_FOREGROUND, so we re-check periodically as a backstop.
constexpr UINT_PTR TIMER_ID_PRESENCE    = 2001;

// -----------------------------------------------------------------------------
// Shared application state
//
// Step 5 of the refactor only splits files. Globals are kept here as `extern`
// declarations and defined in App.cpp so every module can see the same state.
// A later refactor step (3) will fold these into a single AppState class.
//
// The media player's own handles are no longer global: the Media Foundation
// backend keeps them private to MediaPlayer.cpp. Only the cross-cutting
// preference flags below are shared.
// -----------------------------------------------------------------------------
extern HWND g_workerw;
extern HWND g_shellViewHost;
extern HWND g_shellDefView;
extern HWND g_progman;
extern bool g_raisedDesktop;

extern bool g_muted;
extern bool g_autoMuted;

// Independent reasons we may want to pause the wallpaper. Any of them being
// true means playback should be paused; all clear means resume. See
// `ApplyEffectivePause` in OcclusionWatcher for the policy.
extern bool g_pauseFullscreen;   // a non-shell foreground app covers a whole monitor
extern bool g_pauseDisplayOff;   // monitor is off / dimmed, or system is suspending
extern bool g_pauseSessionLock;  // workstation locked / RDP session disconnected
extern bool g_autoPaused;        // mirror of (any reason) currently applied to the player

// When true, crop the video so it fills the wallpaper window (no black bars
// on non-matching panels like 16:10 laptops). When false, the engine letterboxes.
extern bool g_fillScreen;

// Main window procedure (defined in App.cpp).
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
