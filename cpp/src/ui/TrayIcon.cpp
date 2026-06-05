#include "ui/TrayIcon.h"

#include "app/Settings.h"
#include "media/MediaPlayer.h"
#include "util/UniqueHandles.h"

#include "res/Resource.h"

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

	// Load the small (tray-sized) variant of the app icon from our own module's
	// resources. LR_SHARED hands back a cached HICON that must not be
	// DestroyIcon-ed — matches this class's existing no-cleanup contract.
	const int cx = GetSystemMetrics(SM_CXSMICON);
	const int cy = GetSystemMetrics(SM_CYSMICON);
	nid_.hIcon = static_cast<HICON>(LoadImageW(
		GetModuleHandleW(nullptr),
		MAKEINTRESOURCEW(IDI_APP_ICON),
		IMAGE_ICON,
		cx,
		cy,
		LR_DEFAULTCOLOR | LR_SHARED));
	if (nid_.hIcon == nullptr)
	{
		nid_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
	}

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

HRESULT PromptForWallpaperPath(HWND owner, std::wstring& out)
{
	out.clear();

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

	const COMDLG_FILTERSPEC filters[] =
	{
		{ L"Video files", L"*.mp4;*.mkv;*.webm;*.mov;*.avi;*.m4v;*.wmv" },
		{ L"All files",   L"*.*" },
	};
	dialog->SetFileTypes(ARRAYSIZE(filters), filters);
	dialog->SetTitle(L"Choose a wallpaper video");

	hr = dialog->Show(owner);
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

	out = path.get();
	return S_OK;
}

HRESULT ChangeWallpaper()
{
	std::wstring path;
	HRESULT hr = PromptForWallpaperPath(nullptr, path);
	if (FAILED(hr))
	{
		return hr;
	}

	if (HRESULT playHr = PlayWallpaperPath(path.c_str()); FAILED(playHr))
	{
		return playHr;
	}

	// Persist after a successful swap so a broken file never overwrites the
	// last known-good wallpaper in config.ini.
	return SaveWallpaperPath(path.c_str());
}
