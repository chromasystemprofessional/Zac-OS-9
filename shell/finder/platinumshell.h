#pragma once

#include <QPoint>
#include <functional>

class QWidget;

/* Window frame styles zacos9-wm can draw (platinum-shell-v1). */
enum class FrameStyle {
	Document = 0,
	MovableModal = 1,
};

/* Bind platinum-shell-v1 if the compositor offers it (zacos9-wm does). */
void platinumShellInit();
/* Ask for a frame style; call after the window is shown. */
void platinumSetFrameStyle(QWidget *window, FrameStyle style);
/* Ask for the window to appear at `frameTopLeft` (layout coordinates);
 * call right after show(), before the window first draws. */
void platinumSetWindowPosition(QWidget *window, QPoint frameTopLeft);
/* Be told where the window is whenever it maps or the user moves it. */
void platinumOnWindowPosition(QWidget *window, std::function<void(QPoint)> callback);
