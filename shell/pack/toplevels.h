#pragma once

#include <functional>
#include <vector>

#include "apps.h"

/*
 * The windows zacos9-wm reports (wlr-foreign-toplevel-management), for the
 * Pack: their app ids, and whether each is in front or hidden; hiding a
 * program's windows. Events arrive through Qt's own Wayland dispatch.
 */
namespace Toplevels {
void start(std::function<void()> changed);
std::vector<WindowInfo> windows();
/* Hides every window of `appId` (Option-click: the program you leave). */
void hide(const QString &appId);
}
