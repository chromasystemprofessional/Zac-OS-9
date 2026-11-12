#include "volumedialog.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>

#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"
#include "startupmounts.h"
#include "tr.h"

static constexpr int W = 280, H = 266;
static constexpr int MARGIN = 16;
static constexpr int STARTUP_TOP = H - MARGIN - PL_BUTTON_H - 12 - 22;

VolumeDialog::VolumeDialog(const std::vector<AfpVolume> &volumes, const QString &server,
		const QString &user, bool guest, bool cleartext)
	: m_volumes(volumes), m_server(server), m_user(user), m_guest(guest),
	  m_cleartext(cleartext) {
	setWindowTitle(tr9("Select a Volume"));
	setFixedSize(W, H);
	m_list.frame = QRect(MARGIN, 32, W - 2 * MARGIN,
		STARTUP_TOP - 10 - 32);
	QStringList names;
	for (const AfpVolume &v : volumes) {
		names << (v.hasPassword ? v.name + "…" : v.name); /* an ellipsis: this one asks again */
	}
	m_list.setItems(names);
	if (!names.isEmpty()) {
		m_list.select(0, false);
	}
	m_list.picked = [this](int) { updateStartup(); update(); };

	m_startup = PanelCheckbox(tr9("Mount at startup"), QPoint(MARGIN, STARTUP_TOP));
	m_startup.toggled = [this](bool) { update(); };

	m_cancel = PanelButton(tr9("Cancel"), QRect(W - MARGIN - 164, H - MARGIN - PL_BUTTON_H, 76,
		PL_BUTTON_H));
	m_mount = PanelButton(tr9("Mount"), QRect(W - MARGIN - 84, H - MARGIN - PL_BUTTON_H, 84,
		PL_BUTTON_H), true);
	m_mount.enabled = !names.isEmpty();
	m_cancel.clicked = [this] { reject(); };
	m_mount.clicked = [this] {
		if (m_list.state.selected >= 0) {
			accept();
		}
	};
	m_host.buttons = { &m_cancel, &m_mount };
	m_host.checks = { &m_startup };
	m_host.defaultButton = &m_mount;
	m_host.cancelButton = &m_cancel;
	updateStartup();
}

void VolumeDialog::updateStartup() {
	const int selected = m_list.state.selected;
	const bool hasVolume = selected >= 0 && selected < static_cast<int>(m_volumes.size());
	const bool ownPassword = hasVolume && m_volumes[selected].hasPassword;
	const bool keychain = startupMountKeychainAvailable();
	m_startup.enabled = hasVolume && !ownPassword && (m_guest || keychain);
	m_startup.on = false;
	if (hasVolume && !ownPassword && (m_guest || keychain)) {
		StartupMount mount;
		mount.kind = StartupMount::AFP;
		mount.server = m_server;
		mount.volume = m_volumes[selected].name;
		mount.user = m_user;
		mount.guest = m_guest;
		mount.cleartext = m_cleartext;
		m_startup.on = startupMountExists(mount);
	}
	m_startup.hint = !hasVolume ? tr9("Choose a volume to mount at startup.")
		: ownPassword ? tr9("This volume has its own password, so it can’t be mounted automatically.")
		: !m_guest && !keychain ? tr9("The keychain isn’t available; only Guest volumes can mount at startup.")
		: tr9("Mounts this volume each time you log in.");
}

VolumeDialog::Result VolumeDialog::ask(QWidget *parent, const QString &serverName,
		const std::vector<AfpVolume> &volumes, const QString &server, const QString &user,
		bool guest, bool cleartext) {
	VolumeDialog dialog(volumes, server, user, guest, cleartext);
	dialog.setParent(parent, dialog.windowFlags());
	Result result;
	if (dialog.exec() != QDialog::Accepted || dialog.m_list.state.selected < 0) {
		return result;
	}
	result.volume = dialog.m_volumes[static_cast<size_t>(dialog.m_list.state.selected)].name;
	result.mountAtStartup = dialog.m_startup.on;
	(void)serverName;
	return result;
}

void VolumeDialog::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void VolumeDialog::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, GRAY(0xD));
	panelText(c, m_volumes.empty() ? tr9("This server has no volumes to open.") : tr9("Select a volume:"),
		MARGIN, 27);
	m_list.paint(c, true);
	m_host.paintControls(c, GRAY(0xD));
	QPainter p(this);
	px.blit(p);
}

void VolumeDialog::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_list.press(pos)) {
		m_mount.enabled = m_list.state.selected >= 0;
		updateStartup();
		update();
		return;
	}
	if (m_host.hostPress(e)) {
		update();
	}
}
void VolumeDialog::mouseMoveEvent(QMouseEvent *e) {
	if (m_list.move(e->position().toPoint()) || m_host.hostMove(e)) {
		update();
	}
}
void VolumeDialog::mouseReleaseEvent(QMouseEvent *e) {
	if (m_list.release() | m_host.hostRelease(e)) {
		update();
	}
}
void VolumeDialog::wheelEvent(QWheelEvent *e) {
	if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}
void VolumeDialog::keyPressEvent(QKeyEvent *e) {
	if (m_list.key(e->key(), e->text())) {
		updateStartup();
		update();
		return;
	}
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	QDialog::keyPressEvent(e);
}
