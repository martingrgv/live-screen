// Live Screen - entry point.
//
// Top-level orchestration only:
//   * Win32 init (COM, DPI awareness, controller window class + window).
//   * Desktop hooking via DesktopHost.
//   * Tray icon registration via TrayIcon (RAII).
//   * Multi-monitor layout (renderer windows + Media Engines) via WallpaperLayout.
//   * Standard message loop.
//
// The controller window is a hidden, persistent message sink: it owns the tray
// icon and receives all notifications (tray, power, WTS, presence timer, display
// change). The renderer windows that actually host the video are created and
// destroyed around it as the multi-monitor layout changes, so the controller is
// the one stable HWND for the lifetime of the app.
//
// All real work lives in the modules under src/.

// initguid.h must precede windows.h so the power-setting GUIDs
// (GUID_CONSOLE_DISPLAY_STATE, GUID_MONITOR_POWER_ON) get storage in this TU
// instead of being declared `extern`.
#include <initguid.h>
#include <windows.h>
#include <shellscalingapi.h>
#include <wtsapi32.h>

#include <string>

#include "app/App.h"
#include "app/OcclusionWatcher.h"
#include "app/Settings.h"
#include "app/WallpaperLayout.h"
#include "media/MediaPlayer.h"
#include "ui/TrayIcon.h"
#include "util/ComApartment.h"
#include "win/DesktopHost.h"

#include "res/Resource.h"

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Wtsapi32.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR /*pCmdLine*/, int /*nCmdShow*/)
{
	ComApartment com(COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (!com.ok())
	{
		return 0;
	}

	// Match physical pixels so our windows size to the monitors / WorkerW (which
	// are in physical pixels). Try Per-Monitor V2 first; fall back to system DPI
	// awareness on older Windows.
	if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
	{
		SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
	}

	g_hInstance = hInstance;

	const wchar_t* CONTROLLER_CLASS = L"Live Screen Controller Class";

	// Load the app icon from our embedded resources. LR_SHARED returns a cached
	// HICON owned by the system — it must not be DestroyIcon-ed, which matches
	// how WNDCLASSEX consumes hIcon/hIconSm (no cleanup required on shutdown).
	HICON hIconLarge = static_cast<HICON>(LoadImageW(
		hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
		GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
		LR_DEFAULTCOLOR | LR_SHARED));
	HICON hIconSmall = static_cast<HICON>(LoadImageW(
		hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
		GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
		LR_DEFAULTCOLOR | LR_SHARED));

	WNDCLASSEX wc = {};
	wc.cbSize        = sizeof(wc);
	wc.lpfnWndProc   = WindowProc;
	wc.hInstance     = hInstance;
	wc.lpszClassName = CONTROLLER_CLASS;
	wc.hIcon         = hIconLarge;
	wc.hIconSm       = hIconSmall;

	RegisterClassEx(&wc);

	// Hook (or spawn) the desktop wallpaper layer before creating renderers.
	GetWorkerW();

	// The controller window is never shown. It must be a real top-level window
	// (not message-only) so it receives the WM_DISPLAYCHANGE broadcast.
	HWND hwnd = CreateWindowEx(
		0,
		CONTROLLER_CLASS,
		L"Live Screen",
		WS_OVERLAPPED,
		0, 0, 0, 0,
		nullptr,
		nullptr,
		hInstance,
		nullptr);

	if (hwnd == nullptr)
	{
		return 0;
	}
	g_controllerHwnd = hwnd;

	TrayIcon tray(hwnd, WM_TRAYICON, L"Live Screen");

	// Load persisted display preferences before building the players so the first
	// frame is already rendered with the correct crop and on the right monitors.
	{
		bool fill = true;
		if (LoadFillScreen(fill) == S_OK)
		{
			g_fillScreen = fill;
		}

		MultiMonitorMode mode = MultiMonitorMode::Span;
		if (LoadMultiMonitorMode(mode) == S_OK)
		{
			g_multiMonitorMode = mode;
		}

		LoadEnabledMonitors(g_enabledMonitors);
	}

	// Resolve which wallpaper to play:
	//   1. If config.ini has a path *and* the file still exists, use it.
	//   2. Otherwise prompt the user once; persist their pick so subsequent
	//      launches skip the dialog.
	//   3. If the user cancels the initial prompt, exit cleanly — there is
	//      nothing for the wallpaper windows to display.
	std::wstring wallpaperPath;
	HRESULT loadHr = LoadWallpaperPath(wallpaperPath);
	bool needsPrompt =
		FAILED(loadHr) ||
		loadHr == S_FALSE ||
		wallpaperPath.empty() ||
		GetFileAttributesW(wallpaperPath.c_str()) == INVALID_FILE_ATTRIBUTES;

	if (needsPrompt)
	{
		HRESULT promptHr = PromptForWallpaperPath(hwnd, wallpaperPath);
		if (FAILED(promptHr) || wallpaperPath.empty())
		{
			return 0;
		}
		// Best-effort persist; a failed save shouldn't block playback.
		SaveWallpaperPath(wallpaperPath.c_str());
	}

	// Bring up Media Foundation + the shared device, then build the renderer
	// windows and engines for the active multi-monitor layout.
	HRESULT hrPlayer = EnsurePlayerStarted(wallpaperPath.c_str());
	if (FAILED(hrPlayer))
	{
		return 0;
	}
	RebuildWallpaper();

	// Subscribe to display-power and system-suspend notifications so we can
	// pause playback when the screen is off / dimmed or the machine is sleeping.
	// GUID_CONSOLE_DISPLAY_STATE is Win8+ and finer-grained (on / off / dim);
	// GUID_MONITOR_POWER_ON is the legacy fallback.
	HPOWERNOTIFY displayPowerNotify = RegisterPowerSettingNotification(
		hwnd, &GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
	HPOWERNOTIFY monitorPowerNotify = RegisterPowerSettingNotification(
		hwnd, &GUID_MONITOR_POWER_ON, DEVICE_NOTIFY_WINDOW_HANDLE);

	// Subscribe to session lock / unlock / disconnect so we pause while the
	// workstation is locked or the RDP session is disconnected.
	WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION);

	// Pause playback whenever a fullscreen / occluding app is in the foreground,
	// so the wallpaper stops burning GPU/CPU under a game or video player.
	StartOcclusionWatcher(hwnd);

	MSG msg = {};
	while (GetMessage(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	StopOcclusionWatcher();

	// WTSUnRegisterSessionNotification is called from WM_DESTROY (the HWND no
	// longer exists here). Power notification handles aren't tied to the HWND
	// lifetime in the same way and are safe to release after the loop.
	if (displayPowerNotify) UnregisterPowerSettingNotification(displayPowerNotify);
	if (monitorPowerNotify) UnregisterPowerSettingNotification(monitorPowerNotify);

	// `tray` and `com` are torn down by their RAII destructors here.
	return 0;
}
