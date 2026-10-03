#include "netbrowser.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QTimer>

#include "afpclient.h"
#include "alert.h"
#include "connectdialog.h"
#include "logindialog.h"
#include "netvolumes.h"
#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"
#include "smbclient.h"
#include "volumedialog.h"

static constexpr int W = 400, H = 320;
static constexpr int MARGIN = 16;

NetBrowserWindow::NetBrowserWindow() {
	setWindowTitle("Network Browser");
	setFixedSize(W, H);
	m_list.frame = QRect(MARGIN, 32, W - 2 * MARGIN, H - 32 - MARGIN - PL_BUTTON_H - 12);
	m_list.picked = [this](int) { update(); };

	m_scan = PanelButton("Scan Again", QRect(MARGIN, H - MARGIN - PL_BUTTON_H, 100, PL_BUTTON_H));
	m_connect = PanelButton("Connect to Server…",
		QRect(W - MARGIN - 170, H - MARGIN - PL_BUTTON_H, 170, PL_BUTTON_H), true);
	m_scan.clicked = [this] { scan(); };
	m_connect.clicked = [this] { connectToServer(); };
	m_host.buttons = { &m_scan, &m_connect };
	m_host.defaultButton = &m_connect;

	/* A first scan right away; "Scan Again" repeats it (nothing here
	 * watches the network continuously — Bonjour entries can come and
	 * go, but re-browsing takes a few seconds either way, so it is
	 * asked for, not automatic). */
	QTimer::singleShot(0, this, [this] { scan(); });
}

void NetBrowserWindow::scan() {
	m_status = "Looking for computers…";
	m_host.buttons = {}; /* nothing to click mid-scan */
	update();
	QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

	/* Shorter than discoverServers()'s own default: this blocks the
	 * window (the same trade the File Sharing panel and the Software
	 * window both make waiting on a helper), so it is kept brisk rather
	 * than exhaustive. Scan Again is there for a second, longer look. */
	m_found = discoverServers(1500);
	QStringList names;
	for (const DiscoveredServer &s : m_found) {
		names << s.name + (s.kind == DiscoveredServer::AFP ? "  (AFP)" : "  (Windows)");
	}
	m_list.setItems(names);
	if (!names.isEmpty()) {
		m_list.select(0, false);
	}
	m_status = m_found.empty() ? "No computers were found announcing themselves. "
		"Use Connect to Server for one that doesn’t." : QString();
	m_host.buttons = { &m_scan, &m_connect };
	update();
}

void NetBrowserWindow::connectToServer() {
	QString address;
	DiscoveredServer::Kind kind;
	if (ConnectDialog::ask(this, &address, &kind)) {
		connectTo(address, kind);
	}
}

void NetBrowserWindow::connectTo(const QString &address, DiscoveredServer::Kind kind) {
	if (kind == DiscoveredServer::SMB) {
		const LoginDialog::Result login = LoginDialog::ask(this, address, true, true);
		if (!login.accepted) {
			return;
		}
		QString error;
		if (!smbMount(address, login.share, login.guest ? QString() : login.user,
				login.guest ? QString() : login.password, &error)) {
			Alert::ask(error.isEmpty() ? "The share could not be mounted." : error, "OK",
				QString());
		}
		return;
	}

	const AfpServerInfo info = afpServerInfo(address);
	if (!info.ok) {
		Alert::ask(info.error, "OK", QString());
		return;
	}
	QString error;
	for (;;) {
		const LoginDialog::Result login =
			LoginDialog::ask(this, info.name, info.guestAllowed, false, error);
		if (!login.accepted) {
			return;
		}
		const bool cleartext = !login.guest && info.cleartextOnly;
		if (cleartext && !Alert::ask(QStringLiteral("“%1” can only accept an "
				"unencrypted password. Send it anyway?").arg(info.name), "Send", "Cancel")) {
			return;
		}
		bool ok = false, wrong = false;
		QString err;
		const std::vector<AfpVolume> volumes = afpListVolumes(address,
			login.guest ? QString() : login.user, login.guest ? QString() : login.password,
			cleartext, &ok, &wrong, &err);
		if (ok) {
			const QString volume = VolumeDialog::ask(this, info.name, volumes);
			if (!volume.isEmpty()) {
				QString mountError;
				if (!afpMount(address, volume, afpMountPointFor(volume),
						login.guest ? QString() : login.user,
						login.guest ? QString() : login.password, cleartext, &mountError)) {
					Alert::ask(mountError, "OK", QString());
				}
			}
			return;
		}
		if (!wrong) {
			Alert::ask(err, "OK", QString());
			return;
		}
		error = err; /* back to the login dialog, showing why */
	}
}

void NetBrowserWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, GRAY(0xD));
	const QStringList statusLines = panelWrap(m_status, W - 2 * MARGIN, PL_FONT_VIEWS);
	if (!statusLines.isEmpty()) {
		panelText(c, statusLines.first(), MARGIN, 27, PL_FONT_VIEWS, GRAY(0x5));
	}
	m_list.paint(c, true);
	m_host.paintControls(c, GRAY(0xD));
	QPainter p(this);
	px.blit(p);
}

void NetBrowserWindow::mousePressEvent(QMouseEvent *e) {
	if (m_list.press(e->position().toPoint())) {
		update();
		return;
	}
	if (m_host.hostPress(e)) {
		update();
	}
}
void NetBrowserWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_list.move(e->position().toPoint()) || m_host.hostMove(e)) {
		update();
	}
}
void NetBrowserWindow::mouseReleaseEvent(QMouseEvent *e) {
	if (m_list.release() | m_host.hostRelease(e)) {
		update();
	}
}
void NetBrowserWindow::wheelEvent(QWheelEvent *e) {
	if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}
void NetBrowserWindow::mouseDoubleClickEvent(QMouseEvent *e) {
	pl_list v = m_list.view();
	const int row = v.n > 0
		? pl_list_row_at(m_list.frame.left(), m_list.frame.top(), m_list.frame.right(),
			m_list.frame.bottom(), &v, e->position().toPoint().x(), e->position().toPoint().y())
		: -1;
	if (row >= 0 && row < static_cast<int>(m_found.size())) {
		const DiscoveredServer &s = m_found[static_cast<size_t>(row)];
		connectTo(s.address, s.kind);
	}
}
void NetBrowserWindow::keyPressEvent(QKeyEvent *e) {
	if (m_list.key(e->key(), e->text())) {
		update();
		return;
	}
	if (m_host.hostKey(e)) {
		update();
	}
}
