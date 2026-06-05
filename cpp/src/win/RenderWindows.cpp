#include "win/RenderWindows.h"

#include "app/App.h"

namespace
{
	constexpr PCWSTR kRenderClassName = L"Live Screen Renderer Class";

	std::vector<HWND> s_windows;
	bool              s_classRegistered = false;

	// WS_CLIPCHILDREN | WS_CLIPSIBLINGS lets DWM elide drawing under sibling
	// windows (e.g. SHELLDLL_DefView icons) and avoids extra invalidation as the
	// cursor passes over them, which otherwise contributes to cursor lag while
	// the video plays.
	constexpr DWORD kClipStyles = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;

	LRESULT CALLBACK RenderWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		// The media engine paints the entire client area; skip background erase
		// to avoid a white flash / leftover white in letterboxed regions.
		if (msg == WM_ERASEBKGND)
		{
			return 1;
		}
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}

	void EnsureRenderClass(HINSTANCE hInstance)
	{
		if (s_classRegistered)
		{
			return;
		}

		WNDCLASSEXW wc = {};
		wc.cbSize        = sizeof(wc);
		wc.lpfnWndProc   = RenderWndProc;
		wc.hInstance     = hInstance;
		wc.lpszClassName = kRenderClassName;
		// Black background avoids a white flash / leftover white area in
		// letterboxed regions before the first frame is presented.
		wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));

		RegisterClassExW(&wc);
		s_classRegistered = true;
	}

	// Creates a single renderer window covering `rect` (virtual-screen coords),
	// parented per the active desktop layout. Returns nullptr on failure.
	HWND CreateRenderWindow(HINSTANCE hInstance, const RECT& rect)
	{
		const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
		const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
		const int w  = rect.right - rect.left;
		const int h  = rect.bottom - rect.top;

		if (g_raisedDesktop)
		{
			// Raised-desktop layout (Win11 layered ShellView): create a top-level
			// WS_EX_LAYERED window first, then flip to WS_CHILD and SetParent to
			// Progman (WS_EX_LAYERED must be set BEFORE SetParent).
			HWND hwnd = CreateWindowExW(
				WS_EX_LAYERED | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
				kRenderClassName,
				L"Live Screen - Renderer",
				WS_POPUP | kClipStyles,
				rect.left, rect.top, w, h,
				nullptr, nullptr, hInstance, nullptr);
			if (!hwnd)
			{
				return nullptr;
			}

			SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
			SetWindowLongPtrW(hwnd, GWL_STYLE, WS_CHILD | WS_VISIBLE | kClipStyles);
			SetParent(hwnd, g_progman);

			if (g_shellDefView)
			{
				// Z-order just below the icons (SHELLDLL_DefView) so they stay on top.
				SetWindowPos(hwnd, g_shellDefView, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
			}

			// Position in Progman client coordinates (== virtual-screen coords).
			SetWindowPos(hwnd, nullptr, rect.left, rect.top, w, h,
				SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			ShowWindow(hwnd, SW_SHOWNA);
			return hwnd;
		}

		if (g_workerw)
		{
			// Classic layout: child of WorkerW. WorkerW's client (0,0) maps to the
			// top-left of the virtual screen, so translate by -virtual-origin.
			return CreateWindowExW(
				WS_EX_NOACTIVATE,
				kRenderClassName,
				L"Live Screen - Renderer",
				WS_CHILD | WS_VISIBLE | kClipStyles,
				rect.left - vx, rect.top - vy, w, h,
				g_workerw, nullptr, hInstance, nullptr);
		}

		// Fallback: no desktop layer hooked. Plain popup in screen coordinates.
		HWND hwnd = CreateWindowExW(
			WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
			kRenderClassName,
			L"Live Screen - Renderer",
			WS_POPUP | WS_VISIBLE | kClipStyles,
			rect.left, rect.top, w, h,
			nullptr, nullptr, hInstance, nullptr);
		if (hwnd)
		{
			ShowWindow(hwnd, SW_SHOWNA);
		}
		return hwnd;
	}
}

std::vector<HWND> BuildRenderWindows(HINSTANCE hInstance, const std::vector<RECT>& targets)
{
	DestroyRenderWindows();
	EnsureRenderClass(hInstance);

	for (const RECT& rect : targets)
	{
		if (HWND hwnd = CreateRenderWindow(hInstance, rect))
		{
			s_windows.push_back(hwnd);
		}
	}
	return s_windows;
}

void DestroyRenderWindows()
{
	for (HWND hwnd : s_windows)
	{
		if (hwnd && IsWindow(hwnd))
		{
			DestroyWindow(hwnd);
		}
	}
	s_windows.clear();
}

const std::vector<HWND>& RenderWindows()
{
	return s_windows;
}
