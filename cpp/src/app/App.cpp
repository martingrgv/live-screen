#include "app/App.h"

#include "app/OcclusionWatcher.h"
#include "app/Settings.h"
#include "media/VlcPlayer.h"
#include "ui/TrayIcon.h"
#include "util/Autostart.h"
#include "util/UniqueHandles.h"

#include <wtsapi32.h>

// -----------------------------------------------------------------------------
// Global definitions (declared extern in App.h).
// -----------------------------------------------------------------------------
HWND g_workerw       = nullptr;
HWND g_shellViewHost = nullptr;
HWND g_shellDefView  = nullptr;
HWND g_progman       = nullptr;
bool g_raisedDesktop = false;

VlcInstance        g_vlc;
VlcMediaListPlayer g_listPlayer;
VlcMediaList       g_mediaList;
VlcMediaPlayer     g_player;
VlcMedia           g_media;

bool g_muted     = false;
bool g_autoMuted = false;
bool g_pauseFullscreen  = false;
bool g_pauseDisplayOff  = false;
bool g_pauseSessionLock = false;
bool g_autoPaused       = false;
bool g_fillScreen = true;

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_ERASEBKGND:
		// VLC paints the entire client area; skip background erase to avoid a white flash.
		return 1;

	case WM_DESTROY:
	{
		// Unregister WTS session notifications before tearing the window down.
		// RegisterPowerSettingNotification handles are released in main.cpp
		// after the message loop exits.
		WTSUnRegisterSessionNotification(hwnd);

		ShutdownVlc();

		// Force the desktop layer to repaint where our child window used to be,
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
		break;
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

			UniqueHMenu menu(CreatePopupMenu());
			AppendMenu(menu.get(), MF_STRING | (g_muted ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_MUTE, L"Mute");
			AppendMenu(menu.get(), MF_STRING | (g_fillScreen ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_FILL_SCREEN, L"Fill screen (crop)");
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
				ApplyAspectMode(hwnd);
				SaveFillScreen(g_fillScreen);
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
