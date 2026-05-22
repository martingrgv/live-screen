#include "app/OcclusionWatcher.h"

#include "app/App.h"
#include "media/VlcPlayer.h"

namespace
{
	HWINEVENTHOOK g_foregroundHook = nullptr;
	HWINEVENTHOOK g_minimizeHook   = nullptr;
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

	void CheckAndApply()
	{
		const bool autoMute = ShouldAutoMute();
		if (autoMute == g_autoMuted)
		{
			return;
		}
		g_autoMuted = autoMute;
		ApplyEffectiveMute();
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

	CheckAndApply();
}

void StopOcclusionWatcher()
{
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
	g_appHwnd = nullptr;

	// Clear auto-mute and re-apply so we leave libvlc consistent with the
	// user's manual mute preference only. ApplyEffectiveMute no-ops if the
	// player is already torn down.
	if (g_autoMuted)
	{
		g_autoMuted = false;
		ApplyEffectiveMute();
	}
}

