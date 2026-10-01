#pragma once

/*
 * Pieces shared by the control panels: list boxes, push buttons and
 * checkboxes that keep their own state and track the mouse, all drawn
 * with lib/widgets.
 */

#include <QPoint>
#include <QRect>
#include <QStringList>
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
	pl_list state{};
	/* Called with the new row when the user picks one. */
	std::function<void(int)> picked;

	void setItems(const QStringList &items);
	void select(int row, bool notify);
	void scrollTo(int top);
	void ensureVisible(int row);
	pl_list view() const;
	void paint(pl_canvas *c, bool focused) const;
	/* A press inside the frame: picks a row or works the scroll bar. */
	bool press(QPoint pos);
	/* Arrow keys and type-to-select; true if handled. */
	bool key(int key, const QString &text);

private:
	QString m_typed;
	qint64 m_typedAt = 0;
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
