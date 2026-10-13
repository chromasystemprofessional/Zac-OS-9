#pragma once

#include <QPoint>
#include <QSize>
#include <QStringList>
#include <functional>
#include <cstdint>

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
/* The content size the zoom box gives the window ("just big enough"); the
 * window must be shown. An empty size: the zoom box fills the screen. */
void platinumSetStandardSize(QWidget *window, QSize size);
/* ⌘-click (`option`: Option-⌘-click) on the window's title text calls
 * `callback`, the button still down, instead of moving the window. Call
 * once the window is shown. */
void platinumOnTitleMenu(QWidget *window, std::function<void(bool option)> callback);
/* Ask for the window to appear at `frameTopLeft` (layout coordinates);
 * call right after show(), before the window first draws. */
void platinumSetWindowPosition(QWidget *window, QPoint frameTopLeft);
/* Be told where the window is whenever it maps or the user moves it. */
void platinumOnWindowPosition(QWidget *window, std::function<void(QPoint)> callback);
uint32_t platinumBeginLaunch(const QString &appId = {});
void platinumUpdateLaunch(uint32_t cookie, uint32_t pid);
void platinumCancelLaunch(uint32_t cookie);
/* Kill the processes behind an app's windows (the Apple menu's Force
 * Quit); false if the compositor can't. */
bool platinumForceQuit(const QString &appId);
bool platinumStartApplication(const QString &program, const QStringList &args,
	const QString &directory = {});
