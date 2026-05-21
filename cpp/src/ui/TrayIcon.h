#pragma once

#include <windows.h>
#include <shellapi.h>

// RAII registration of a notification-area icon.
//
// ctor: Shell_NotifyIcon(NIM_ADD, ...)
// dtor: Shell_NotifyIcon(NIM_DELETE, ...) — guarantees the icon is removed even
//       on abnormal exits where WM_DESTROY was not processed.
class TrayIcon
{
public:
	TrayIcon(HWND hwnd, UINT callbackMessage, PCWSTR tip);
	~TrayIcon();

	TrayIcon(const TrayIcon&) = delete;
	TrayIcon& operator=(const TrayIcon&) = delete;

private:
	NOTIFYICONDATA nid_{};
	bool           added_ = false;
};

// Prompts the user for a media file and starts playing it.
HRESULT ChangeWallpaper();
