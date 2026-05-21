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
