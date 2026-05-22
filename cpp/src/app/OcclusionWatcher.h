#pragma once

#include <windows.h>

// Watches the foreground window and auto-mutes libvlc audio while any
// non-shell window is focused. Unmutes when the desktop / shell is focused.
// The user's manual mute (`g_muted`) still wins — auto-mute can only
// add muting on top of it, never override it to "unmuted".
//
// Driven entirely by SetWinEventHook on EVENT_SYSTEM_FOREGROUND and
// EVENT_SYSTEM_MINIMIZESTART/END (delivered to this thread's message queue
// via WINEVENT_OUTOFCONTEXT). No polling timer — focus transitions are
// the only thing we react to, and WinEvents are reliable for those.
void StartOcclusionWatcher(HWND hwndApp);

// Stops the watcher and clears the auto-mute flag, re-applying the user's
// manual mute preference. Safe to call if not started.
void StopOcclusionWatcher();
