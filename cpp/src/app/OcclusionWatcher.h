#pragma once

#include <windows.h>

// Watches for fullscreen / occluding foreground windows and pauses libvlc playback
// while the wallpaper is fully covered. Resumes when uncovered.
//
// The watcher uses two signals:
//   * SetWinEventHook on EVENT_SYSTEM_FOREGROUND / EVENT_SYSTEM_MINIMIZESTART /
//     EVENT_SYSTEM_MINIMIZEEND to react immediately to focus / minimise changes.
//   * A 1 Hz fallback timer (TimerProc on the message thread) to catch cases that
//     don't raise a WinEvent (e.g. an already-foreground window toggles fullscreen).
//
// Both signals run on the UI thread, so callbacks are serialised and don't need
// extra synchronisation around the libvlc calls.
void StartOcclusionWatcher(HWND hwndApp);

// Stops the watcher and ensures playback is resumed. Safe to call if not started.
void StopOcclusionWatcher();
