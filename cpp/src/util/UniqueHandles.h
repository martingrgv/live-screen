#pragma once

#include <windows.h>
#include <objbase.h>
#include <memory>
#include <type_traits>

// RAII wrappers for misc Win32/COM resources.

// Frees a buffer allocated with CoTaskMemAlloc (e.g. PWSTR returned by
// IShellItem::GetDisplayName).
struct CoTaskMemDeleter
{
	void operator()(void* p) const noexcept { CoTaskMemFree(p); }
};

using UniqueCoTaskMemString = std::unique_ptr<WCHAR, CoTaskMemDeleter>;

// Owns an HMENU returned by CreatePopupMenu / CreateMenu.
struct HMenuDeleter
{
	using pointer = HMENU;
	void operator()(HMENU h) const noexcept
	{
		if (h) DestroyMenu(h);
	}
};

using UniqueHMenu = std::unique_ptr<HMENU, HMenuDeleter>;

// Owns a Win32 HANDLE returned by CreateFile / CreateEvent / etc.
// INVALID_HANDLE_VALUE and nullptr both denote "no handle"; the deleter
// skips CloseHandle on either to match the kernel32 contract.
struct HandleDeleter
{
	using pointer = HANDLE;
	void operator()(HANDLE h) const noexcept
	{
		if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
	}
};

using UniqueHandle = std::unique_ptr<HANDLE, HandleDeleter>;
