#include "ui/TrayIcon.h"

#include "media/VlcPlayer.h"
#include "util/UniqueHandles.h"

#include <shobjidl.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

TrayIcon::TrayIcon(HWND hwnd, UINT callbackMessage, PCWSTR tip)
{
	nid_.cbSize           = sizeof(NOTIFYICONDATA);
	nid_.hWnd             = hwnd;
	nid_.uID              = 1;
	nid_.uFlags           = NIF_MESSAGE | NIF_ICON | NIF_TIP;
	nid_.uCallbackMessage = callbackMessage;
	nid_.hIcon            = LoadIcon(nullptr, IDI_APPLICATION);

	wcscpy_s(nid_.szTip, tip);

	added_ = Shell_NotifyIcon(NIM_ADD, &nid_) != FALSE;
}

TrayIcon::~TrayIcon()
{
	if (added_)
	{
		Shell_NotifyIcon(NIM_DELETE, &nid_);
	}
}

HRESULT ChangeWallpaper()
{
	ComPtr<IFileOpenDialog> dialog;
	HRESULT hr = CoCreateInstance(
		__uuidof(FileOpenDialog),
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&dialog));
	if (FAILED(hr))
	{
		return hr;
	}

	hr = dialog->Show(nullptr);
	if (FAILED(hr))
	{
		return hr;
	}

	ComPtr<IShellItem> item;
	hr = dialog->GetResult(&item);
	if (FAILED(hr))
	{
		return hr;
	}

	PWSTR rawPath = nullptr;
	hr = item->GetDisplayName(SIGDN_FILESYSPATH, &rawPath);
	UniqueCoTaskMemString path(rawPath);
	if (FAILED(hr))
	{
		return hr;
	}

	return PlayWallpaperPath(path.get());
}
