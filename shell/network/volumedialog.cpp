#include "volumedialog.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>

#include "pixels.h"
#include "platinumshell.h"
#include "settings.h"

static constexpr int W = 280, H = 240;
static constexpr int MARGIN = 16;

VolumeDialog::VolumeDialog(const std::vector<AfpVolume> &volumes) : m_volumes(volumes) {
	setWindowTitle("Select a Volume");
	setFixedSize(W, H);
	m_list.frame = QRect(MARGIN, 32, W - 2 * MARGIN, H - 32 - MARGIN - PL_BUTTON_H - 12);
	QStringList names;
	for (const AfpVolume &v : volumes) {
		names << (v.hasPassword ? v.name + "…" : v.name); /* an ellipsis: this one asks again */
	}
	m_list.setItems(names);
	if (!names.isEmpty()) {
		m_list.select(0, false);
	}
	m_list.picked = [this](int) { update(); };

	m_cancel = PanelButton("Cancel", QRect(W - MARGIN - 164, H - MARGIN - PL_BUTTON_H, 76,
		PL_BUTTON_H));
	m_mount = PanelButton("Mount", QRect(W - MARGIN - 84, H - MARGIN - PL_BUTTON_H, 84,
		PL_BUTTON_H), true);
	m_mount.enabled = !names.isEmpty();
	m_cancel.clicked = [this] { reject(); };
	m_mount.clicked = [this] {
		if (m_list.state.selected >= 0) {
			accept();
		}
	};
	m_host.buttons = { &m_cancel, &m_mount };
	m_host.defaultButton = &m_mount;
	m_host.cancelButton = &m_cancel;
}

QString VolumeDialog::ask(QWidget *parent, const QString &serverName,
		const std::vector<AfpVolume> &volumes) {
	VolumeDialog dialog(volumes);
	dialog.setParent(parent, dialog.windowFlags());
	(void)serverName;
	if (dialog.exec() != QDialog::Accepted || dialog.m_list.state.selected < 0) {
		return QString();
	}
	return dialog.m_volumes[static_cast<size_t>(dialog.m_list.state.selected)].name;
}

void VolumeDialog::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void VolumeDialog::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, GRAY(0xD));
	panelText(c, m_volumes.empty() ? "This server has no volumes to open." : "Select a volume:",
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
		update();
		return;
	}
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	QDialog::keyPressEvent(e);
}
