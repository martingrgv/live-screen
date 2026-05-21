#pragma once

#include <windows.h>
#include <string>

// Converts a NUL-terminated wide string to UTF-8.
// Returns S_OK on success, or an HRESULT derived from GetLastError() on failure.
HRESULT WideToUtf8(PCWSTR input, std::string& out);
