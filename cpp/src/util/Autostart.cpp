#include "util/Autostart.h"

#include <windows.h>

#include <string>

namespace
{
	constexpr PCWSTR kRunKeyPath  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
	constexpr PCWSTR kRunValueName = L"LiveScreen";

	struct RegKey
	{
		HKEY h = nullptr;
		~RegKey() { if (h) RegCloseKey(h); }
	};

	bool GetCurrentExePath(std::wstring& out)
	{
		// MAX_PATH is not always enough on modern Windows; grow on demand.
		DWORD cap = MAX_PATH;
		for (;;)
		{
			std::wstring buf(cap, L'\0');
			DWORD len = GetModuleFileNameW(nullptr, buf.data(), cap);
			if (len == 0)
			{
				return false;
			}
			if (len < cap)
			{
				buf.resize(len);
				out = std::move(buf);
				return true;
			}
			// Buffer was too small (truncated). Grow and retry.
			cap *= 2;
			if (cap > 32768) return false;
		}
	}

	std::wstring QuotePath(const std::wstring& path)
	{
		std::wstring s;
		s.reserve(path.size() + 2);
		s.push_back(L'"');
		s.append(path);
		s.push_back(L'"');
		return s;
	}

	// Strip a single pair of surrounding quotes, if present.
	std::wstring Unquote(std::wstring s)
	{
		if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
		{
			s.erase(s.size() - 1, 1);
			s.erase(0, 1);
		}
		return s;
	}
}

bool IsAutostartEnabled()
{
	RegKey key;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key.h) != ERROR_SUCCESS)
	{
		return false;
	}

	DWORD type = 0;
	DWORD bytes = 0;
	LONG rc = RegQueryValueExW(key.h, kRunValueName, nullptr, &type, nullptr, &bytes);
	if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes == 0)
	{
		return false;
	}

	std::wstring value(bytes / sizeof(wchar_t), L'\0');
	rc = RegQueryValueExW(key.h, kRunValueName, nullptr, &type,
		reinterpret_cast<LPBYTE>(value.data()), &bytes);
	if (rc != ERROR_SUCCESS)
	{
		return false;
	}
	// Trim trailing null terminators that RegQueryValueExW may include.
	while (!value.empty() && value.back() == L'\0')
	{
		value.pop_back();
	}

	std::wstring exe;
	if (!GetCurrentExePath(exe))
	{
		return false;
	}

	std::wstring stored = Unquote(std::move(value));
	return CompareStringOrdinal(stored.c_str(), -1, exe.c_str(), -1, TRUE) == CSTR_EQUAL;
}

bool EnableAutostart()
{
	std::wstring exe;
	if (!GetCurrentExePath(exe))
	{
		return false;
	}
	std::wstring quoted = QuotePath(exe);

	RegKey key;
	if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr,
			REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key.h, nullptr) != ERROR_SUCCESS)
	{
		return false;
	}

	const DWORD bytes = static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t));
	return RegSetValueExW(key.h, kRunValueName, 0, REG_SZ,
		reinterpret_cast<const BYTE*>(quoted.c_str()), bytes) == ERROR_SUCCESS;
}

bool DisableAutostart()
{
	RegKey key;
	if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key.h) != ERROR_SUCCESS)
	{
		// If the key itself doesn't exist, there's nothing to disable.
		return true;
	}

	LONG rc = RegDeleteValueW(key.h, kRunValueName);
	return rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND;
}
