#include "win/DebugLog.h"

#include <strsafe.h>

void DebugWindowState(const wchar_t* name, HWND hwnd, HWND expectedParent)
{
	wchar_t className[256] = {};
	wchar_t parentClassName[256] = {};
	wchar_t buffer[1024] = {};
	RECT rect = {};

	HWND parent = hwnd ? GetParent(hwnd) : nullptr;
	LONG_PTR style = hwnd ? GetWindowLongPtr(hwnd, GWL_STYLE) : 0;
	LONG_PTR exStyle = hwnd ? GetWindowLongPtr(hwnd, GWL_EXSTYLE) : 0;
	BOOL visible = hwnd ? IsWindowVisible(hwnd) : FALSE;
	BOOL childOfExpectedParent = hwnd && expectedParent ? IsChild(expectedParent, hwnd) : FALSE;
	BOOL parentMatches = parent == expectedParent;

	if (hwnd)
	{
		GetClassNameW(hwnd, className, ARRAYSIZE(className));
		GetWindowRect(hwnd, &rect);
	}

	if (parent)
	{
		GetClassNameW(parent, parentClassName, ARRAYSIZE(parentClassName));
	}

	StringCchPrintfW(
		buffer,
		ARRAYSIZE(buffer),
		L"%s: hwnd=%p class=%s parent=%p parentClass=%s expectedParent=%p parentMatches=%d isChild=%d visible=%d style=0x%Ix exStyle=0x%Ix rect=(%ld,%ld)-(%ld,%ld)\r\n",
		name,
		hwnd,
		className,
		parent,
		parentClassName,
		expectedParent,
		parentMatches,
		childOfExpectedParent,
		visible,
		style,
		exStyle,
		rect.left,
		rect.top,
		rect.right,
		rect.bottom);

	OutputDebugStringW(buffer);
}

void VlcLog(void* /*data*/, int level, const libvlc_log_t* /*ctx*/, const char* fmt, va_list args)
{
	char message[1024] = {};
	vsnprintf_s(message, _TRUNCATE, fmt, args);

	wchar_t wide[1100] = {};
	StringCchPrintfW(wide, ARRAYSIZE(wide), L"[libvlc:%d] %S\r\n", level, message);
	OutputDebugStringW(wide);
}
