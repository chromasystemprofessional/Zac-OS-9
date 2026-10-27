#pragma once

/*
 * The panels zacos9-controlpanel opens, one per run (`zacos9-controlpanel
 * NAME`). Each panel's own file registers it at its end:
 *
 *   PANEL("energysaver", "Nap Time", false, EnergySaverPanel);
 *
 * NAME, the name the menu bar shows, whether it needs platinumShellInit()
 * first (for its alerts), and how to make it. A new panel is its file, a
 * line in meson.build, and its desktop entry (Exec=zacos9-controlpanel NAME).
 */

#include <QWidget>
#include <memory>
#include <vector>

struct PanelInfo {
	const char *name;
	const char *title;
	bool alerts;
	std::unique_ptr<QWidget> (*make)();
};

inline std::vector<PanelInfo> &panels() {
	static std::vector<PanelInfo> all;
	return all;
}

#define PANEL_JOIN2(a, b) a##b
#define PANEL_JOIN(a, b) PANEL_JOIN2(a, b)
#define PANEL(name, title, alerts, ...) \
	[[maybe_unused]] static const bool PANEL_JOIN(panel_registered_, __LINE__) = \
		(panels().push_back({ name, title, alerts, \
			[] { return std::unique_ptr<QWidget>(new __VA_ARGS__); } }), true)
