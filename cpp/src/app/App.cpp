#include "app/App.h"

#include "app/OcclusionWatcher.h"
#include "app/Settings.h"
#include "app/WallpaperLayout.h"
#include "media/MediaPlayer.h"
#include "ui/TrayIcon.h"
#include "util/Autostart.h"
#include "util/UniqueHandles.h"
#include "win/DesktopHost.h"
#include "win/Monitors.h"
#include "win/RenderWindows.h"

#include <vector>

#include <algorithm>

#include <wtsapi32.h>

// -----------------------------------------------------------------------------
// Global definitions (declared extern in App.h).
// -----------------------------------------------------------------------------
HWND g_workerw       = nullptr;
HWND g_shellViewHost = nullptr;
HWND g_shellDefView  = nullptr;
HWND g_progman       = nullptr;
bool g_raisedDesktop = false;

bool g_muted     = false;
bool g_autoMuted = false;
bool g_pauseFullscreen  = false;
bool g_pauseDisplayOff  = false;
bool g_pauseSessionLock = false;
bool g_autoPaused       = false;
bool g_fillScreen = true;

HINSTANCE g_hInstance      = nullptr;
HWND      g_controllerHwnd = nullptr;

MultiMonitorMode g_multiMonitorMode = MultiMonitorMode::Span;
std::vector<std::wstring> g_enabledMonitors;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_ERASEBKGND:
		// The media engine paints the entire client area; skip background erase to avoid a white flash.
		return 1;

	case WM_DESTROY:
	{
		// Unregister WTS session notifications before tearing the window down.
		// RegisterPowerSettingNotification handles are released in main.cpp
		// after the message loop exits.
		WTSUnRegisterSessionNotification(hwnd);

		ShutdownPlayer();

		// Tear down the renderer windows once their engines have stopped
		// presenting (ShutdownPlayer shuts the engines first).
		DestroyRenderWindows();

		// Force the desktop layer to repaint where our child windows used to be,
		// otherwise stale pixels stay on screen.
		HWND repaintTarget = g_raisedDesktop ? g_progman : g_workerw;
		if (repaintTarget && IsWindow(repaintTarget))
		{
			InvalidateRect(repaintTarget, nullptr, TRUE);
			UpdateWindow(repaintTarget);
		}

		PostQuitMessage(0);
		return 0;
	}

	case WM_TIMER:
	{
		if (wParam == TIMER_ID_PRESENCE)
		{
			RecheckPresence();
			return 0;
		}
		if (wParam == TIMER_ID_DISPLAY)
		{
			// Debounced display-change rebuild. A monitor hot-plug or resolution
			// change can recreate the wallpaper WorkerW, so re-hook the desktop
			// before recomputing the layout and rebuilding the renderers.
			KillTimer(hwnd, TIMER_ID_DISPLAY);
			GetWorkerW();
			RebuildWallpaper();
			return 0;
		}
		break;
	}

	case WM_DISPLAYCHANGE:
	{
		// Coalesce the burst of notifications Windows sends while displays are
		// reconfigured into a single rebuild a short moment later.
		SetTimer(hwnd, TIMER_ID_DISPLAY, 500, nullptr);
		return 0;
	}

	case WM_POWERBROADCAST:
	{
		if (wParam == PBT_POWERSETTINGCHANGE && lParam)
		{
			const POWERBROADCAST_SETTING* setting =
				reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
			if (setting->DataLength >= sizeof(DWORD))
			{
				const DWORD data = *reinterpret_cast<const DWORD*>(setting->Data);
				// GUID_CONSOLE_DISPLAY_STATE: 0 off, 1 on, 2 dimmed.
				// GUID_MONITOR_POWER_ON:      0 off, 1 on.
				// Treat anything other than "on" as off — dimmed monitors
				// won't reliably show wallpaper anyway.
				if (IsEqualGUID(setting->PowerSetting, GUID_CONSOLE_DISPLAY_STATE)
					|| IsEqualGUID(setting->PowerSetting, GUID_MONITOR_POWER_ON))
				{
					const bool off = (data != 1);
					if (off != g_pauseDisplayOff)
					{
						g_pauseDisplayOff = off;
						ApplyEffectivePause();
					}
				}
			}
			return TRUE;
		}
		if (wParam == PBT_APMSUSPEND)
		{
			if (!g_pauseDisplayOff)
			{
				g_pauseDisplayOff = true;
				ApplyEffectivePause();
			}
			return TRUE;
		}
		if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND)
		{
			// On resume, also re-check fullscreen / display state in case
			// the GUID_CONSOLE_DISPLAY_STATE event ordering puts us out of sync.
			g_pauseDisplayOff = false;
			ApplyEffectivePause();
			RecheckPresence();
			return TRUE;
		}
		break;
	}

	case WM_WTSSESSION_CHANGE:
	{
		switch (wParam)
		{
		case WTS_SESSION_LOCK:
		case WTS_CONSOLE_DISCONNECT:
		case WTS_REMOTE_DISCONNECT:
		case WTS_SESSION_LOGOFF:
			if (!g_pauseSessionLock)
			{
				g_pauseSessionLock = true;
				ApplyEffectivePause();
			}
			return 0;
		case WTS_SESSION_UNLOCK:
		case WTS_CONSOLE_CONNECT:
		case WTS_REMOTE_CONNECT:
		case WTS_SESSION_LOGON:
			if (g_pauseSessionLock)
			{
				g_pauseSessionLock = false;
				ApplyEffectivePause();
			}
			RecheckPresence();
			return 0;
		}
		return 0;
	}

	case WM_TRAYICON:
	{
		if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP)
		{
			POINT pt;
			GetCursorPos(&pt);

			const bool autostart = IsAutostartEnabled();

			// Snapshot the monitors now; TrackPopupMenu is modal, so this list
			// stays valid for the per-monitor command handling below.
			const std::vector<MonitorEntry> monitors = EnumerateMonitors();

			// "Multiple monitors" submenu: layout-mode radio group + a checkable
			// toggle per connected monitor. The submenu HMENU is owned by `menu`
			// (DestroyMenu recurses), so it needs no separate RAII handle.
			HMENU mmMenu = CreatePopupMenu();
			AppendMenu(mmMenu, MF_STRING, ID_TRAY_MM_SPAN, L"Span across all");
			AppendMenu(mmMenu, MF_STRING, ID_TRAY_MM_MIRROR, L"Mirror on each");
			AppendMenu(mmMenu, MF_STRING, ID_TRAY_MM_PRIMARY, L"Primary monitor only");
			if (g_multiMonitorMode != MultiMonitorMode::Specific)
			{
				const UINT activePos =
					g_multiMonitorMode == MultiMonitorMode::Mirror  ? 1 :
					g_multiMonitorMode == MultiMonitorMode::Primary ? 2 : 0;
				CheckMenuRadioItem(mmMenu, 0, 2, activePos, MF_BYPOSITION);
			}
			AppendMenu(mmMenu, MF_SEPARATOR, 0, nullptr);
			for (size_t i = 0; i < monitors.size(); ++i)
			{
				const bool on =
					g_multiMonitorMode == MultiMonitorMode::Specific &&
					std::find(g_enabledMonitors.begin(), g_enabledMonitors.end(),
						monitors[i].device) != g_enabledMonitors.end();
				AppendMenu(
					mmMenu,
					MF_STRING | (on ? MF_CHECKED : MF_UNCHECKED),
					ID_TRAY_MM_MONITOR_BASE + static_cast<UINT>(i),
					monitors[i].label.c_str());
			}

			UniqueHMenu menu(CreatePopupMenu());
			AppendMenu(menu.get(), MF_STRING | (g_muted ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_MUTE, L"Mute");
			AppendMenu(menu.get(), MF_STRING | (g_fillScreen ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_FILL_SCREEN, L"Fill screen (crop)");
			AppendMenu(menu.get(), MF_POPUP, reinterpret_cast<UINT_PTR>(mmMenu), L"Multiple monitors");
			AppendMenu(menu.get(), MF_STRING | (autostart ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_AUTOSTART, L"Launch at startup");
			AppendMenu(menu.get(), MF_STRING, ID_TRAY_CHANGE_WALLPAPER, L"Change Wallpaper");
			AppendMenu(menu.get(), MF_STRING, ID_TRAY_RESTART, L"Restart video");
			AppendMenu(menu.get(), MF_STRING, ID_TRAY_EXIT, L"Exit");

			SetForegroundWindow(hwnd);

			int cmd = TrackPopupMenu(
				menu.get(),
				TPM_RETURNCMD | TPM_NONOTIFY,
				pt.x,
				pt.y,
				0,
				hwnd,
				nullptr);

			if (cmd == ID_TRAY_EXIT)
			{
				DestroyWindow(hwnd);
				return 0;
			}
			if (cmd == ID_TRAY_MUTE)
			{
				g_muted = !g_muted;
				ApplyEffectiveMute();
				return 0;
			}
			if (cmd == ID_TRAY_FILL_SCREEN)
			{
				g_fillScreen = !g_fillScreen;
				ApplyAspectMode();
				SaveFillScreen(g_fillScreen);
				return 0;
			}
			if (cmd == ID_TRAY_MM_SPAN || cmd == ID_TRAY_MM_MIRROR || cmd == ID_TRAY_MM_PRIMARY)
			{
				const MultiMonitorMode newMode =
					cmd == ID_TRAY_MM_MIRROR  ? MultiMonitorMode::Mirror  :
					cmd == ID_TRAY_MM_PRIMARY ? MultiMonitorMode::Primary :
												MultiMonitorMode::Span;
				if (newMode != g_multiMonitorMode)
				{
					g_multiMonitorMode = newMode;
					SaveMultiMonitorMode(g_multiMonitorMode);
					RebuildWallpaper();
				}
				return 0;
			}
			if (cmd >= static_cast<int>(ID_TRAY_MM_MONITOR_BASE) &&
				cmd < static_cast<int>(ID_TRAY_MM_MONITOR_BASE + monitors.size()))
			{
				const std::wstring& device =
					monitors[cmd - static_cast<int>(ID_TRAY_MM_MONITOR_BASE)].device;
				auto it = std::find(g_enabledMonitors.begin(), g_enabledMonitors.end(), device);
				if (it != g_enabledMonitors.end())
				{
					g_enabledMonitors.erase(it);
				}
				else
				{
					g_enabledMonitors.push_back(device);
				}
				// Toggling a specific monitor implies Specific mode.
				g_multiMonitorMode = MultiMonitorMode::Specific;
				SaveMultiMonitorMode(g_multiMonitorMode);
				SaveEnabledMonitors(g_enabledMonitors);
				RebuildWallpaper();
				return 0;
			}
			if (cmd == ID_TRAY_AUTOSTART)
			{
				const bool ok = autostart ? DisableAutostart() : EnableAutostart();
				if (!ok)
				{
					MessageBoxW(hwnd,
						autostart ? L"Failed to disable launch at startup"
								  : L"Failed to enable launch at startup",
						L"Error", MB_ICONERROR);
				}
				return 0;
			}
			if (cmd == ID_TRAY_CHANGE_WALLPAPER)
			{
				HRESULT hr = ChangeWallpaper();
				if (FAILED(hr))
				{
					MessageBoxW(hwnd, L"Failed to change wallpaper", L"Error", MB_ICONERROR);
				}
				return 0;
			}
			if (cmd == ID_TRAY_RESTART)
			{
				RestartWallpaper();
				return 0;
			}
		}
		return 0;
	}
	}

	return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
