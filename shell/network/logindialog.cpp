#include "logindialog.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"

static constexpr int W = 340;
static constexpr int MARGIN = 16;
static constexpr int FIELD_X = MARGIN + 60;

LoginDialog::LoginDialog(const QString &serverName, bool guestAllowed, bool needsShare,
		const QString &error)
	: m_error(error), m_needsShare(needsShare) {
	setWindowTitle("Connect to the File Server");

	/* Laid out top to bottom first, so the window is exactly as tall
	 * as what ends up in it — not a guessed constant (an earlier,
	 * untested version of this got that guess wrong and clipped the
	 * password field right under the buttons; see docs/network.md). */
	int y = 44;
	m_who.add("Guest", QPoint(MARGIN, y));
	m_who.add("Registered User", QPoint(MARGIN, y + 20));
	/* PanelRadios enables or disables the whole group, not one button
	 * at a time, so Guest can't be struck out on its own when the
	 * server doesn't offer it; Registered User is selected by default
	 * instead, and choosing Guest anyway simply gets the same refusal
	 * the server itself would give. */
	m_who.selected = guestAllowed ? 0 : 1;
	y += 44;
	m_name.rect = QRect(FIELD_X, y, W - MARGIN - FIELD_X, PL_EDIT_H);
	y += PL_EDIT_H + 6;
	m_password.rect = QRect(FIELD_X, y, W - MARGIN - FIELD_X, PL_EDIT_H);
	m_password.password = true;
	y += PL_EDIT_H + 6;
	if (needsShare) {
		m_share.rect = QRect(FIELD_X, y, W - MARGIN - FIELD_X, PL_EDIT_H);
		y += PL_EDIT_H + 6;
	}
	if (!error.isEmpty()) {
		y += 16;
	}
	const int H = y + 12 + PL_BUTTON_H + MARGIN;
	setFixedSize(W, H);

	m_cancel = PanelButton("Cancel", QRect(W - MARGIN - 180, H - MARGIN - PL_BUTTON_H, 84,
		PL_BUTTON_H));
	m_connect = PanelButton("Connect", QRect(W - MARGIN - 88, H - MARGIN - PL_BUTTON_H, 88,
		PL_BUTTON_H), true);
	m_cancel.clicked = [this] { reject(); };
	m_connect.clicked = [this] {
		if (m_who.selected == 1 && m_name.text.trimmed().isEmpty()) {
			return; /* a registered user needs a name; Guest needs nothing */
		}
		if (m_needsShare && m_share.text.trimmed().isEmpty()) {
			return;
		}
		accept();
	};
	m_who.changed = [this](int) { updateEnabled(); };
	updateEnabled();

	m_host.radios = { &m_who };
	m_host.edits = needsShare
		? std::vector<PanelEdit *>{ &m_name, &m_password, &m_share }
		: std::vector<PanelEdit *>{ &m_name, &m_password };
	m_host.buttons = { &m_cancel, &m_connect };
	m_host.defaultButton = &m_connect;
	m_host.cancelButton = &m_cancel;
	m_host.setFocus(guestAllowed ? nullptr : &m_name);

	(void)serverName;
}

void LoginDialog::updateEnabled() {
	const bool registered = m_who.selected == 1;
	m_name.enabled = registered;
	m_password.enabled = registered;
}

LoginDialog::Result LoginDialog::ask(QWidget *parent, const QString &serverName, bool guestAllowed,
		bool needsShare, const QString &error) {
	LoginDialog dialog(serverName, guestAllowed, needsShare, error);
	dialog.setParent(parent, dialog.windowFlags());
	Result r;
	if (dialog.exec() != QDialog::Accepted) {
		return r;
	}
	r.accepted = true;
	r.guest = dialog.m_who.selected == 0;
	if (!r.guest) {
		r.user = dialog.m_name.text.trimmed();
		r.password = dialog.m_password.text;
	}
	if (needsShare) {
		r.share = dialog.m_share.text.trimmed();
	}
	return r;
}

void LoginDialog::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void LoginDialog::paintEvent(QPaintEvent *) {
	Pixels px(width(), height());
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, width() - 1, height() - 1, GRAY(0xD));
	panelText(c, "Connect to the file server as:", MARGIN, 29);
	panelLabel(c, "Name:", FIELD_X - 8, m_name.rect.top() + 15);
	panelLabel(c, "Password:", FIELD_X - 8, m_password.rect.top() + 15);
	if (m_needsShare) {
		panelLabel(c, "Share:", FIELD_X - 8, m_share.rect.top() + 15);
	}
	if (!m_error.isEmpty()) {
		const int y = (m_needsShare ? m_share.rect : m_password.rect).bottom() + 20;
		panelText(c, m_error, MARGIN, y, PL_FONT_VIEWS, RGB(0x99, 0x00, 0x00));
	}
	m_host.paintControls(c, GRAY(0xD));
	QPainter p(this);
	px.blit(p);
}

void LoginDialog::mousePressEvent(QMouseEvent *e) {
	if (m_host.hostPress(e)) {
		update();
	}
}
void LoginDialog::mouseMoveEvent(QMouseEvent *e) {
	if (m_host.hostMove(e)) {
		update();
	}
}
void LoginDialog::mouseReleaseEvent(QMouseEvent *e) {
	if (m_host.hostRelease(e)) {
		update();
	}
}
void LoginDialog::keyPressEvent(QKeyEvent *e) {
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	QDialog::keyPressEvent(e);
}
