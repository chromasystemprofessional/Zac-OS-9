#pragma once

#include <QMainWindow>
#include <QSet>

#include "keytags.h"

class QActionGroup;
class QLineEdit;
class QMenu;
class KeyboardView;

/*
 * Key Tags, ZacOS 9's Key Caps (Missing Manual ch. 5): the keyboard drawn
 * with what each key types in the layout in use. Holding Shift or Option
 * (or clicking them in the picture) shows their characters; keys that add
 * an accent to the next letter are outlined. Typing, or clicking a key,
 * adds its character to the line above, to copy elsewhere.
 */
class KeyTagsWindow : public QMainWindow {
public:
	KeyTagsWindow();

	KeyTags &tags() { return m_tags; }
	/* Show this layout; false if xkb doesn't know it. */
	bool showLayout(const QString &code);
	/* 0 plain, 1 Shift, 2 Option, 3 Shift-Option: from the keys held and
	 * the ones clicked down in the picture. */
	int level() const;
	bool isDown(int key) const;
	/* A key pressed or released on the keyboard (Linux key code). */
	void keyEvent(int key, bool down);
	/* A key clicked in the picture: Shift and Option stay down until
	 * clicked again; others type their character. */
	void click(int key);
	QString typed() const;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void fillLayoutMenu();
	bool isOptionCap(int key) const;

	KeyTags m_tags;
	QSet<int> m_held;
	bool m_shiftLatch = false, m_optionLatch = false;
	QLineEdit *m_line = nullptr;
	KeyboardView *m_view = nullptr;
	QMenu *m_layoutMenu = nullptr;
	QActionGroup *m_layoutGroup = nullptr;
};
