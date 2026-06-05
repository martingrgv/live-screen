#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "app/App.h"

// A single connected monitor, as needed for the tray menu and layout maths.
struct MonitorEntry
{
	HMONITOR     handle    = nullptr;
	RECT         rect      = {};      // rcMonitor, in virtual-screen coordinates
	std::wstring device;              // MONITORINFOEX.szDevice (\\.\DISPLAYn)
	bool         isPrimary = false;
	std::wstring label;               // e.g. "Monitor 1 (Primary) - 2560x1440"
};

// Enumerates the connected monitors in a stable left-to-right / top-to-bottom
// order. The order is what the tray menu's per-monitor toggle ids map onto.
std::vector<MonitorEntry> EnumerateMonitors();

// Computes the set of render-target rectangles (virtual-screen coordinates) for
// the given layout mode. For Specific mode only the currently-present monitors
// whose device names appear in `enabledMonitors` are included.
std::vector<RECT> ComputeTargetRects(
	MultiMonitorMode mode,
	const std::vector<std::wstring>& enabledMonitors);
