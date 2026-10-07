#pragma once

/*
 * Pieces shared by the control panels: list boxes, push buttons and
 * checkboxes that keep their own state and track the mouse, all drawn
 * with lib/widgets.
 */

#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QRect>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

#include "pixels.h"
#include "widgets.h"

/* A Platinum list box: its rows, selection and scrolling. */
struct PanelList {
	QRect frame; /* the black frame */
	QStringList items;
	std::vector<std::unique_ptr<Text>> texts;
	std::vector<int> icons;
	pl_list state{};
	/* Called with the new row when the user picks one. */
	std::function<void(int)> picked;

	/* Icons, if given, are one pl_icon_kind per row, drawn small before
	 * each row's text. */
	void setItems(const QStringList &items, const std::vector<int> &icons = {});
	void select(int row, bool notify);
	void scrollTo(int top);
	void ensureVisible(int row);
	pl_list view() const;
	void paint(pl_canvas *c, bool focused) const;
	/* A press inside the frame: picks a row or works the scroll bar
	 * (arrows, page, or a press on the thumb that move() then drags). */
	bool press(QPoint pos);
	bool move(QPoint pos);
	bool release();
	/* The scroll wheel over the list (QWheelEvent::angleDelta().y()). */
	bool wheel(QPoint pos, int angleDeltaY);
	/* Arrow keys and type-to-select; true if handled. */
	bool key(int key, const QString &text);

private:
	QString m_typed;
	qint64 m_typedAt = 0;
	int m_thumbGrab = -1; /* dragging the thumb: where on it it was pressed */
	int m_wheelRemainder = 0;
};

/* A push button that tracks the mouse and fires on release inside it. */
struct PanelButton {
	QRect rect; /* the button, without a default ring */
	std::unique_ptr<Text> label;
	bool isDefault = false;
	bool enabled = true;
	std::function<void()> clicked;

	bool down = false, inside = false;

	PanelButton() = default;
	PanelButton(const QString &text, QRect r, bool isDefault = false);
	void paint(pl_canvas *c) const;
	bool press(QPoint pos);
	bool move(QPoint pos);
	bool release(QPoint pos);
};

/* A checkbox with its label; clicking the box or the label toggles it. */
struct PanelCheckbox {
	QPoint pos; /* the box's top-left */
	std::unique_ptr<Text> label;
	bool on = false;
	bool enabled = true;
	std::function<void(bool)> toggled;

	bool down = false, inside = false;

	PanelCheckbox() = default;
	PanelCheckbox(const QString &text, QPoint p);
	QRect hitRect() const;
	void paint(pl_canvas *c) const;
	bool press(QPoint pos);
	bool move(QPoint pos);
	bool release(QPoint pos);
};

/* A horizontal slider with labels under its ends; dragging or clicking
 * picks a step, and `changed` fires as the value changes. */
struct PanelSlider {
	QPoint pos; /* the track's left end, at the thumb's top */
	int width = 200;
	int steps = 7;
	int value = 0;
	bool enabled = true;
	std::unique_ptr<Text> leftLabel, rightLabel;
	std::function<void(int)> changed;
	bool dragging = false;

	void setLabels(const QString &left, const QString &right);
	QRect hitRect() const;
	void paint(pl_canvas *c) const;
	bool press(QPoint p);
	bool move(QPoint p);
	bool release(QPoint p);
	bool key(int key); /* left and right arrows */

private:
	void set(int v);
};

/* A set of radio buttons; one is on. */
struct PanelRadios {
	struct Button {
		QPoint pos;
		std::unique_ptr<Text> label;
	};
	std::vector<Button> buttons;
	int selected = 0;
	bool enabled = true;
	std::function<void(int)> changed;
	int down = -1;
	bool inside = false;

	void add(const QString &text, QPoint pos);
	void paint(pl_canvas *c) const;
	bool press(QPoint p);
	bool move(QPoint p);
	bool release(QPoint p);

private:
	int hit(QPoint p) const;
};

/* A titled group box drawn on `bg`. */
void panelGroup(pl_canvas *c, int x0, int y0, int x1, int y1, const char *title, uint32_t bg);

/* One choice in a pop-up menu. Wi-Fi networks also show signal bars
 * (0..4; -1 for none) and a padlock; Get Info's privileges an icon
 * before the text. */
struct PopupItem {
	PopupItem(QString t = QString(), bool on = true, bool sep = false, int bars = -1, bool locked = false)
		: text(std::move(t)), enabled(on), separator(sep), signal(bars), lock(locked) {}
	PopupItem(const char *t, bool on = true) : PopupItem(QString(t), on) {}
	QString text;
	bool enabled = true;
	bool separator = false;
	int signal = -1;
	bool lock = false;
	QImage icon; /* at most PANEL_ICON_W x 16 */
};
constexpr int PANEL_ICON_W = 20;

/* A pop-up menu button (HIG figures 2-6, 2-7, 3-25), with an optional
 * label to its left. Pressing it opens the menu with the current choice
 * level with the button; release on an item to choose it, or click and
 * release quickly to leave the menu open (a "sticky" menu). */
struct PanelPopup {
	QRect rect; /* the button: PL_POPUP_H tall */
	QString label;
	std::vector<PopupItem> items;
	int selected = 0;
	bool enabled = true;
	/* Instead of the button, the choice's icon in a recessed well and a
	 * small button with the double triangle beside it (Get Info's
	 * privileges). `rect` holds both: PANEL_WELL_W + 3 + PANEL_ARROWS_W. */
	bool iconWell = false;
	/* Called with the chosen item, even if it is already selected
	 * (so commands such as "Other Network…" work). If the items change
	 * while the menu is open, the choice is matched up by its text. */
	std::function<void(int)> chosen;

	void paint(pl_canvas *c) const;
	/* Opens the menu over `owner` if pos is on the button. */
	bool press(QWidget *owner, QPoint pos);
};

constexpr int PANEL_WELL_W = 36, PANEL_ARROWS_W = 22;

/* An ARGB icon at (x, y), where it isn't transparent. */
void panelIcon(pl_canvas *c, const QImage &icon, int x, int y);

/* A Platinum edit text field (HIG figures 2-28, 3-30): one line, or
 * several (Return starts a new one). Click or drag to place the caret
 * and select; ⌘A, ⌘C, ⌘X and ⌘V work. */
struct PanelEdit {
	QRect rect; /* the black frame; PL_EDIT_H tall for one line */
	QString text;
	bool multiline = false;
	bool password = false;
	bool enabled = true;
	/* Characters the field takes (all printable ones if unset). */
	std::function<bool(QChar)> accepts;
	std::function<void()> edited;

	int caret = 0, anchor = 0;

	void setText(const QString &t);
	void selectAll();
	void paint(pl_canvas *c, bool focused, bool caretOn) const;
	bool press(QPoint p, bool extend);
	bool move(QPoint p);
	bool release(QPoint p);
	/* Editing keys; false for keys the field doesn't use (Tab, Escape,
	 * and Return in a one-line field). */
	bool key(QKeyEvent *e);

private:
	bool m_dragging = false;
	QString shown() const;
	QStringList lines() const;
	int indexAt(QPoint p) const;
	QPoint caretPos(int index) const; /* left of the character, baseline */
	void insert(const QString &s);
	int m_scroll = 0; /* one-line fields: pixels scrolled left */
	void keepCaretVisible();
};

/* Controls laid out in a window, and the plumbing they share: mouse
 * tracking, keyboard focus (Tab between fields), the blinking caret, and
 * the default button (Return) and cancel button (Escape, ⌘.). */
class PanelHost {
public:
	std::vector<PanelButton *> buttons;
	std::vector<PanelCheckbox *> checks;
	std::vector<PanelRadios *> radios;
	std::vector<PanelPopup *> popups;
	std::vector<PanelEdit *> edits;
	PanelEdit *focus = nullptr;
	PanelButton *defaultButton = nullptr, *cancelButton = nullptr;

	explicit PanelHost(QWidget *w);
	void setFocus(PanelEdit *e);
	void paintControls(pl_canvas *c, uint32_t bg) const;
	bool hostPress(QMouseEvent *e);
	bool hostMove(QMouseEvent *e);
	bool hostRelease(QMouseEvent *e);
	bool hostKey(QKeyEvent *e);

private:
	QWidget *m_widget;
	std::unique_ptr<QTimer> m_caret;
	bool m_caretOn = true;
	PanelEdit *m_pressedEdit = nullptr;
};

/* Text at a pen position, and a label right-aligned so its ink ends at
 * `right`; both on `baseline`. */
void panelText(pl_canvas *c, const QString &s, int x, int baseline, pl_font font = PL_FONT_SYSTEM,
	uint32_t color = C_BLACK, int maxWidth = 1000);
void panelLabel(pl_canvas *c, const QString &s, int right, int baseline, uint32_t color = C_BLACK);
/* Greedy word wrap: the lines of `s` that fit in `width`. */
QStringList panelWrap(const QString &s, int width, pl_font font);
