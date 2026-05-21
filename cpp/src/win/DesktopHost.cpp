#include "win/DesktopHost.h"

#include "app/App.h"
#include "win/DebugLog.h"

namespace
{
	BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM /*lParam*/)
	{
		// Classic case: walk top-level windows; whoever owns SHELLDLL_DefView is the icon
		// host, the wallpaper WorkerW is the next top-level WorkerW after it in Z-order.
		HWND shellView = FindWindowEx(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
		if (shellView != nullptr)
		{
			g_shellViewHost = hwnd;
			g_shellDefView = shellView;
			HWND candidate = FindWindowEx(nullptr, hwnd, L"WorkerW", nullptr);
			if (candidate != nullptr)
			{
				g_workerw = candidate;
			}
		}

		return TRUE;
	}

	BOOL CALLBACK DebugDesktopWindowsProc(HWND hwnd, LPARAM /*lParam*/)
	{
		wchar_t className[256] = {};
		GetClassNameW(hwnd, className, ARRAYSIZE(className));

		if (lstrcmpW(className, L"Progman") == 0 || lstrcmpW(className, L"WorkerW") == 0)
		{
			DebugWindowState(className, hwnd, nullptr);

			HWND shellView = FindWindowEx(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
			if (shellView)
			{
				DebugWindowState(L"  SHELLDLL_DefView", shellView, hwnd);
			}
		}

		return TRUE;
	}
}

HWND GetWorkerW()
{
	g_workerw = nullptr;
	g_shellViewHost = nullptr;
	g_shellDefView = nullptr;
	g_raisedDesktop = false;

	g_progman = FindWindow(L"Progman", nullptr);
	if (!g_progman)
	{
		return nullptr;
	}

	// Detect the modern Windows 11 "raised desktop with layered ShellView" layout.
	// In that case Progman is created with WS_EX_NOREDIRECTIONBITMAP, the SHELLDLL_DefView
	// is a WS_EX_LAYERED child of Progman, and the wallpaper WorkerW is itself a CHILD of
	// Progman (not a top-level sibling). EnumWindows will not find it.
	LONG_PTR progmanExStyle = GetWindowLongPtr(g_progman, GWL_EXSTYLE);
	g_raisedDesktop = (progmanExStyle & WS_EX_NOREDIRECTIONBITMAP) != 0;

	DWORD_PTR result = 0;

	// Tell Progman to spawn the wallpaper WorkerW. The (0xD, 0x1) variant is what works
	// on modern Windows 10/11.
	SendMessageTimeout(
		g_progman,
		0x052C,
		0xD,
		0x1,
		SMTO_NORMAL,
		1000,
		&result);

	// Classic detection (works on Windows 10 and pre-raised-desktop Windows 11).
	EnumWindows(EnumWindowsProc, 0);
	EnumWindows(DebugDesktopWindowsProc, 0);
	DebugWindowState(L"Progman", g_progman, nullptr);
	DebugWindowState(L"Shell view host", g_shellViewHost, nullptr);
	DebugWindowState(L"Selected WorkerW (classic)", g_workerw, nullptr);

	if (g_raisedDesktop)
	{
		// Raised-desktop layout: SHELLDLL_DefView and the wallpaper WorkerW are both
		// direct children of Progman.
		g_shellDefView = FindWindowEx(g_progman, nullptr, L"SHELLDLL_DefView", nullptr);
		g_workerw = FindWindowEx(g_progman, nullptr, L"WorkerW", nullptr);
		DebugWindowState(L"Selected WorkerW (raised)", g_workerw, nullptr);
		DebugWindowState(L"SHELLDLL_DefView (raised)", g_shellDefView, nullptr);
	}

	return g_workerw;
}
