#include "connectdialog.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"

static constexpr int W = 320, H = 158;
static constexpr int MARGIN = 16;

ConnectDialog::ConnectDialog() {
	setWindowTitle("Connect to Server");
	setFixedSize(W, H);
	m_address.rect = QRect(MARGIN, 34, W - 2 * MARGIN, PL_EDIT_H);
	m_kind.add("A classic Mac (AFP)", QPoint(MARGIN, 70));
	m_kind.add("A Windows computer (SMB)", QPoint(MARGIN, 90));
	m_kind.selected = 0;
	m_cancel = PanelButton("Cancel", QRect(W - MARGIN - 180, H - MARGIN - PL_BUTTON_H, 84,
		PL_BUTTON_H));
	m_connect = PanelButton("Connect", QRect(W - MARGIN - 88, H - MARGIN - PL_BUTTON_H, 88,
		PL_BUTTON_H), true);
	m_cancel.clicked = [this] { reject(); };
	m_connect.clicked = [this] {
		if (!m_address.text.trimmed().isEmpty()) {
			accept();
		}
	};
	m_host.edits = { &m_address };
	m_host.radios = { &m_kind };
	m_host.buttons = { &m_cancel, &m_connect };
	m_host.defaultButton = &m_connect;
	m_host.cancelButton = &m_cancel;
	m_host.setFocus(&m_address);
}

bool ConnectDialog::ask(QWidget *parent, QString *address, DiscoveredServer::Kind *kind) {
	ConnectDialog dialog;
	dialog.setParent(parent, dialog.windowFlags());
	if (dialog.exec() != QDialog::Accepted) {
		return false;
	}
	QString typed = dialog.m_address.text.trimmed();
	if (typed.startsWith("afp://", Qt::CaseInsensitive)) {
		*kind = DiscoveredServer::AFP;
		typed.remove(0, 6);
	} else if (typed.startsWith("smb://", Qt::CaseInsensitive)) {
		*kind = DiscoveredServer::SMB;
		typed.remove(0, 6);
	} else {
		*kind = dialog.m_kind.selected == 1 ? DiscoveredServer::SMB : DiscoveredServer::AFP;
	}
	*address = typed;
	return true;
}

void ConnectDialog::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void ConnectDialog::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, GRAY(0xD));
	panelText(c, "Server address:", MARGIN, 29);
	m_host.paintControls(c, GRAY(0xD));
	QPainter p(this);
	px.blit(p);
}

void ConnectDialog::mousePressEvent(QMouseEvent *e) {
	if (!m_host.hostPress(e)) {
		return;
	}
	update();
}
void ConnectDialog::mouseMoveEvent(QMouseEvent *e) {
	if (m_host.hostMove(e)) {
		update();
	}
}
void ConnectDialog::mouseReleaseEvent(QMouseEvent *e) {
	if (m_host.hostRelease(e)) {
		update();
	}
}
void ConnectDialog::keyPressEvent(QKeyEvent *e) {
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	QDialog::keyPressEvent(e);
}
