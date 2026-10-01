#include "panelkit.h"

#include <QDateTime>
#include <algorithm>

#include "settings.h"

/* ---- list boxes ------------------------------------------------------------ */

void PanelList::setItems(const QStringList &list) {
	items = list;
	texts.clear();
	for (const QString &s : list) {
		texts.push_back(std::make_unique<Text>(s, frame.width() - SB_WIDTH - 8, PL_FONT_SYSTEM));
	}
	state.n = static_cast<int>(texts.size());
	state.top = std::min(state.top, std::max(0, state.n - 1));
}

pl_list PanelList::view() const {
	pl_list v = state;
	static thread_local std::vector<const plat_text *> rows;
	rows.clear();
	for (auto &t : texts) {
		rows.push_back(t->t);
	}
	v.rows = rows.data();
	return v;
}

void PanelList::paint(pl_canvas *c, bool focused) const {
	pl_list v = view();
	v.focused = focused;
	pl_list_paint(c, frame.left(), frame.top(), frame.right(), frame.bottom(), &v,
		pl_accent_current(), pl_highlight_current());
}

void PanelList::scrollTo(int top) {
	const int visible = pl_list_visible_rows(frame.top(), frame.bottom());
	state.top = std::clamp(top, 0, std::max(0, state.n - visible));
}

void PanelList::ensureVisible(int row) {
	const int visible = pl_list_visible_rows(frame.top(), frame.bottom());
	if (row < state.top) {
		scrollTo(row);
	} else if (row >= state.top + visible) {
		scrollTo(row - visible + 1);
	}
}

void PanelList::select(int row, bool notify) {
	if (row < 0 || row >= state.n) {
		return;
	}
	state.selected = row;
	ensureVisible(row);
	if (notify && picked) {
		picked(row);
	}
}

bool PanelList::press(QPoint pos) {
	if (!frame.contains(pos)) {
		return false;
	}
	if (pos.x() >= frame.right() - SB_WIDTH + 1) {
		/* The scroll bar: arrows step a row, the track a page. */
		pl_scrollbar sb = pl_list_scrollbar(frame.top(), frame.bottom(), &state);
		const int visible = pl_list_visible_rows(frame.top(), frame.bottom());
		switch (sb_hit(&sb, pos.y() - frame.top())) {
		case SB_DEC_ARROW: scrollTo(state.top - 1); break;
		case SB_INC_ARROW: scrollTo(state.top + 1); break;
		case SB_DEC_PAGE: scrollTo(state.top - visible); break;
		case SB_INC_PAGE: scrollTo(state.top + visible); break;
		default: break;
		}
		return true;
	}
	pl_list v = view();
	const int row = pl_list_row_at(frame.left(), frame.top(), frame.right(), frame.bottom(), &v,
		pos.x(), pos.y());
	if (row >= 0) {
		select(row, true);
	}
	return true;
}

bool PanelList::key(int key, const QString &text) {
	if (key == Qt::Key_Up) {
		select(std::max(0, state.selected - 1), true);
		return true;
	}
	if (key == Qt::Key_Down) {
		select(std::min(state.n - 1, state.selected + 1), true);
		return true;
	}
	if (text.isEmpty() || !text.at(0).isPrint()) {
		return false;
	}
	/* Type-to-select, as in Mac lists: letters typed within a second
	 * build up a prefix. */
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	m_typed = now - m_typedAt < 1000 ? m_typed + text : text;
	m_typedAt = now;
	for (int i = 0; i < items.size(); i++) {
		if (items[i].startsWith(m_typed, Qt::CaseInsensitive)) {
			select(i, true);
			break;
		}
	}
	return true;
}

/* ---- push buttons ------------------------------------------------------------ */

PanelButton::PanelButton(const QString &text, QRect r, bool def)
	: rect(r), label(std::make_unique<Text>(text, r.width() - 8, PL_FONT_SYSTEM)), isDefault(def) {}

void PanelButton::paint(pl_canvas *c) const {
	unsigned flags = 0;
	if (isDefault) {
		flags |= PL_BUTTON_DEFAULT;
	}
	if (down && inside) {
		flags |= PL_BUTTON_PRESSED;
	}
	if (!enabled) {
		flags |= PL_BUTTON_DISABLED;
	}
	pl_button_paint(c, rect.x(), rect.y(), rect.width(), label ? label->t : nullptr, flags);
}

bool PanelButton::press(QPoint pos) {
	if (!enabled || !rect.contains(pos)) {
		return false;
	}
	down = inside = true;
	return true;
}

bool PanelButton::move(QPoint pos) {
	if (!down) {
		return false;
	}
	inside = rect.contains(pos);
	return true;
}

bool PanelButton::release(QPoint pos) {
	if (!down) {
		return false;
	}
	const bool fire = rect.contains(pos);
	down = inside = false;
	if (fire && clicked) {
		clicked();
	}
	return true;
}

/* ---- checkboxes ------------------------------------------------------------- */

PanelCheckbox::PanelCheckbox(const QString &text, QPoint p)
	: pos(p), label(std::make_unique<Text>(text, 400, PL_FONT_SYSTEM)) {}

QRect PanelCheckbox::hitRect() const {
	const int w = PL_CHECKBOX_LABEL_X + (label ? label->inkWidth() : 0);
	return QRect(pos.x(), pos.y() - 2, w, PL_CHECKBOX_SIZE + 4);
}

void PanelCheckbox::paint(pl_canvas *c) const {
	pl_checkbox_paint(c, pos.x(), pos.y(), on ? PL_CHECK_ON : PL_CHECK_OFF, down && inside,
		enabled, label ? label->t : nullptr);
}

bool PanelCheckbox::press(QPoint p) {
	if (!enabled || !hitRect().contains(p)) {
		return false;
	}
	down = inside = true;
	return true;
}

bool PanelCheckbox::move(QPoint p) {
	if (!down) {
		return false;
	}
	inside = hitRect().contains(p);
	return true;
}

bool PanelCheckbox::release(QPoint p) {
	if (!down) {
		return false;
	}
	const bool fire = hitRect().contains(p);
	down = inside = false;
	if (fire) {
		on = !on;
		if (toggled) {
			toggled(on);
		}
	}
	return true;
}

/* ---- sliders --------------------------------------------------------------- */

void PanelSlider::setLabels(const QString &left, const QString &right) {
	leftLabel = std::make_unique<Text>(left, 200, PL_FONT_VIEWS);
	rightLabel = std::make_unique<Text>(right, 200, PL_FONT_VIEWS);
}

QRect PanelSlider::hitRect() const {
	return QRect(pos.x(), pos.y(), width, PL_SLIDER_H);
}

void PanelSlider::paint(pl_canvas *c) const {
	pl_slider_paint(c, pos.x(), pos.y(), width, steps, value, enabled, pl_accent_current());
	const uint32_t ink = enabled ? C_BLACK : GRAY(0x8);
	if (leftLabel) {
		pl_text(c, leftLabel->t, pos.x() + 1, pos.y() + PL_SLIDER_H + 11, ink);
	}
	if (rightLabel) {
		pl_text(c, rightLabel->t, pos.x() + width - 1 - rightLabel->inkWidth(),
			pos.y() + PL_SLIDER_H + 11, ink);
	}
}

void PanelSlider::set(int v) {
	v = std::clamp(v, 0, steps - 1);
	if (v != value) {
		value = v;
		if (changed) {
			changed(value);
		}
	}
}

bool PanelSlider::press(QPoint p) {
	if (!enabled || !hitRect().contains(p)) {
		return false;
	}
	dragging = true;
	set(pl_slider_value_at(pos.x(), width, steps, p.x()));
	return true;
}

bool PanelSlider::move(QPoint p) {
	if (!dragging) {
		return false;
	}
	set(pl_slider_value_at(pos.x(), width, steps, p.x()));
	return true;
}

bool PanelSlider::release(QPoint) {
	if (!dragging) {
		return false;
	}
	dragging = false;
	return true;
}

bool PanelSlider::key(int key) {
	if (!enabled || (key != Qt::Key_Left && key != Qt::Key_Right)) {
		return false;
	}
	set(value + (key == Qt::Key_Right ? 1 : -1));
	return true;
}

/* ---- radio buttons ------------------------------------------------------------ */

void PanelRadios::add(const QString &text, QPoint pos) {
	buttons.push_back({ pos, std::make_unique<Text>(text, 300, PL_FONT_SYSTEM) });
}

int PanelRadios::hit(QPoint p) const {
	for (int i = 0; i < static_cast<int>(buttons.size()); i++) {
		const Button &b = buttons[i];
		const int w = PL_CHECKBOX_LABEL_X + (b.label ? b.label->inkWidth() : 0);
		if (QRect(b.pos.x(), b.pos.y() - 2, w, PL_RADIO_SIZE + 4).contains(p)) {
			return i;
		}
	}
	return -1;
}

void PanelRadios::paint(pl_canvas *c) const {
	for (int i = 0; i < static_cast<int>(buttons.size()); i++) {
		const Button &b = buttons[i];
		pl_radio_paint(c, b.pos.x(), b.pos.y(), i == selected, i == down && inside, enabled,
			b.label ? b.label->t : nullptr);
	}
}

bool PanelRadios::press(QPoint p) {
	const int i = enabled ? hit(p) : -1;
	if (i < 0) {
		return false;
	}
	down = i;
	inside = true;
	return true;
}

bool PanelRadios::move(QPoint p) {
	if (down < 0) {
		return false;
	}
	inside = hit(p) == down;
	return true;
}

bool PanelRadios::release(QPoint p) {
	if (down < 0) {
		return false;
	}
	const int i = hit(p) == down ? down : -1;
	down = -1;
	inside = false;
	if (i >= 0 && i != selected) {
		selected = i;
		if (changed) {
			changed(i);
		}
	}
	return true;
}

void panelGroup(pl_canvas *c, int x0, int y0, int x1, int y1, const char *title, uint32_t bg) {
	Text t(title, 400, PL_FONT_SYSTEM);
	pl_group_box_paint(c, x0, y0, x1, y1, t.t, bg);
}
