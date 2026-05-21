#pragma once

#include <windows.h>
#include <vlc/vlc.h>

// Dumps key Win32 metadata about a window to OutputDebugString.
void DebugWindowState(const wchar_t* name, HWND hwnd, HWND expectedParent);

// libvlc -> OutputDebugString bridge.
void VlcLog(void* data, int level, const libvlc_log_t* ctx, const char* fmt, va_list args);
