#pragma once

class QWidget;

/* Window frame styles platinum-wm can draw (platinum-shell-v1). */
enum class FrameStyle {
	Document = 0,
	MovableModal = 1,
};

/* Bind platinum-shell-v1 if the compositor offers it (platinum-wm does). */
void platinumShellInit();
/* Ask for a frame style; call after the window is shown. */
void platinumSetFrameStyle(QWidget *window, FrameStyle style);
