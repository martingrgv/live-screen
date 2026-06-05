#include "win/Monitors.h"

#include <algorithm>

namespace
{
	BOOL CALLBACK CollectMonitorProc(HMONITOR hmon, HDC /*hdc*/, LPRECT /*rc*/, LPARAM lParam)
	{
		auto* out = reinterpret_cast<std::vector<MonitorEntry>*>(lParam);

		MONITORINFOEXW mi{};
		mi.cbSize = sizeof(mi);
		if (!GetMonitorInfoW(hmon, &mi))
		{
			return TRUE; // skip this monitor, keep enumerating
		}

		MonitorEntry entry;
		entry.handle    = hmon;
		entry.rect      = mi.rcMonitor;
		entry.device    = mi.szDevice;
		entry.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
		out->push_back(std::move(entry));
		return TRUE;
	}
}

std::vector<MonitorEntry> EnumerateMonitors()
{
	std::vector<MonitorEntry> monitors;
	EnumDisplayMonitors(nullptr, nullptr, CollectMonitorProc, reinterpret_cast<LPARAM>(&monitors));

	// Stable left-to-right, then top-to-bottom ordering so the per-monitor menu
	// items keep their position across rebuilds (independent of EnumDisplayMonitors'
	// device order).
	std::sort(monitors.begin(), monitors.end(),
		[](const MonitorEntry& a, const MonitorEntry& b)
		{
			if (a.rect.left != b.rect.left)
			{
				return a.rect.left < b.rect.left;
			}
			return a.rect.top < b.rect.top;
		});

	for (size_t i = 0; i < monitors.size(); ++i)
	{
		MonitorEntry& m = monitors[i];
		const LONG w = m.rect.right - m.rect.left;
		const LONG h = m.rect.bottom - m.rect.top;

		wchar_t buf[96] = {};
		swprintf_s(
			buf,
			L"Monitor %zu%s - %ldx%ld",
			i + 1,
			m.isPrimary ? L" (Primary)" : L"",
			w,
			h);
		m.label = buf;
	}

	return monitors;
}

std::vector<RECT> ComputeTargetRects(
	MultiMonitorMode mode,
	const std::vector<std::wstring>& enabledMonitors)
{
	std::vector<RECT> targets;

	switch (mode)
	{
	case MultiMonitorMode::Span:
	{
		RECT vr{
			GetSystemMetrics(SM_XVIRTUALSCREEN),
			GetSystemMetrics(SM_YVIRTUALSCREEN),
			GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
			GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN),
		};
		targets.push_back(vr);
		break;
	}

	case MultiMonitorMode::Mirror:
	{
		for (const MonitorEntry& m : EnumerateMonitors())
		{
			targets.push_back(m.rect);
		}
		break;
	}

	case MultiMonitorMode::Primary:
	{
		for (const MonitorEntry& m : EnumerateMonitors())
		{
			if (m.isPrimary)
			{
				targets.push_back(m.rect);
				break;
			}
		}
		break;
	}

	case MultiMonitorMode::Specific:
	{
		for (const MonitorEntry& m : EnumerateMonitors())
		{
			const bool enabled = std::any_of(
				enabledMonitors.begin(), enabledMonitors.end(),
				[&](const std::wstring& dev) { return dev == m.device; });
			if (enabled)
			{
				targets.push_back(m.rect);
			}
		}
		break;
	}
	}

	return targets;
}
