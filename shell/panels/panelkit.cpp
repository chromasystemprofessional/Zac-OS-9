#include "panelkit.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPainter>
#include <algorithm>

#include "menudraw.h"
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
		case SB_THUMB_PART:
			/* Drag the thumb: remember where on it the press landed. */
			m_thumbGrab = pos.y() - frame.top() - (SB_ARROW + 1 + sb.thumb);
			break;
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

bool PanelList::move(QPoint pos) {
	if (m_thumbGrab < 0) {
		return false;
	}
	const pl_scrollbar sb = pl_list_scrollbar(frame.top(), frame.bottom(), &state);
	const int range = sb_thumb_range(sb.length);
	const int visible = pl_list_visible_rows(frame.top(), frame.bottom());
	if (range <= 0 || state.n <= visible) {
		return false;
	}
	const int thumb = std::clamp(pos.y() - frame.top() - (SB_ARROW + 1) - m_thumbGrab, 0, range);
	const int top = (thumb * (state.n - visible) + range / 2) / range;
	if (top == state.top) {
		return false;
	}
	scrollTo(top);
	return true;
}

bool PanelList::release() {
	const bool was = m_thumbGrab >= 0;
	m_thumbGrab = -1;
	return was;
}

bool PanelList::wheel(QPoint pos, int angleDeltaY) {
	if (!frame.contains(pos) || angleDeltaY == 0) {
		return false;
	}
	/* Three rows a notch, as most systems scroll; smooth-scrolling
	 * touchpads send smaller steps, which add up. */
	m_wheelRemainder += angleDeltaY;
	const int rows = m_wheelRemainder / 40;
	m_wheelRemainder -= rows * 40;
	if (rows == 0) {
		return false;
	}
	scrollTo(state.top - rows);
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
		pl_sound_event("button-click");
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
		pl_sound_event("checkbox-toggle");
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

/* ---- pop-up menus -------------------------------------------------------------- */

namespace {

/* Not in the HIG: Wi-Fi signal bars and a padlock, at the right of a
 * network's menu item, in the item's text colour. */
constexpr int SIGNAL_W = 11; /* four 2 px bars, 1 px apart */
constexpr int LOCK_W = 7;
constexpr int EXTRA_GAP = 4;  /* between the padlock and the bars */
constexpr int EXTRA_TEXT_GAP = 12;
constexpr int EXTRAS_W = LOCK_W + EXTRA_GAP + SIGNAL_W;

void paintSignal(pl_canvas *c, int x, int baseline, int bars, uint32_t on, uint32_t off) {
	for (int i = 0; i < 4; i++) {
		const int h = 2 + 2 * i, bx = x + 3 * i;
		pl_fill(c, bx, baseline - h + 1, bx + 1, baseline, i < bars ? on : off);
	}
}

void paintLock(pl_canvas *c, int x, int baseline, uint32_t color) {
	static const char *const rows[] = {
		"..###..", ".#...#.", ".#...#.", "#######", "#######",
		"###.###", "###.###", "#######",
	};
	const int top = baseline - 7;
	for (int r = 0; r < 8; r++) {
		for (int i = 0; i < LOCK_W; i++) {
			if (rows[r][i] == '#') {
				pl_put(c, x + i, top + r, color);
			}
		}
	}
}

/* The open menu: its own popup window, so it can extend past the
 * owner's edges. */
class PopupMenu : public QWidget {
public:
	PopupMenu(QWidget *owner, const std::vector<PopupItem> &items, int selected, int minWidth,
			std::function<void(int)> done)
		: QWidget(owner, Qt::Popup | Qt::FramelessWindowHint), m_items(items),
		  m_hover(selected), m_checked(selected), m_done(std::move(done)) {
		setAttribute(Qt::WA_TranslucentBackground);
		setAttribute(Qt::WA_DeleteOnClose);
		setMouseTracking(true);
		bool extras = false;
		for (const PopupItem &it : m_items) {
			m_texts.push_back(std::make_unique<Text>(it.text, 400, PL_FONT_SYSTEM));
			extras |= it.signal >= 0 || it.lock;
		}
		for (size_t i = 0; i < m_items.size(); i++) {
			menu_item mi{};
			if (!m_items[i].separator) {
				mi.label = m_texts[i]->t;
				mi.enabled = m_items[i].enabled;
				mi.checked = static_cast<int>(i) == m_checked;
				mi.indent = m_items[i].icon.isNull() ? 0 : PANEL_ICON_W;
			}
			m_menu.push_back(mi);
		}
		menu_measure(m_menu.data(), static_cast<int>(m_menu.size()), &m_w, &m_h);
		if (extras) {
			m_w += EXTRA_TEXT_GAP + EXTRAS_W;
		}
		m_w = std::max(m_w, minWidth);
		setFixedSize(m_w + MENU_SHADOW, m_h + MENU_SHADOW);
		m_opened.start();
	}

	/* Menu-local top of item i. */
	int itemTop(int i) const {
		return menu_item_top(m_menu.data(), static_cast<int>(m_menu.size()), i);
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(width(), height());
		pl_canvas *c = &px.c;
		const pl_accent accent = pl_accent_current();
		menu_paint(c, m_menu.data(), static_cast<int>(m_menu.size()), m_w, m_h, m_hover, accent);
		for (int i = 0; i < static_cast<int>(m_items.size()); i++) {
			const PopupItem &it = m_items[i];
			if (!it.icon.isNull()) {
				/* After the checkmark, centred in the row. */
				panelIcon(c, it.icon, MENU_TEXT_X - 4 + (PANEL_ICON_W - it.icon.width()) / 2,
					itemTop(i) + (MENU_ITEM_H - it.icon.height()) / 2);
			}
			if (it.separator || (it.signal < 0 && !it.lock)) {
				continue;
			}
			const bool lit = i == m_hover && it.enabled;
			const uint32_t ink = lit ? C_WHITE : it.enabled ? C_BLACK : GRAY(0x8);
			const int baseline = itemTop(i) + MENU_ITEM_BASELINE;
			const int sx = m_w - MENU_RIGHT_PAD - SIGNAL_W;
			if (it.signal >= 0) {
				paintSignal(c, sx, baseline, it.signal, ink, lit ? accent.dark : GRAY(0xA));
			}
			if (it.lock) {
				paintLock(c, sx - EXTRA_GAP - LOCK_W, baseline, ink);
			}
		}
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		if (!rect().contains(e->position().toPoint())) {
			close();
			return;
		}
		track(e->position().toPoint());
	}

	void mouseMoveEvent(QMouseEvent *e) override {
		track(e->position().toPoint());
	}

	void mouseReleaseEvent(QMouseEvent *e) override {
		if (!m_sticky && m_opened.elapsed() < QApplication::doubleClickInterval()) {
			/* A quick click on the button: the menu stays open, and the
			 * next click chooses (HIG: sticky menus). */
			m_sticky = true;
			return;
		}
		const int i = itemAt(e->position().toPoint());
		if (i >= 0) {
			choose(i);
		} else {
			close();
		}
	}

	void keyPressEvent(QKeyEvent *e) override {
		const int n = static_cast<int>(m_items.size());
		if (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down) {
			const int step = e->key() == Qt::Key_Up ? -1 : 1;
			for (int i = m_hover + step; i >= 0 && i < n; i += step) {
				if (m_items[i].enabled && !m_items[i].separator) {
					m_hover = i;
					update();
					break;
				}
			}
		} else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
			if (m_hover >= 0) {
				choose(m_hover);
			}
		} else if (e->key() == Qt::Key_Escape ||
				(e->key() == Qt::Key_Period && (e->modifiers() & Qt::ControlModifier))) {
			close();
		}
	}

private:
	int itemAt(QPoint p) const {
		if (p.x() < 0 || p.x() >= m_w) {
			return -1;
		}
		const int i = menu_item_at(m_menu.data(), static_cast<int>(m_menu.size()), m_h, p.y());
		return i >= 0 && m_items[i].enabled ? i : -1;
	}

	void track(QPoint p) {
		const int i = itemAt(p);
		if (i != m_hover) {
			m_hover = i;
			update();
		}
	}

	void choose(int i) {
		auto done = m_done;
		close();
		if (done) {
			done(i);
		}
	}

	std::vector<PopupItem> m_items;
	std::vector<std::unique_ptr<Text>> m_texts;
	std::vector<menu_item> m_menu;
	int m_w = 0, m_h = 0;
	int m_hover, m_checked;
	bool m_sticky = false;
	QElapsedTimer m_opened;
	std::function<void(int)> m_done;
};

} // namespace

void panelIcon(pl_canvas *c, const QImage &icon, int x, int y) {
	const QImage img = icon.convertToFormat(QImage::Format_ARGB32);
	for (int j = 0; j < img.height(); j++) {
		const QRgb *row = reinterpret_cast<const QRgb *>(img.constScanLine(j));
		for (int i = 0; i < img.width(); i++) {
			if (qAlpha(row[i]) > 127) {
				pl_put(c, x + i, y + j, 0xFF000000u | (row[i] & 0xFFFFFFu));
			}
		}
	}
}

/* The small button with only the double triangle, beside an icon well:
 * a black frame with cut corners, lit inside its top and left, shaded
 * inside its bottom and right. Measured from Mac OS 9's Get Info. */
static void paintArrowsButton(pl_canvas *c, int x, int y, int w, int h, bool enabled) {
	const int x1 = x + w - 1, y1 = y + h - 1;
	pl_fill(c, x + 1, y + 1, x1 - 1, y1 - 1, GRAY(0xD));
	pl_hline(c, x + 2, x1 - 2, y, C_BLACK);
	pl_hline(c, x + 2, x1 - 2, y1, C_BLACK);
	pl_vline(c, x, y + 2, y1 - 2, C_BLACK);
	pl_vline(c, x1, y + 2, y1 - 2, C_BLACK);
	pl_put(c, x + 1, y + 1, C_BLACK);
	pl_put(c, x1 - 1, y + 1, C_BLACK);
	pl_put(c, x + 1, y1 - 1, C_BLACK);
	pl_put(c, x1 - 1, y1 - 1, C_BLACK);
	pl_hline(c, x + 2, x1 - 3, y + 2, C_WHITE);
	pl_vline(c, x + 2, y + 2, y1 - 3, C_WHITE);
	pl_hline(c, x + 3, x1 - 2, y1 - 2, GRAY(0xA));
	pl_vline(c, x1 - 2, y + 3, y1 - 2, GRAY(0xA));
	pl_hline(c, x + 2, x1 - 2, y1 - 1, GRAY(0x5));
	pl_vline(c, x1 - 1, y + 2, y1 - 2, GRAY(0x5));
	const uint32_t ink = enabled ? C_BLACK : GRAY(0x8);
	const int mid = x + (w - 1) / 2, cy = y + h / 2;
	for (int i = 0; i < 4; i++) {
		pl_hline(c, mid - i, mid + i, cy - 5 + i, ink);
		pl_hline(c, mid - i, mid + i, cy + 4 - i, ink);
	}
}

void PanelPopup::paint(pl_canvas *c) const {
	if (iconWell) {
		/* The well: etched in, shaded top and left, lit bottom and right. */
		const int x = rect.x(), y = rect.y(), x1 = x + PANEL_WELL_W - 1, y1 = y + rect.height() - 1;
		pl_hline(c, x, x1 - 1, y, GRAY(0x8));
		pl_vline(c, x, y, y1 - 1, GRAY(0x8));
		pl_hline(c, x + 1, x1, y1, C_WHITE);
		pl_vline(c, x1, y + 1, y1, C_WHITE);
		if (selected >= 0 && selected < static_cast<int>(items.size())) {
			const QImage &icon = items[selected].icon;
			QImage shown = icon;
			if (!enabled) { /* dimmed: halfway to the face */
				shown = icon.convertToFormat(QImage::Format_ARGB32);
				for (int j = 0; j < shown.height(); j++) {
					auto *row = reinterpret_cast<QRgb *>(shown.scanLine(j));
					for (int i = 0; i < shown.width(); i++) {
						row[i] = qRgba((qRed(row[i]) + 0xDD) / 2, (qGreen(row[i]) + 0xDD) / 2,
							(qBlue(row[i]) + 0xDD) / 2, qAlpha(row[i]));
					}
				}
			}
			panelIcon(c, shown, x + (PANEL_WELL_W - icon.width()) / 2,
				y + (rect.height() - icon.height()) / 2);
		}
		paintArrowsButton(c, rect.right() - PANEL_ARROWS_W + 1, y, PANEL_ARROWS_W, rect.height(),
			enabled);
		return;
	}
	const int baseline = rect.y() + PL_POPUP_BASELINE(rect.height());
	const uint32_t ink = enabled ? C_BLACK : GRAY(0x8);
	if (!label.isEmpty()) {
		Text t(label, 300, PL_FONT_SYSTEM);
		pl_text(c, t.t, rect.x() - PL_POPUP_LABEL_GAP - t.inkWidth(), baseline, ink);
	}
	const QString current =
		selected >= 0 && selected < static_cast<int>(items.size()) ? items[selected].text : QString();
	Text choice(current, rect.width() - PL_POPUP_ARROWS_W - PL_POPUP_TEXT_X - 4, PL_FONT_SYSTEM);
	pl_popup_button_paint(c, rect.x(), rect.y(), rect.width(), rect.height(),
		current.isEmpty() ? nullptr : choice.t, enabled);
}

bool PanelPopup::press(QWidget *owner, QPoint pos) {
	if (!enabled || !rect.contains(pos) || items.empty()) {
		return false;
	}
	const int sel = std::clamp(selected, 0, static_cast<int>(items.size()) - 1);
	pl_sound_event("menu-open");
	auto *menu = new PopupMenu(owner, items, sel, rect.width() - 1,
		[this, owner, shown = items](int i) {
			/* The items may have been refreshed while the menu was open:
			 * find the chosen one again by its text. */
			for (int j = 0; j < static_cast<int>(items.size()); j++) {
				if (!items[j].separator && items[j].text == shown[i].text) {
					if (chosen) {
						pl_sound_event("menu-command");
						chosen(j);
					}
					break;
				}
			}
			owner->update();
		});
	/* The current choice opens level with the button (figure 2-7); beside
	 * an icon well, from the small button. */
	const int left = iconWell ? rect.right() - PANEL_ARROWS_W + 1 : rect.x();
	const QPoint at(left, rect.y() + 1 - menu->itemTop(sel));
	menu->move(owner->mapToGlobal(at));
	menu->show();
	return true;
}

/* ---- edit text fields ------------------------------------------------------------ */

namespace {
constexpr int EDIT_PAD = 4;         /* frame to the pen */
constexpr int EDIT_LINE_H = 12;     /* the views font's line */
constexpr int EDIT_BASELINE_1 = 15; /* one-line field, from the frame top */
constexpr int EDIT_BASELINE_N = 13; /* first line of a multi-line field */

int advanceOf(const QString &s) {
	if (s.isEmpty()) {
		return 0;
	}
	Text t(s, 100000, PL_FONT_VIEWS);
	return t.t ? t.t->advance : 0;
}
} // namespace

QString PanelEdit::shown() const {
	return password ? QString(text.size(), QChar(0x2022)) : text;
}

QStringList PanelEdit::lines() const {
	return shown().split('\n');
}

void PanelEdit::setText(const QString &t) {
	text = t;
	caret = anchor = static_cast<int>(text.size());
	m_scroll = 0;
}

void PanelEdit::selectAll() {
	anchor = 0;
	caret = static_cast<int>(text.size());
	keepCaretVisible();
}

QPoint PanelEdit::caretPos(int index) const {
	const QString s = shown();
	const int line = static_cast<int>(s.left(index).count('\n'));
	const int lineStart = static_cast<int>(s.lastIndexOf('\n', index - 1)) + 1;
	const int x = rect.left() + EDIT_PAD + advanceOf(s.mid(lineStart, index - lineStart)) - m_scroll;
	const int base = multiline ? rect.top() + EDIT_BASELINE_N + EDIT_LINE_H * line
	                           : rect.top() + EDIT_BASELINE_1;
	return QPoint(x, base);
}

int PanelEdit::indexAt(QPoint p) const {
	const QStringList ls = lines();
	int line = multiline ? (p.y() - rect.top() - EDIT_BASELINE_N + 9) / EDIT_LINE_H : 0;
	line = std::clamp(line, 0, static_cast<int>(ls.size()) - 1);
	int start = 0;
	for (int i = 0; i < line; i++) {
		start += static_cast<int>(ls[i].size()) + 1;
	}
	const QString &l = ls[line];
	const int x = p.x() - rect.left() - EDIT_PAD + m_scroll;
	int best = 0, prev = 0;
	for (int i = 1; i <= l.size(); i++) {
		const int a = advanceOf(l.left(i));
		if (x >= (prev + a) / 2) {
			best = i;
		}
		prev = a;
	}
	return start + best;
}

void PanelEdit::keepCaretVisible() {
	if (multiline) {
		return;
	}
	const int width = rect.width() - 2 * EDIT_PAD;
	const int cx = advanceOf(shown().left(caret));
	if (cx - m_scroll > width) {
		m_scroll = cx - width;
	} else if (cx < m_scroll) {
		m_scroll = cx;
	}
}

void PanelEdit::paint(pl_canvas *c, bool focused, bool caretOn) const {
	const QRect r = rect;
	pl_edit_frame_paint(c, r.left(), r.top(), r.right(), r.bottom());
	if (focused) {
		pl_focus_ring_paint(c, r.left(), r.top(), r.right(), r.bottom(), pl_accent_current(),
			GRAY(0xD));
	}
	/* Text and selection, clipped to the inside of the frame. */
	const int cx0 = r.left() + 1, cy0 = r.top() + 1;
	pl_canvas in = { c->px + (cy0 - c->y) * c->stride + (cx0 - c->x), c->stride, cx0, cy0,
		r.width() - 2, r.height() - 2 };
	const int s0 = std::min(caret, anchor), s1 = std::max(caret, anchor);
	const QStringList ls = lines();
	int start = 0;
	for (int i = 0; i < ls.size(); i++) {
		const int end = start + static_cast<int>(ls[i].size());
		const QPoint at = caretPos(start);
		if (focused && s0 != s1 && s1 >= start && s0 <= end) {
			const int a = caretPos(std::max(s0, start)).x();
			const int b = s1 > end ? r.right() : caretPos(std::min(s1, end)).x();
			pl_fill(&in, a, at.y() - 10, b, at.y() + 2, pl_highlight_current());
		}
		if (!ls[i].isEmpty()) {
			Text t(ls[i], 100000, PL_FONT_VIEWS);
			if (t.t && t.t->ink_l >= 0) {
				pl_text(&in, t.t, at.x() + t.t->ink_l - 1, at.y(), enabled ? C_BLACK : GRAY(0x8));
			}
		}
		start = end + 1;
	}
	if (focused && caretOn && enabled && s0 == s1) {
		const QPoint at = caretPos(caret);
		pl_vline(&in, at.x(), at.y() - 11, at.y() + 2, C_BLACK);
	}
}

bool PanelEdit::press(QPoint p, bool extend) {
	if (!enabled || !rect.contains(p)) {
		return false;
	}
	caret = indexAt(p);
	if (!extend) {
		anchor = caret;
	}
	m_dragging = true;
	return true;
}

bool PanelEdit::move(QPoint p) {
	if (!m_dragging) {
		return false;
	}
	caret = indexAt(p);
	keepCaretVisible();
	return true;
}

bool PanelEdit::release(QPoint) {
	const bool was = m_dragging;
	m_dragging = false;
	return was;
}

void PanelEdit::insert(const QString &raw) {
	QString s;
	for (QChar ch : raw) {
		if (ch == '\n' && multiline) {
			s += ch;
		} else if (ch.isPrint() && (!accepts || accepts(ch))) {
			s += ch;
		}
	}
	const int s0 = std::min(caret, anchor), s1 = std::max(caret, anchor);
	text.replace(s0, s1 - s0, s);
	caret = anchor = s0 + static_cast<int>(s.size());
}

bool PanelEdit::key(QKeyEvent *e) {
	if (!enabled) {
		return false;
	}
	const bool cmd = e->modifiers() & Qt::ControlModifier;
	const bool shift = e->modifiers() & Qt::ShiftModifier;
	const int s0 = std::min(caret, anchor), s1 = std::max(caret, anchor);
	const int key = e->key();
	const int len = static_cast<int>(text.size());
	/* After a caret move: Shift extends the selection, else it collapses. */
	auto moved = [&](int to) {
		caret = std::clamp(to, 0, len);
		if (!shift) {
			anchor = caret;
		}
		keepCaretVisible();
		return true;
	};
	auto changed = [&] {
		keepCaretVisible();
		if (edited) {
			edited();
		}
		return true;
	};
	if (cmd) {
		if (key == Qt::Key_A) {
			selectAll();
			return true;
		}
		if ((key == Qt::Key_C || key == Qt::Key_X) && s0 != s1 && !password) {
			QApplication::clipboard()->setText(text.mid(s0, s1 - s0));
		}
		if (key == Qt::Key_X && s0 != s1) {
			insert(QString());
			return changed();
		}
		if (key == Qt::Key_V) {
			QString paste = QApplication::clipboard()->text();
			if (!multiline) {
				paste.replace('\n', ' ');
			}
			insert(paste);
			return changed();
		}
		return key == Qt::Key_C;
	}
	switch (key) {
	case Qt::Key_Left:
		return moved(!shift && s0 != s1 ? s0 : caret - 1);
	case Qt::Key_Right:
		return moved(!shift && s0 != s1 ? s1 : caret + 1);
	case Qt::Key_Up:
	case Qt::Key_Down: {
		if (!multiline) {
			return moved(key == Qt::Key_Up ? 0 : len);
		}
		const QPoint at = caretPos(caret);
		const int dy = (key == Qt::Key_Up ? -1 : 1) * EDIT_LINE_H;
		return moved(indexAt(QPoint(at.x(), at.y() - 6 + dy)));
	}
	case Qt::Key_Home:
		return moved(static_cast<int>(text.lastIndexOf('\n', caret - 1)) + 1);
	case Qt::Key_End: {
		const int nl = static_cast<int>(text.indexOf('\n', caret));
		return moved(nl < 0 ? len : nl);
	}
	case Qt::Key_Backspace:
		if (s0 == s1 && caret > 0) {
			anchor = caret - 1;
		}
		insert(QString());
		return changed();
	case Qt::Key_Delete:
		if (s0 == s1 && caret < len) {
			anchor = caret + 1;
		}
		insert(QString());
		return changed();
	case Qt::Key_Return:
	case Qt::Key_Enter:
		if (!multiline) {
			return false;
		}
		insert("\n");
		return changed();
	case Qt::Key_Tab:
	case Qt::Key_Backtab:
	case Qt::Key_Escape:
		return false;
	default:
		break;
	}
	if (!e->text().isEmpty() && e->text().at(0).isPrint()) {
		insert(e->text());
		return changed();
	}
	return false;
}

/* ---- the host --------------------------------------------------------------------- */

PanelHost::PanelHost(QWidget *w) : m_widget(w), m_caret(std::make_unique<QTimer>()) {
	QObject::connect(m_caret.get(), &QTimer::timeout, w, [this] {
		m_caretOn = !m_caretOn;
		if (focus) {
			m_widget->update(focus->rect.adjusted(-3, -3, 3, 3));
		}
	});
	m_caret->start(std::max(200, QApplication::cursorFlashTime() / 2));
}

void PanelHost::setFocus(PanelEdit *e) {
	focus = e;
	if (e) {
		e->selectAll();
	}
	m_caretOn = true;
	m_widget->update();
}

void PanelHost::paintControls(pl_canvas *c, uint32_t) const {
	for (PanelPopup *p : popups) {
		p->paint(c);
	}
	for (PanelCheckbox *k : checks) {
		k->paint(c);
	}
	for (PanelRadios *r : radios) {
		r->paint(c);
	}
	for (PanelButton *b : buttons) {
		b->paint(c);
	}
	for (PanelEdit *e : edits) {
		e->paint(c, e == focus, m_caretOn);
	}
}

bool PanelHost::hostPress(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	for (PanelPopup *p : popups) {
		if (p->press(m_widget, pos)) {
			return true;
		}
	}
	for (PanelEdit *ed : edits) {
		if (ed->press(pos, e->modifiers() & Qt::ShiftModifier)) {
			focus = ed;
			m_pressedEdit = ed;
			m_caretOn = true;
			m_widget->update();
			return true;
		}
	}
	bool hit = false;
	for (PanelButton *b : buttons) {
		hit |= b->press(pos);
	}
	for (PanelCheckbox *k : checks) {
		hit |= k->press(pos);
	}
	for (PanelRadios *r : radios) {
		hit |= r->press(pos);
	}
	if (hit) {
		m_widget->update();
	}
	return hit;
}

bool PanelHost::hostMove(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	bool changed = m_pressedEdit && m_pressedEdit->move(pos);
	for (PanelButton *b : buttons) {
		changed |= b->move(pos);
	}
	for (PanelCheckbox *k : checks) {
		changed |= k->move(pos);
	}
	for (PanelRadios *r : radios) {
		changed |= r->move(pos);
	}
	if (changed) {
		m_widget->update();
	}
	return changed;
}

bool PanelHost::hostRelease(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	bool changed = false;
	if (m_pressedEdit) {
		m_pressedEdit->release(pos);
		m_pressedEdit = nullptr;
		changed = true;
	}
	for (PanelButton *b : buttons) {
		changed |= b->release(pos);
	}
	for (PanelCheckbox *k : checks) {
		changed |= k->release(pos);
	}
	for (PanelRadios *r : radios) {
		changed |= r->release(pos);
	}
	m_widget->update();
	return changed;
}

bool PanelHost::hostKey(QKeyEvent *e) {
	const int key = e->key();
	if (key == Qt::Key_Tab || key == Qt::Key_Backtab) {
		std::vector<PanelEdit *> live;
		for (PanelEdit *ed : edits) {
			if (ed->enabled) {
				live.push_back(ed);
			}
		}
		if (live.empty()) {
			return false;
		}
		auto it = std::find(live.begin(), live.end(), focus);
		int i = it == live.end() ? -1 : static_cast<int>(it - live.begin());
		const int n = static_cast<int>(live.size());
		i = key == Qt::Key_Backtab ? (i <= 0 ? n - 1 : i - 1) : (i + 1) % n;
		setFocus(live[i]);
		return true;
	}
	if (focus && focus->key(e)) {
		m_caretOn = true;
		m_widget->update();
		return true;
	}
	if ((key == Qt::Key_Return || key == Qt::Key_Enter) && defaultButton &&
			defaultButton->enabled && defaultButton->clicked) {
		defaultButton->clicked();
		return true;
	}
	if ((key == Qt::Key_Escape ||
			(key == Qt::Key_Period && (e->modifiers() & Qt::ControlModifier))) &&
			cancelButton && cancelButton->clicked) {
		cancelButton->clicked();
		return true;
	}
	return false;
}

/* ---- text ---------------------------------------------------------------------- */

void panelText(pl_canvas *c, const QString &s, int x, int baseline, pl_font font, uint32_t color,
		int maxWidth) {
	Text t(s, maxWidth, font);
	if (t.t && t.t->ink_l >= 0) {
		pl_text(c, t.t, x + t.t->ink_l - 1, baseline, color);
	}
}

void panelLabel(pl_canvas *c, const QString &s, int right, int baseline, uint32_t color) {
	Text t(s, 400, PL_FONT_SYSTEM);
	if (t.t && t.t->ink_l >= 0) {
		pl_text(c, t.t, right - t.inkWidth() + 1, baseline, color);
	}
}

QStringList panelWrap(const QString &s, int width, pl_font font) {
	QStringList lines;
	QString line;
	for (const QString &word : s.split(' ', Qt::SkipEmptyParts)) {
		const QString candidate = line.isEmpty() ? word : line + ' ' + word;
		if (!line.isEmpty() && Text(candidate, 100000, font).inkWidth() > width) {
			lines << line;
			line = word;
		} else {
			line = candidate;
		}
	}
	if (!line.isEmpty()) {
		lines << line;
	}
	return lines;
}
