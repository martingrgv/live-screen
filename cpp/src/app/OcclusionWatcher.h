#pragma once

#include <windows.h>

// Watches the foreground window and auto-mutes playback audio while any
// non-shell window is focused. Unmutes when the desktop / shell is focused.
// The user's manual mute (`g_muted`) still wins — auto-mute can only
// add muting on top of it, never override it to "unmuted".
//
// Additionally maintains `g_pauseFullscreen` (set when a non-shell foreground
// window covers an entire monitor, or when the shell reports a D3D fullscreen
// / presentation app is running) and calls `ApplyEffectivePause` to pause
// playback whenever any pause reason is active.
//
// Driven by SetWinEventHook on EVENT_SYSTEM_FOREGROUND,
// EVENT_SYSTEM_MINIMIZESTART/END and EVENT_SYSTEM_MOVESIZEEND
// (delivered to this thread's message queue via WINEVENT_OUTOFCONTEXT),
// plus a low-rate SetTimer fallback for borderless-fullscreen toggles that
// don't emit a foreground event.
void StartOcclusionWatcher(HWND hwndApp);

// Stops the watcher and clears the auto-mute flag, re-applying the user's
// manual mute preference. Safe to call if not started.
void StopOcclusionWatcher();

// Re-evaluate fullscreen state and auto-mute, then reapply mute/pause.
// Exposed so WindowProc can poke us from WM_TIMER (presence backstop) and
// from the power / WTS handlers when the display or session state changes.
void RecheckPresence();

// Pause iff any of g_pauseFullscreen / g_pauseDisplayOff / g_pauseSessionLock
// is set; otherwise resume. Idempotent (mirrors state via g_autoPaused).
// Safe to call before InitPlayer — MediaPlayer's Pause/Resume helpers no-op then.
void ApplyEffectivePause();
