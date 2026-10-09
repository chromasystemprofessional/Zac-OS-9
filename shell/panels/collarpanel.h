#pragma once

#include <QDialog>
#include <QWidget>

#include "panelkit.h"

/*
 * Hot keys as desktop.conf's collar-hotkey stores them: modifier tokens
 * (command, option, control, shift) and an xkb keysym name joined by '+',
 * e.g. "command+F8". The compositor matches them (compositor/src/hotkey.c).
 */
namespace collarhotkey {
constexpr const char *Default = "command+F8";
/* The setting, or the default if it's unset or unreadable. */
QString current();
/* "command+F8" -> "command + F8", the way the panel shows it. */
QString display(const QString &hotkey);
/* The hot key for a key press, or an empty string if it can't be one:
 * function keys work alone, anything else needs a modifier. Qt's
 * Control is ⌘, since the compositor hands clients ⌘ as Ctrl. */
QString fromKey(int key, Qt::KeyboardModifiers mods);
} // namespace collarhotkey

/* "Define hot key...": press the new key, then OK. */
class HotKeyDialog : public QDialog {
public:
	explicit HotKeyDialog(const QString &current);
	QString chosen() const { return m_hotkey; }
	/* What a key press does in the dialog (for tests). */
	void press(int key, Qt::KeyboardModifiers mods);

protected:
	void paintEvent(QPaintEvent *) override;
	void showEvent(QShowEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	QString m_hotkey;
	PanelButton m_ok, m_cancel;
};

/*
 * The Collar control panel, after the Control Strip panel: show or hide
 * the Collar, or toggle it with a hot key (desktop.conf collar-visibility,
 * collar-hotkey), and the font of its module menus (collar-menu-font).
 */
class CollarPanel : public QWidget {
public:
	static constexpr int W = 295, H = 248;
	CollarPanel();

	/* For tests. */
	PanelRadios &visibility() { return m_visibility; }
	PanelPopup &font() { return m_font; }
	PanelButton &defineButton() { return m_define; }
	/* Replaces the modal dialog in tests: returns the new hot key or "". */
	std::function<QString(const QString &)> askHotKey;
	void render(pl_canvas *c) const;

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	void defineHotKey();
	void syncSize();

	PanelHost m_host;
	PanelRadios m_visibility;
	PanelButton m_define;
	PanelPopup m_font, m_size;
	QString m_hotkey;
};
