// Live Screen - entry point.
//
// Top-level orchestration only:
//   * Win32 init (COM, DPI awareness, window class, window).
//   * Desktop hooking via DesktopHost.
//   * Tray icon registration via TrayIcon (RAII).
//   * libvlc lifecycle via VlcPlayer.
//   * Standard message loop.
//
// All real work lives in the modules under src/.

#include <windows.h>
#include <shellscalingapi.h>

#include "app/App.h"
#include "media/VlcPlayer.h"
#include "ui/TrayIcon.h"
#include "util/ComApartment.h"
#include "win/DesktopHost.h"

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "User32.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR /*pCmdLine*/, int /*nCmdShow*/)
{
	ComApartment com(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (!com.ok())
	{
		return 0;
	}

	// Match physical pixels so our window size aligns with WorkerW (which is in physical pixels).
	// Try Per-Monitor V2 first; fall back to system DPI awareness on older Windows.
	if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
	{
		SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
	}

	const wchar_t* CLASS_NAME = L"Live Screen Window Class";

	WNDCLASS wc = {};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = CLASS_NAME;
	// Black background avoids a white flash / leftover white area in letterboxed regions.
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);

	RegisterClass(&wc);

	HWND workerw = GetWorkerW();

	// Compute the full virtual-screen rect so the wallpaper covers all monitors.
	int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
	int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
	int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
	int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);

	if (workerw && !g_raisedDesktop)
	{
		// Classic layout: WorkerW is the parent, child coords are relative to it.
		RECT wr = {};
		GetClientRect(workerw, &wr);
		x = 0;
		y = 0;
		width = wr.right - wr.left;
		height = wr.bottom - wr.top;
	}

	// Choose styles:
	//   * Classic (parent = WorkerW)        -> WS_CHILD, parent passed to CreateWindowEx.
	//   * Raised desktop (parent = Progman) -> create top-level WS_EX_LAYERED first, then
	//                                          flip to WS_CHILD and SetParent (Lively pattern).
	//   * No desktop hooked at all          -> WS_POPUP fallback covering the primary monitor.
	DWORD style;
	DWORD exStyle;
	HWND createParent;
	// WS_CLIPCHILDREN | WS_CLIPSIBLINGS lets DWM elide drawing under sibling windows
	// (e.g. SHELLDLL_DefView icons) and avoids extra invalidation as the cursor passes
	// over them, which otherwise contributes to cursor lag while the video plays.
	const DWORD kClipStyles = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

	if (g_raisedDesktop)
	{
		style = WS_POPUP | kClipStyles; // becomes WS_CHILD after SetParent
		exStyle = WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
		createParent = nullptr;
	}
	else if (workerw)
	{
		style = WS_CHILD | WS_VISIBLE | kClipStyles;
		exStyle = WS_EX_NOACTIVATE;
		createParent = workerw;
	}
	else
	{
		// No desktop layer found: fall back to a popup over the primary monitor.
		MONITORINFO mi = {};
		mi.cbSize = sizeof(mi);
		HMONITOR hmon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
		if (GetMonitorInfo(hmon, &mi))
		{
			x = mi.rcMonitor.left;
			y = mi.rcMonitor.top;
			width = mi.rcMonitor.right - mi.rcMonitor.left;
			height = mi.rcMonitor.bottom - mi.rcMonitor.top;
		}
		style = WS_POPUP | WS_VISIBLE | kClipStyles;
		exStyle = WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
		createParent = nullptr;
	}

	HWND hwnd = CreateWindowEx(
		exStyle,
		CLASS_NAME,
		L"Live Screen - Renderer",
		style,
		x,
		y,
		width,
		height,
		createParent,
		nullptr,
		hInstance,
		nullptr);

	if (hwnd == nullptr)
	{
		return 0;
	}

	if (g_raisedDesktop)
	{
		// Microsoft guidance for "raised desktop with layered ShellView" (Win11):
		// our window must be a WS_EX_LAYERED child of Progman, z-ordered just below
		// SHELLDLL_DefView (icons) but above the wallpaper WorkerW.
		// Note: WS_EX_LAYERED must already be set BEFORE SetParent (set at create time).
		SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
		SetWindowLongPtr(hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE | kClipStyles);
		SetParent(hwnd, g_progman);

		if (g_shellDefView)
		{
			SetWindowPos(
				hwnd,
				g_shellDefView,
				0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		// Now size the child to cover the entire virtual screen (Progman client coords).
		SetWindowPos(
			hwnd,
			nullptr,
			GetSystemMetrics(SM_XVIRTUALSCREEN),
			GetSystemMetrics(SM_YVIRTUALSCREEN),
			GetSystemMetrics(SM_CXVIRTUALSCREEN),
			GetSystemMetrics(SM_CYVIRTUALSCREEN),
			SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
	}

	TrayIcon tray(hwnd, WM_TRAYICON, L"Live Screen");

	HRESULT hrVlc = InitVlc(
		hwnd,
		LR"(C:\Users\marti\Downloads\samurai_wallpaper.mp4)"); // TODO: make this dynamic
	if (FAILED(hrVlc))
	{
		return 0;
	}

	ShowWindow(hwnd, SW_SHOWNA);

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	// `tray` and `com` are torn down by their RAII destructors here.
	return 0;
}
