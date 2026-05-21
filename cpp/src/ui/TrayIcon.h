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

#include <string>

// Shows an IFileOpenDialog filtered to common video extensions.
// On success returns S_OK and stores the chosen file's full path in `out`.
// Returns HRESULT_FROM_WIN32(ERROR_CANCELLED) when the user cancels.
HRESULT PromptForWallpaperPath(HWND owner, std::wstring& out);

// Prompts the user for a media file, starts playing it, and persists the choice.
HRESULT ChangeWallpaper();
