#include "util/Utf8.h"

HRESULT WideToUtf8(PCWSTR input, std::string& out)
{
	if (!input)
	{
		return E_INVALIDARG;
	}

	int utf8Length = WideCharToMultiByte(CP_UTF8, 0, input, -1, nullptr, 0, nullptr, nullptr);
	if (utf8Length <= 0)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	out.assign(utf8Length, '\0');
	if (WideCharToMultiByte(CP_UTF8, 0, input, -1, out.data(), utf8Length, nullptr, nullptr) <= 0)
	{
		return HRESULT_FROM_WIN32(GetLastError());
	}

	return S_OK;
}
