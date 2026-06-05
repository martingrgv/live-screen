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
	constexpr PCWSTR kSection      = L"Wallpaper";
	constexpr PCWSTR kKeyPath      = L"Path";
	constexpr PCWSTR kDisplaySection = L"Display";
	constexpr PCWSTR kKeyFillScreen  = L"FillScreen";
	constexpr PCWSTR kKeyMultiMonitorMode = L"MultiMonitorMode";
	constexpr PCWSTR kKeyMonitors         = L"Monitors";

	// Serialized tokens for MultiMonitorMode. Stored as text so the ini stays
	// human-readable and stable if the enum's underlying values ever change.
	constexpr PCWSTR kModeSpan     = L"span";
	constexpr PCWSTR kModeMirror   = L"mirror";
	constexpr PCWSTR kModePrimary  = L"primary";
	constexpr PCWSTR kModeSpecific = L"specific";

	// Enabled-monitor device names are joined with '|'. Device names
	// (\\.\DISPLAYn) never contain that character, so it is a safe delimiter.
	constexpr wchar_t kMonitorDelimiter = L'|';

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

HRESULT LoadFillScreen(bool& out)
{
	std::wstring path;
	if (HRESULT hr = GetConfigPath(path); FAILED(hr))
	{
		return hr;
	}

	if (!PathFileExistsW(path.c_str()))
	{
		return S_FALSE;
	}

	// Sentinel UINT_MAX lets us distinguish "not present" from an explicit 0.
	constexpr UINT kSentinel = 0xFFFFFFFFu;
	UINT value = GetPrivateProfileIntW(
		kDisplaySection,
		kKeyFillScreen,
		kSentinel,
		path.c_str());

	if (value == kSentinel)
	{
		return S_FALSE;
	}

	out = (value != 0);
	return S_OK;
}

HRESULT SaveFillScreen(bool value)
{
	std::wstring configPath;
	if (HRESULT hr = GetConfigPath(configPath); FAILED(hr))
	{
		return hr;
	}

	if (HRESULT hr = EnsureUnicodeIniFile(configPath.c_str()); FAILED(hr))
	{
		return hr;
	}

	PCWSTR text = value ? L"1" : L"0";
	if (!WritePrivateProfileStringW(kDisplaySection, kKeyFillScreen, text, configPath.c_str()))
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}
	return S_OK;
}

HRESULT LoadMultiMonitorMode(MultiMonitorMode& out)
{
	std::wstring path;
	if (HRESULT hr = GetConfigPath(path); FAILED(hr))
	{
		return hr;
	}

	if (!PathFileExistsW(path.c_str()))
	{
		return S_FALSE;
	}

	wchar_t buf[32] = {};
	DWORD copied = GetPrivateProfileStringW(
		kDisplaySection,
		kKeyMultiMonitorMode,
		L"",
		buf,
		ARRAYSIZE(buf),
		path.c_str());

	if (copied == 0)
	{
		return S_FALSE;
	}

	if (lstrcmpiW(buf, kModeMirror) == 0)
	{
		out = MultiMonitorMode::Mirror;
	}
	else if (lstrcmpiW(buf, kModePrimary) == 0)
	{
		out = MultiMonitorMode::Primary;
	}
	else if (lstrcmpiW(buf, kModeSpecific) == 0)
	{
		out = MultiMonitorMode::Specific;
	}
	else
	{
		out = MultiMonitorMode::Span;
	}
	return S_OK;
}

HRESULT SaveMultiMonitorMode(MultiMonitorMode value)
{
	std::wstring configPath;
	if (HRESULT hr = GetConfigPath(configPath); FAILED(hr))
	{
		return hr;
	}

	if (HRESULT hr = EnsureUnicodeIniFile(configPath.c_str()); FAILED(hr))
	{
		return hr;
	}

	PCWSTR text = kModeSpan;
	switch (value)
	{
	case MultiMonitorMode::Mirror:   text = kModeMirror;   break;
	case MultiMonitorMode::Primary:  text = kModePrimary;  break;
	case MultiMonitorMode::Specific: text = kModeSpecific; break;
	case MultiMonitorMode::Span:
	default:                         text = kModeSpan;     break;
	}

	if (!WritePrivateProfileStringW(kDisplaySection, kKeyMultiMonitorMode, text, configPath.c_str()))
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}
	return S_OK;
}

HRESULT LoadEnabledMonitors(std::vector<std::wstring>& out)
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

	// Device-name lists are short; a fixed buffer comfortably holds the
	// realistic maximum (a few dozen \\.\DISPLAYn tokens).
	std::wstring buf(1024, L'\0');
	DWORD copied = GetPrivateProfileStringW(
		kDisplaySection,
		kKeyMonitors,
		L"",
		buf.data(),
		static_cast<DWORD>(buf.size()),
		path.c_str());
	buf.resize(copied);

	if (buf.empty())
	{
		return S_FALSE;
	}

	size_t start = 0;
	while (start <= buf.size())
	{
		size_t end = buf.find(kMonitorDelimiter, start);
		std::wstring token = buf.substr(
			start,
			(end == std::wstring::npos) ? std::wstring::npos : end - start);
		if (!token.empty())
		{
			out.push_back(token);
		}
		if (end == std::wstring::npos)
		{
			break;
		}
		start = end + 1;
	}
	return S_OK;
}

HRESULT SaveEnabledMonitors(const std::vector<std::wstring>& value)
{
	std::wstring configPath;
	if (HRESULT hr = GetConfigPath(configPath); FAILED(hr))
	{
		return hr;
	}

	if (HRESULT hr = EnsureUnicodeIniFile(configPath.c_str()); FAILED(hr))
	{
		return hr;
	}

	std::wstring joined;
	for (size_t i = 0; i < value.size(); ++i)
	{
		if (i != 0)
		{
			joined += kMonitorDelimiter;
		}
		joined += value[i];
	}

	if (!WritePrivateProfileStringW(kDisplaySection, kKeyMonitors, joined.c_str(), configPath.c_str()))
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}
	return S_OK;
}
