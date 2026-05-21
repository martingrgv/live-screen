#include "app/Settings.h"

#include <shlobj.h>
#include <shlwapi.h>

#include "util/UniqueHandles.h"

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Ole32.lib")

namespace
{
	constexpr PCWSTR kAppFolder   = L"LiveScreen";
	constexpr PCWSTR kConfigFile  = L"config.ini";
	constexpr PCWSTR kSection     = L"Wallpaper";
	constexpr PCWSTR kKeyPath     = L"Path";

	// Resolves %APPDATA%\LiveScreen, creating the directory if it doesn't exist.
	HRESULT GetConfigDir(std::wstring& out)
	{
		PWSTR rawAppData = nullptr;
		HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &rawAppData);
		UniqueCoTaskMemString appData(rawAppData);
		if (FAILED(hr))
		{
			return hr;
		}

		std::wstring dir = appData.get();
		dir += L'\\';
		dir += kAppFolder;

		int err = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
		if (err != ERROR_SUCCESS && err != ERROR_ALREADY_EXISTS && err != ERROR_FILE_EXISTS)
		{
			return HRESULT_FROM_WIN32(err);
		}

		out = std::move(dir);
		return S_OK;
	}

	HRESULT GetConfigPath(std::wstring& out)
	{
		std::wstring dir;
		if (HRESULT hr = GetConfigDir(dir); FAILED(hr))
		{
			return hr;
		}
		dir += L'\\';
		dir += kConfigFile;
		out = std::move(dir);
		return S_OK;
	}

	// WritePrivateProfileStringW only emits UTF-16 when the target file already
	// starts with a UTF-16 LE BOM. Without this, paths with non-ASCII characters
	// are mangled on save and unreadable on load.
	HRESULT EnsureUnicodeIniFile(PCWSTR path)
	{
		if (PathFileExistsW(path))
		{
			return S_OK;
		}

		HANDLE raw = CreateFileW(
			path,
			GENERIC_WRITE,
			FILE_SHARE_READ,
			nullptr,
			CREATE_NEW,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (raw == INVALID_HANDLE_VALUE)
		{
			DWORD err = GetLastError();
			// Race: another instance created it first; that's fine.
			if (err == ERROR_FILE_EXISTS)
			{
				return S_OK;
			}
			return HRESULT_FROM_WIN32(err);
		}
		UniqueHandle file(raw);

		const unsigned char bom[] = { 0xFF, 0xFE };
		DWORD written = 0;
		if (!WriteFile(file.get(), bom, sizeof(bom), &written, nullptr) || written != sizeof(bom))
		{
			return HRESULT_FROM_WIN32(GetLastError());
		}
		return S_OK;
	}
}

HRESULT LoadWallpaperPath(std::wstring& out)
{
	out.clear();

	std::wstring path;
	if (HRESULT hr = GetConfigPath(path); FAILED(hr))
	{
		return hr;
	}

	if (!PathFileExistsW(path.c_str()))
	{
		return S_FALSE;
	}

	// MAX_PATH is the documented limit for legacy Win32 APIs, but wallpaper
	// paths can exceed it on long-path-aware systems. Grow the buffer until
	// the value fits (GetPrivateProfileStringW returns size-1 when truncated).
	std::wstring buf(MAX_PATH, L'\0');
	for (;;)
	{
		DWORD copied = GetPrivateProfileStringW(
			kSection,
			kKeyPath,
			L"",
			buf.data(),
			static_cast<DWORD>(buf.size()),
			path.c_str());

		if (copied + 1 < buf.size())
		{
			buf.resize(copied);
			break;
		}
		if (buf.size() >= 32768)
		{
			// Hit Win32 path ceiling without a terminator; treat as corrupt.
			return E_FAIL;
		}
		buf.resize(buf.size() * 2, L'\0');
	}

	if (buf.empty())
	{
		return S_FALSE;
	}

	out = std::move(buf);
	return S_OK;
}

HRESULT SaveWallpaperPath(PCWSTR path)
{
	if (!path || !*path)
	{
		return E_INVALIDARG;
	}

	std::wstring configPath;
	if (HRESULT hr = GetConfigPath(configPath); FAILED(hr))
	{
		return hr;
	}

	if (HRESULT hr = EnsureUnicodeIniFile(configPath.c_str()); FAILED(hr))
	{
		return hr;
	}

	if (!WritePrivateProfileStringW(kSection, kKeyPath, path, configPath.c_str()))
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}
	return S_OK;
}
