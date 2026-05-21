#include "app/OcclusionWatcher.h"

#include "app/App.h"
#include "media/VlcPlayer.h"

namespace
{
	// Arbitrary non-zero timer id; scoped to our app HWND so it can't collide
	// with anyone else's timers on the same window.
	constexpr UINT_PTR kOcclusionTimerId = 0xA55E1;
	constexpr UINT     kOcclusionTimerMs = 1000;

	HWINEVENTHOOK g_foregroundHook = nullptr;
	HWINEVENTHOOK g_minimizeHook   = nullptr;
	HWND          g_appHwnd        = nullptr;
	bool          g_paused         = false;

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
		// Skip shell-owned surfaces: pausing because the taskbar or start menu
		// gained focus would be wrong.
		if (lstrcmpW(cls, L"Progman") == 0
			|| lstrcmpW(cls, L"WorkerW") == 0
			|| lstrcmpW(cls, L"Shell_TrayWnd") == 0
			|| lstrcmpW(cls, L"Shell_SecondaryTrayWnd") == 0
			|| lstrcmpW(cls, L"Windows.UI.Core.CoreWindow") == 0)
		{
			return true;
		}
		return false;
	}

	// True when `hwnd`'s window rect covers the entirety of its current monitor's
	// work area. This catches both real fullscreen (D3D exclusive) and the much
	// more common "borderless fullscreen" used by modern games / video players.
	bool CoversItsMonitor(HWND hwnd)
	{
		if (!hwnd)
		{
			return false;
		}
		HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		if (!GetMonitorInfoW(mon, &mi))
		{
			return false;
		}
		RECT wr = {};
		if (!GetWindowRect(hwnd, &wr))
		{
			return false;
		}
		return wr.left   <= mi.rcMonitor.left
			&& wr.top    <= mi.rcMonitor.top
			&& wr.right  >= mi.rcMonitor.right
			&& wr.bottom >= mi.rcMonitor.bottom;
	}

	bool ShouldPause()
	{
		HWND fg = GetForegroundWindow();
		if (IsShellOrOwnWindow(fg))
		{
			return false;
		}
		return CoversItsMonitor(fg);
	}

	void CheckAndApply()
	{
		const bool pause = ShouldPause();
		if (pause == g_paused)
		{
			return;
		}
		g_paused = pause;
		if (pause)
		{
			PauseWallpaper();
		}
		else
		{
			ResumeWallpaper();
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

	void CALLBACK OcclusionTimerProc(HWND /*hwnd*/, UINT /*msg*/, UINT_PTR /*id*/, DWORD /*time*/)
	{
		CheckAndApply();
	}
}

void StartOcclusionWatcher(HWND hwndApp)
{
	g_appHwnd = hwndApp;
	g_paused  = false;

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

	SetTimer(hwndApp, kOcclusionTimerId, kOcclusionTimerMs, OcclusionTimerProc);

	CheckAndApply();
}

void StopOcclusionWatcher()
{
	if (g_appHwnd)
	{
		KillTimer(g_appHwnd, kOcclusionTimerId);
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
	g_appHwnd = nullptr;

	// Make sure we don't leave libvlc paused on shutdown if the last decision
	// was "pause"; ShutdownVlc handles release, but Resume is cheap and safer.
	if (g_paused)
	{
		ResumeWallpaper();
		g_paused = false;
	}
}
