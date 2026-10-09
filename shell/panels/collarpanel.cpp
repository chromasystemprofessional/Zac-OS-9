/*
 * The Collar control panel. Its layout is measured from the Mac OS 9
 * Control Strip panel: two group boxes on white, Show/Hide and Font
 * Settings. The preview is our own Collar artwork.
 */
#include "collarpanel.h"

#include <QPainter>
#include <QShowEvent>
#include <cstring>

#include "collarart.h"
#include "platinumshell.h"
#include "settings.h"

namespace collarhotkey {

QString current() {
	char value[64];
	if (pl_setting("collar-hotkey", value, sizeof(value)) && value[0]) {
		return QString::fromUtf8(value);
	}
	return Default;
}

QString display(const QString &hotkey) {
	QStringList parts = hotkey.split('+', Qt::SkipEmptyParts);
	if (!parts.isEmpty() && parts.last().size() == 1) {
		parts.last() = parts.last().toUpper();
	}
	return parts.join(" + ");
}

static QString keyName(int key) {
	if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
		return QString("F%1").arg(key - Qt::Key_F1 + 1);
	}
	if (key >= Qt::Key_A && key <= Qt::Key_Z) {
		return QString(QChar('a' + (key - Qt::Key_A)));
	}
	if (key >= Qt::Key_0 && key <= Qt::Key_9) {
		return QString(QChar('0' + (key - Qt::Key_0)));
	}
	switch (key) {
	case Qt::Key_Space: return "space";
	case Qt::Key_Left: return "Left";
	case Qt::Key_Right: return "Right";
	case Qt::Key_Up: return "Up";
	case Qt::Key_Down: return "Down";
	case Qt::Key_Home: return "Home";
	case Qt::Key_End: return "End";
	case Qt::Key_PageUp: return "Prior";
	case Qt::Key_PageDown: return "Next";
	case Qt::Key_Insert: return "Insert";
	case Qt::Key_Delete: return "Delete";
	case Qt::Key_Backspace: return "BackSpace";
	case Qt::Key_Return: return "Return";
	case Qt::Key_Tab: return "Tab";
	case Qt::Key_Minus: return "minus";
	case Qt::Key_Equal: return "equal";
	case Qt::Key_Comma: return "comma";
	case Qt::Key_Period: return "period";
	case Qt::Key_Slash: return "slash";
	case Qt::Key_Semicolon: return "semicolon";
	case Qt::Key_BracketLeft: return "bracketleft";
	case Qt::Key_BracketRight: return "bracketright";
	default: return QString();
	}
}

QString fromKey(int key, Qt::KeyboardModifiers mods) {
	const QString name = keyName(key);
	if (name.isEmpty()) {
		return QString();
	}
	QStringList parts;
	if (mods & Qt::ControlModifier) {
		parts << "command";
	}
	if (mods & Qt::AltModifier) {
		parts << "option";
	}
	if (mods & Qt::MetaModifier) {
		parts << "control";
	}
	if (mods & Qt::ShiftModifier) {
		parts << "shift";
	}
	const bool function = key >= Qt::Key_F1 && key <= Qt::Key_F24;
	/* Shift alone would steal a capital letter. */
	if (!function && (parts.isEmpty() || parts == QStringList { "shift" })) {
		return QString();
	}
	parts << name;
	return parts.join('+');
}

} // namespace collarhotkey

/* ---- Define hot key -------------------------------------------------------------- */

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int HK_W = 300, HK_H = 128;
static constexpr int HK_BOX_Y = 46;

HotKeyDialog::HotKeyDialog(const QString &current) : m_hotkey(current) {
	setWindowTitle("Define Hot Key");
	setFixedSize(HK_W, HK_H);
	m_ok = PanelButton("OK", QRect(HK_W - 13 - 58 - 3, HK_H - 13 - 20 - 3, 58, 20), true);
	m_ok.clicked = [this] { accept(); };
	m_cancel = PanelButton("Cancel", QRect(m_ok.rect.x() - 12 - 3 - 58, m_ok.rect.y(), 58, 20));
	m_cancel.clicked = [this] { reject(); };
}

void HotKeyDialog::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void HotKeyDialog::press(int key, Qt::KeyboardModifiers mods) {
	const QString hotkey = collarhotkey::fromKey(key, mods);
	if (!hotkey.isEmpty()) {
		m_hotkey = hotkey;
		update();
	}
}

void HotKeyDialog::paintEvent(QPaintEvent *) {
	Pixels px(HK_W, HK_H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, HK_W - 1, HK_H - 1, FACE);
	panelText(c, "Press the keys to show and hide the Collar.", 13, 24);
	pl_fill(c, 13, HK_BOX_Y, HK_W - 14, HK_BOX_Y + 22, C_WHITE);
	pl_outline(c, 13, HK_BOX_Y, HK_W - 14, HK_BOX_Y + 22, C_BLACK);
	Text value(collarhotkey::display(m_hotkey), HK_W - 40, PL_FONT_SYSTEM);
	pl_text(c, value.t, (HK_W - value.inkWidth()) / 2, HK_BOX_Y + 16, C_BLACK);
	m_cancel.paint(c);
	m_ok.paint(c);
	QPainter p(this);
	px.blit(p);
}

void HotKeyDialog::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_ok.press(pos) || m_cancel.press(pos)) {
		update();
	}
}

void HotKeyDialog::mouseMoveEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_ok.move(pos) || m_cancel.move(pos)) {
		update();
	}
}

void HotKeyDialog::mouseReleaseEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_ok.release(pos) || m_cancel.release(pos)) {
		update();
	}
}

void HotKeyDialog::keyPressEvent(QKeyEvent *e) {
	const auto mods = e->modifiers() & ~Qt::KeypadModifier;
	if (mods == Qt::NoModifier && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
		accept();
	} else if (mods == Qt::NoModifier && e->key() == Qt::Key_Escape) {
		reject();
	} else {
		press(e->key(), mods);
	}
}

/* ---- the panel ------------------------------------------------------------------- */

/* Measured from the Control Strip panel. */
static constexpr int GROUP_L = 10, GROUP_R = 282;
static constexpr int SHOW_TOP = 10, SHOW_BOTTOM = 150;
static constexpr int FONT_TOP = 160, FONT_BOTTOM = 236;
static constexpr int GROUP_INK_X = 21; /* group titles: views font, on the top line */
static constexpr int RADIO_X = 22, RADIO_Y = 26, RADIO_STEP = 20;
static constexpr int PREVIEW_X = 202, PREVIEW_Y = 31;
static constexpr int HOTKEY_L = 39, HOTKEY_T = 117, HOTKEY_R = 251, HOTKEY_B = 139;
static constexpr int HOTKEY_INK_X = 50, HOTKEY_TITLE_BASE = 120, HOTKEY_VALUE_BASE = 135;
static constexpr uint32_t GROUP_LINE = GRAY(0x8);

static const char *const FontNames[] = { "System", "Views" };
static const char *const FontSettings[] = { "system", "views" };
static const int FontSizes[] = { 12, 9 };

/* The panel's group boxes: one gray line on white, the title in the
 * views font, the line cleared 1 px before and 2 px after its ink. */
static void group(pl_canvas *c, int x0, int y0, int x1, int y1, const char *title, int baseline) {
	pl_outline(c, x0, y0, x1, y1, GROUP_LINE);
	Text t(title, 300, PL_FONT_VIEWS);
	pl_hline(c, GROUP_INK_X - 1, GROUP_INK_X + t.inkWidth() + 1, y0, C_WHITE);
	pl_text(c, t.t, GROUP_INK_X, baseline, C_BLACK);
}

CollarPanel::CollarPanel() : m_host(this) {
	setWindowTitle("Collar");
	setFixedSize(W, H);

	m_visibility.add("Show Collar", QPoint(RADIO_X, RADIO_Y));
	m_visibility.add("Hide Collar", QPoint(RADIO_X, RADIO_Y + RADIO_STEP));
	m_visibility.add("Hot key to show/hide", QPoint(RADIO_X, RADIO_Y + 2 * RADIO_STEP));
	char value[16];
	if (pl_setting("collar-visibility", value, sizeof(value))) {
		m_visibility.selected = !strcmp(value, "hide") ? 1 : !strcmp(value, "hotkey") ? 2 : 0;
	}
	m_visibility.changed = [this](int i) {
		static const char *const values[] = { nullptr, "hide", "hotkey" };
		pl_setting_set("collar-visibility", values[i]);
		m_define.enabled = i == 2;
		update();
	};

	m_hotkey = collarhotkey::current();
	m_define = PanelButton("Define hot key…", QRect(82, 85, 130, 20));
	m_define.enabled = m_visibility.selected == 2;
	m_define.clicked = [this] { defineHotKey(); };

	m_font.rect = QRect(65, 175, 169, PL_POPUP_H);
	m_font.label = "Font:";
	m_font.items = { PopupItem(FontNames[0]), PopupItem(FontNames[1]) };
	m_font.selected = pl_setting("collar-menu-font", value, sizeof(value)) && !strcmp(value, "views");
	m_font.chosen = [this](int i) {
		m_font.selected = i;
		pl_setting_set("collar-menu-font", i ? FontSettings[i] : nullptr);
		syncSize();
		update();
	};
	m_size.rect = QRect(65, 205, 67, PL_POPUP_H);
	m_size.label = "Size:";
	m_size.chosen = [this](int) { update(); };
	syncSize();

	m_host.radios = { &m_visibility };
	m_host.buttons = { &m_define };
	m_host.popups = { &m_font, &m_size };
}

/* Our bitmap fonts come in one size each. */
void CollarPanel::syncSize() {
	m_size.items = { PopupItem(QString::number(FontSizes[m_font.selected])) };
	m_size.selected = 0;
}

void CollarPanel::defineHotKey() {
	QString chosen;
	if (askHotKey) {
		chosen = askHotKey(m_hotkey);
	} else {
		HotKeyDialog dialog(m_hotkey);
		if (dialog.exec() == QDialog::Accepted) {
			chosen = dialog.chosen();
		}
	}
	if (chosen.isEmpty()) {
		return;
	}
	m_hotkey = chosen;
	pl_setting_set("collar-hotkey", chosen == collarhotkey::Default ? nullptr : chosen.toUtf8().constData());
	update();
}

void CollarPanel::render(pl_canvas *c) const {
	pl_fill(c, 0, 0, W - 1, H - 1, C_WHITE);

	group(c, GROUP_L, SHOW_TOP, GROUP_R, SHOW_BOTTOM, "Show/Hide", SHOW_TOP + 3);
	collarart::preview(c, PREVIEW_X, PREVIEW_Y);

	pl_outline(c, HOTKEY_L, HOTKEY_T, HOTKEY_R, HOTKEY_B, C_BLACK);
	Text title("Current hot key", 200, PL_FONT_VIEWS);
	pl_hline(c, HOTKEY_INK_X - 1, HOTKEY_INK_X + title.inkWidth() + 1, HOTKEY_T, C_WHITE);
	pl_text(c, title.t, HOTKEY_INK_X, HOTKEY_TITLE_BASE, C_BLACK);
	Text hotkey(collarhotkey::display(m_hotkey), HOTKEY_R - HOTKEY_L - 8, PL_FONT_VIEWS);
	const uint32_t ink = m_visibility.selected == 2 ? C_BLACK : GRAY(0x8);
	pl_text(c, hotkey.t, (HOTKEY_L + HOTKEY_R + 1 - hotkey.inkWidth()) / 2, HOTKEY_VALUE_BASE, ink);

	group(c, GROUP_L, FONT_TOP, GROUP_R, FONT_BOTTOM, "Font Settings", FONT_TOP + 3);
	m_host.paintControls(c, C_WHITE);
}

void CollarPanel::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	render(&px.c);
	QPainter p(this);
	px.blit(p);
}

void CollarPanel::mousePressEvent(QMouseEvent *e) {
	m_host.hostPress(e);
}

void CollarPanel::mouseMoveEvent(QMouseEvent *e) {
	m_host.hostMove(e);
}

void CollarPanel::mouseReleaseEvent(QMouseEvent *e) {
	m_host.hostRelease(e);
}

void CollarPanel::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	m_host.hostKey(e);
}
