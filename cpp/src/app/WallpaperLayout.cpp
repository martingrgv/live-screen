#include "app/WallpaperLayout.h"

#include "app/App.h"
#include "media/MediaPlayer.h"
#include "win/Monitors.h"
#include "win/RenderWindows.h"

#include <vector>

void RebuildWallpaper()
{
	// 1. Release engines first so they stop presenting to the windows we are
	//    about to destroy.
	ReleaseRenderTargets();

	// 2. Compute the target rectangles for the active mode and (re)create the
	//    renderer windows over them.
	std::vector<RECT> targets = ComputeTargetRects(g_multiMonitorMode, g_enabledMonitors);
	std::vector<HWND> windows = BuildRenderWindows(g_hInstance, targets);

	// 3. Bind a fresh engine to each new window.
	SetRenderTargets(windows);
}
