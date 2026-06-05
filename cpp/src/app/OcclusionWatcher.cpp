#include "app/OcclusionWatcher.h"

#include "app/App.h"
#include "media/MediaPlayer.h"

#include <shellapi.h>
#include <shlobj.h>

namespace
{
	HWINEVENTHOOK g_foregroundHook = nullptr;
	HWINEVENTHOOK g_minimizeHook   = nullptr;
	HWINEVENTHOOK g_moveSizeHook   = nullptr;
	HWND          g_appHwnd        = nullptr;

	bool IsShellOrOwnWindow(HWND hwnd)
	{
		if (!hwnd)
		{
			return true;
		}
		if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow())
		{
			return true;
		}
		if (hwnd == g_progman || hwnd == g_workerw || hwnd == g_shellViewHost
			|| hwnd == g_shellDefView || hwnd == g_appHwnd)
		{
			return true;
		}

		wchar_t cls[64] = {};
		GetClassNameW(hwnd, cls, ARRAYSIZE(cls));
		// Only Progman / WorkerW count as "desktop focus". The taskbar
		// (Shell_TrayWnd / Shell_SecondaryTrayWnd) and Start / Search
		// (Windows.UI.Core.CoreWindow) are deliberately treated as
		// foreground apps so the wallpaper mutes when they are opened.
		if (lstrcmpW(cls, L"Progman") == 0
			|| lstrcmpW(cls, L"WorkerW") == 0)
		{
			return true;
		}
		return false;
	}

	bool ShouldAutoMute()
	{
		return !IsShellOrOwnWindow(GetForegroundWindow());
	}

	// Heuristic: a foreground window whose rect equals (or covers) its
	// monitor's rect is treated as fullscreen. We also consult the shell's
	// user-notification state to catch exclusive-fullscreen D3D apps where
	// the HWND bounds can be misleading.
	bool IsForegroundFullscreen()
	{
		HWND fg = GetForegroundWindow();
		if (!fg || IsShellOrOwnWindow(fg))
		{
			// Even if no "real" foreground app is focused, the shell may still
			// flag a fullscreen D3D / presentation app (e.g. a game minimised
			// from focus but kept in exclusive mode briefly).
			QUERY_USER_NOTIFICATION_STATE state{};
			if (SUCCEEDED(SHQueryUserNotificationState(&state)))
			{
				return state == QUNS_RUNNING_D3D_FULL_SCREEN
					|| state == QUNS_PRESENTATION_MODE;
			}
			return false;
		}

		RECT wr{};
		if (!GetWindowRect(fg, &wr))
		{
			return false;
		}
		HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{ sizeof(mi) };
		if (mon && GetMonitorInfoW(mon, &mi))
		{
			// Allow 1px slack — borderless windows on scaled displays often
			// round to within a pixel of the monitor rect.
			if (wr.left   <= mi.rcMonitor.left   + 1
				&& wr.top    <= mi.rcMonitor.top    + 1
				&& wr.right  >= mi.rcMonitor.right  - 1
				&& wr.bottom >= mi.rcMonitor.bottom - 1)
			{
				return true;
			}
		}

		QUERY_USER_NOTIFICATION_STATE state{};
		if (SUCCEEDED(SHQueryUserNotificationState(&state)))
		{
			if (state == QUNS_RUNNING_D3D_FULL_SCREEN
				|| state == QUNS_PRESENTATION_MODE)
			{
				return true;
			}
		}
		return false;
	}

	void CheckAndApply()
	{
		const bool autoMute = ShouldAutoMute();
		if (autoMute != g_autoMuted)
		{
			g_autoMuted = autoMute;
			ApplyEffectiveMute();
		}

		const bool fullscreen = IsForegroundFullscreen();
		if (fullscreen != g_pauseFullscreen)
		{
			g_pauseFullscreen = fullscreen;
			ApplyEffectivePause();
		}
	}

	void CALLBACK WinEventProc(
		HWINEVENTHOOK /*hook*/,
		DWORD /*event*/,
		HWND /*hwnd*/,
		LONG /*idObject*/,
		LONG /*idChild*/,
		DWORD /*idEventThread*/,
		DWORD /*dwmsEventTime*/)
	{
		CheckAndApply();
	}
}

void StartOcclusionWatcher(HWND hwndApp)
{
	g_appHwnd   = hwndApp;
	g_autoMuted = false;
	g_pauseFullscreen = false;

	// WINEVENT_OUTOFCONTEXT delivers events on this thread's message queue;
	// WINEVENT_SKIPOWNPROCESS avoids self-triggering from our own tray menu.
	g_foregroundHook = SetWinEventHook(
		EVENT_SYSTEM_FOREGROUND,
		EVENT_SYSTEM_FOREGROUND,
		nullptr,
		WinEventProc,
		0, 0,
		WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

	g_minimizeHook = SetWinEventHook(
		EVENT_SYSTEM_MINIMIZESTART,
		EVENT_SYSTEM_MINIMIZEEND,
		nullptr,
		WinEventProc,
		0, 0,
		WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

	// Catches a foreground window being resized into / out of fullscreen
	// without changing focus (e.g. dragging a maximised window down).
	g_moveSizeHook = SetWinEventHook(
		EVENT_SYSTEM_MOVESIZEEND,
		EVENT_SYSTEM_MOVESIZEEND,
		nullptr,
		WinEventProc,
		0, 0,
		WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

	// Backstop poll: borderless games that toggle fullscreen via F11 or
	// internal renderer reconfig don't always emit foreground / move events.
	// 2s is well under any human pause-perception threshold and costs almost
	// nothing (one GetForegroundWindow + GetMonitorInfo).
	if (g_appHwnd)
	{
		SetTimer(g_appHwnd, TIMER_ID_PRESENCE, 2000, nullptr);
	}

	CheckAndApply();
}

void StopOcclusionWatcher()
{
	if (g_appHwnd)
	{
		KillTimer(g_appHwnd, TIMER_ID_PRESENCE);
	}
	if (g_foregroundHook)
	{
		UnhookWinEvent(g_foregroundHook);
		g_foregroundHook = nullptr;
	}
	if (g_minimizeHook)
	{
		UnhookWinEvent(g_minimizeHook);
		g_minimizeHook = nullptr;
	}
	if (g_moveSizeHook)
	{
		UnhookWinEvent(g_moveSizeHook);
		g_moveSizeHook = nullptr;
	}
	g_appHwnd = nullptr;

	// Clear auto-mute / auto-pause and re-apply so we leave playback consistent
	// with the user's manual preferences only. The Apply* helpers no-op if the
	// player is already torn down.
	if (g_autoMuted)
	{
		g_autoMuted = false;
		ApplyEffectiveMute();
	}
	if (g_pauseFullscreen || g_pauseDisplayOff || g_pauseSessionLock)
	{
		g_pauseFullscreen  = false;
		g_pauseDisplayOff  = false;
		g_pauseSessionLock = false;
		ApplyEffectivePause();
	}
}

void RecheckPresence()
{
	CheckAndApply();
}

void ApplyEffectivePause()
{
	const bool shouldPause =
		g_pauseFullscreen || g_pauseDisplayOff || g_pauseSessionLock;
	if (shouldPause == g_autoPaused)
	{
		return;
	}
	g_autoPaused = shouldPause;
	if (shouldPause)
	{
		PauseWallpaper();
	}
	else
	{
		ResumeWallpaper();
	}
}

