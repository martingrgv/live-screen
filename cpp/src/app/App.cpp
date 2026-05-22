#include "app/App.h"

#include "media/VlcPlayer.h"
#include "ui/TrayIcon.h"
#include "util/Autostart.h"
#include "util/UniqueHandles.h"

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

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_ERASEBKGND:
		// VLC paints the entire client area; skip background erase to avoid a white flash.
		return 1;

	case WM_DESTROY:
	{
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

	case WM_TRAYICON:
	{
		if (lParam == WM_LBUTTONUP || lParam == WM_RBUTTONUP)
		{
			POINT pt;
			GetCursorPos(&pt);

			const bool autostart = IsAutostartEnabled();

			UniqueHMenu menu(CreatePopupMenu());
			AppendMenu(menu.get(), MF_STRING | (g_muted ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_MUTE, L"Mute");
			AppendMenu(menu.get(), MF_STRING | (autostart ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_AUTOSTART, L"Launch at startup");
			AppendMenu(menu.get(), MF_STRING, ID_TRAY_CHANGE_WALLPAPER, L"Change Wallpaper");
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
		}
		return 0;
	}
	}

	return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
