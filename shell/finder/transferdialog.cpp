#include "transferdialog.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <algorithm>
#include "platinumshell.h"
#include "settings.h"
#include "widgets.h"

TransferDialog::TransferDialog(std::atomic_bool &stopped) : m_stopped(stopped) {
	setObjectName("sniffer-transfer");
	setWindowTitle("Copying Files");
	setWindowModality(Qt::ApplicationModal);
	setFixedSize(430, 175);
}

void TransferDialog::setProgress(const QString &operation, const QString &file,
		const QString &destination, quint64 completed, quint64 total, bool preparing) {
	m_operation = operation;
	m_file = file;
	m_destination = destination;
	m_completed = completed;
	m_total = total;
	m_preparing = preparing;
	setWindowTitle(operation + " Files");
	update();
}

void TransferDialog::reject() {
	m_stopped.store(true);
	update();
}

void TransferDialog::showEvent(QShowEvent *event) {
	QDialog::showEvent(event);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void TransferDialog::closeEvent(QCloseEvent *event) {
	reject();
	event->ignore();
}

void TransferDialog::keyPressEvent(QKeyEvent *event) {
	if (event->key() == Qt::Key_Escape ||
			(event->key() == Qt::Key_Period && (event->modifiers() & Qt::ControlModifier))) {
		reject();
	}
}

void TransferDialog::mousePressEvent(QMouseEvent *event) {
	m_tracking = event->button() == Qt::LeftButton && m_stop.contains(event->position().toPoint());
	m_inside = m_tracking;
	update();
}

void TransferDialog::mouseMoveEvent(QMouseEvent *event) {
	m_inside = m_tracking && m_stop.contains(event->position().toPoint());
	update();
}

void TransferDialog::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && m_tracking && m_stop.contains(event->position().toPoint())) {
		reject();
	}
	m_tracking = m_inside = false;
	update();
}

void TransferDialog::paintEvent(QPaintEvent *) {
	Pixels pixels(width(), height());
	auto *canvas = &pixels.c;
	pl_fill(canvas, 0, 0, width() - 1, height() - 1, GRAY(0xD));
	const bool stopped = m_stopped.load();
	Text heading(stopped ? "Stopping…" : m_preparing ? "Preparing to transfer files…" :
		m_operation + " “" + m_file + "”", 390, PL_FONT_SYSTEM);
	Text destination("To: " + m_destination, 390, PL_FONT_SYSTEM);
	pl_text(canvas, heading.t, 20, 30, C_BLACK);
	pl_text(canvas, destination.t, 20, 54, C_BLACK);
	pl_progress_paint(canvas, 20, 75, 390,
		m_total ? static_cast<double>(m_completed) / m_total : 0, pl_accent_current());
	Text detail(m_preparing ? "Counting files…" :
		QString::number(m_total ? static_cast<int>(std::min(100.0, m_completed * 100.0 / m_total)) : 0) +
			"% complete", 390, PL_FONT_SYSTEM);
	pl_text(canvas, detail.t, 20, 112, C_BLACK);
	Text stop("Stop", 70, PL_FONT_SYSTEM);
	pl_button_paint(canvas, m_stop.x(), m_stop.y(), m_stop.width(), stop.t,
		stopped ? PL_BUTTON_DISABLED : m_tracking && m_inside ? PL_BUTTON_PRESSED : 0);
	QPainter painter(this);
	pixels.blit(painter);
}
