#include "installer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>

#include "logo.h"
#include "panelkit.h"
#include "pixels.h"
#include "settings.h"

static constexpr int W = 440, H = 290;
static constexpr int MARGIN = 16;
static constexpr int BTN_W = 80, BTN_H = 20;
static constexpr int BTN_Y = H - MARGIN - BTN_H;
static constexpr int BTN_R = W - MARGIN;    /* right edge of right button */
static constexpr uint32_t FACE = GRAY(0xD); /* standard Platinum panel face */
static constexpr int LINE_H = 14;           /* body text line height */

/* ---- DiskEntry ------------------------------------------------------------ */

QString DiskEntry::label() const {
	if (model.isEmpty()) {
		return device + "  " + size;
	}
	return model + "  " + size + "  (" + device + ")";
}

/* ---- construction -------------------------------------------------------- */

InstallerWindow::InstallerWindow() {
	setWindowTitle("ZacOS 9 Installer");
	setFixedSize(W, H);
	showWelcome();
}

void InstallerWindow::showWelcome() {
	m_screen = Screen::Welcome;
	m_continue = PanelButton("Continue",
		QRect(BTN_R - BTN_W, BTN_Y, BTN_W, BTN_H), true);
	update();
}

void InstallerWindow::showSelect() {
	m_screen = Screen::Select;
	enumerateDisks();
	m_diskList.frame = QRect(MARGIN, 90, W - 2 * MARGIN, 130);
	m_install = PanelButton("Install",
		QRect(BTN_R - BTN_W, BTN_Y, BTN_W, BTN_H), true);
	update();
}

void InstallerWindow::showInstalling(const QString &device) {
	m_screen = Screen::Installing;
	m_progress = 0.0;
	m_step = 0;
	m_status = "Preparing...";
	update();

	static const QString helper = QStringLiteral("/usr/libexec/zacos9/zacos9-install");
	m_installProcess = new QProcess(this);
	m_installProcess->setProcessChannelMode(QProcess::MergedChannels);
	connect(m_installProcess, &QProcess::readyRead, this, [this] {
		while (m_installProcess->canReadLine()) {
			const QString line = QString::fromUtf8(m_installProcess->readLine()).trimmed();
			if (line.startsWith("step ")) {
				/* "step N Label..." */
				const int sp = line.indexOf(' ', 5);
				m_step = line.mid(5, sp > 5 ? sp - 5 : -1).toInt();
				m_status = sp > 0 ? line.mid(sp + 1) : QString();
				m_progress = std::max(0.0, (m_step - 1) / 6.0);
			} else if (line == "done") {
				m_progress = 1.0;
				update();
				showDone();
				return;
			} else if (line.startsWith("error ")) {
				m_status = "Error: " + line.mid(6);
			}
			update();
		}
	});
	connect(m_installProcess,
		QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		this, [this](int code, QProcess::ExitStatus) {
			if (m_screen == Screen::Installing && code != 0) {
				m_status = "Installation failed (exit " + QString::number(code) + ").";
				m_progress = 0.0;
				update();
			}
		});
	m_installProcess->start("pkexec", { helper, device });
}

void InstallerWindow::showDone() {
	m_screen = Screen::Done;
	m_restart = PanelButton("Restart",
		QRect(BTN_R - BTN_W, BTN_Y, BTN_W, BTN_H), true);
	update();
}

/* ---- disk enumeration ---------------------------------------------------- */

/* True if the process finished; false (and killed) on timeout. */
static bool waitSync(QProcess *p, int ms) {
	bool done = false;
	QObject::connect(p, &QProcess::finished,
		[&done] { done = true; });
	QObject::connect(p, &QProcess::errorOccurred,
		[&done](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) { done = true; }
		});
	QElapsedTimer t;
	t.start();
	while (!done && t.elapsed() < ms) {
		QCoreApplication::processEvents(
			QEventLoop::ExcludeUserInputEvents |
			QEventLoop::WaitForMoreEvents, 100);
	}
	if (!done) { p->kill(); p->waitForFinished(500); return false; }
	return true;
}

void InstallerWindow::enumerateDisks() {
	m_disks.clear();
	/* -e 7,11: exclude loop (7) and optical (11) devices */
	QProcess p;
	p.start("lsblk", { "-d", "-n", "-e", "7,11", "-o", "NAME,SIZE,MODEL" });
	waitSync(&p, 5000);
	for (const QByteArray &raw : p.readAllStandardOutput().split('\n')) {
		const QString line = QString::fromUtf8(raw).simplified();
		if (line.isEmpty()) {
			continue;
		}
		const QStringList parts = line.split(' ', Qt::SkipEmptyParts);
		if (parts.isEmpty()) {
			continue;
		}
		DiskEntry d;
		d.device = "/dev/" + parts[0];
		if (parts.size() > 1) {
			d.size = parts[1];
		}
		if (parts.size() > 2) {
			d.model = parts.mid(2).join(' ');
		}
		m_disks.push_back(d);
	}
	QStringList labels;
	for (const DiskEntry &d : m_disks) {
		labels << d.label();
	}
	m_diskList.setItems(labels);
}

/* ---- painting ------------------------------------------------------------ */

void InstallerWindow::paintLogo(pl_canvas *c, int cx, int cy, int size) {
	const uint32_t *logo = logo_pixels();
	const int scale = size / PL_LOGO_SIZE;
	const int ox = cx - size / 2, oy = cy - size / 2;
	for (int y = 0; y < PL_LOGO_SIZE * scale; y++) {
		for (int x = 0; x < PL_LOGO_SIZE * scale; x++) {
			uint32_t v = logo[(y / scale) * PL_LOGO_SIZE + x / scale];
			if (v >> 24) {
				pl_put(c, ox + x, oy + y, v);
			}
		}
	}
}

/* Draw word-wrapped body text; returns the y of the next line. */
static int bodyText(pl_canvas *c, const QString &text, int x, int y, int maxW) {
	for (const QString &line : panelWrap(text, maxW, PL_FONT_SYSTEM)) {
		panelText(c, line, x, y);
		y += LINE_H;
	}
	return y;
}

void InstallerWindow::paintWelcome(pl_canvas *c) {
	paintLogo(c, W / 2, 68, 64);

	/* Centred heading */
	Text ttl("ZacOS 9 Installer", W, PL_FONT_SYSTEM);
	const int ttl_w = ttl.t->ink_l < 0 ? 0 : ttl.t->ink_r - ttl.t->ink_l + 1;
	pl_text(c, ttl.t, (W - ttl_w) / 2, 122, C_BLACK);

	bodyText(c,
		"This will install ZacOS 9 on a hard disk. "
		"The selected disk will be completely erased. "
		"Back up any files you want to keep before continuing.",
		MARGIN, 148, W - 2 * MARGIN);

	m_continue.paint(c);
}

void InstallerWindow::paintSelect(pl_canvas *c) {
	panelText(c, "Select a Destination", MARGIN, MARGIN + 13);

	bodyText(c,
		"Where would you like to install ZacOS 9? "
		"All data on the selected disk will be erased.",
		MARGIN, MARGIN + 13 + 18, W - 2 * MARGIN);

	m_diskList.paint(c, true);

	if (m_disks.empty()) {
		panelText(c, "No suitable disks were found.",
			MARGIN, m_diskList.frame.bottom() + 14);
	}

	m_install.enabled = m_diskList.state.selected >= 0;
	m_install.paint(c);
}

void InstallerWindow::paintInstalling(pl_canvas *c) {
	panelText(c, "Installing ZacOS 9\xE2\x80\xA6", MARGIN, MARGIN + 13);

	paintLogo(c, W / 2, 118, 48);

	panelText(c, m_status, MARGIN, 162);

	pl_progress_paint(c, MARGIN, 182, W - 2 * MARGIN, m_progress,
		pl_accent_current());
}

void InstallerWindow::paintDone(pl_canvas *c) {
	paintLogo(c, W / 2, 88, 64);

	Text ttl("Installation Complete", W, PL_FONT_SYSTEM);
	const int ttl_w = ttl.t->ink_l < 0 ? 0 : ttl.t->ink_r - ttl.t->ink_l + 1;
	pl_text(c, ttl.t, (W - ttl_w) / 2, 146, C_BLACK);

	bodyText(c,
		"ZacOS 9 has been installed. Remove the installer disk "
		"and click Restart to start your new system.",
		MARGIN, 166, W - 2 * MARGIN);

	m_restart.paint(c);
}

void InstallerWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	/* Separator above the button row */
	pl_fill(c, 0, BTN_Y - 8, W - 1, BTN_Y - 8, GRAY(0x8));

	switch (m_screen) {
	case Screen::Welcome:    paintWelcome(c);    break;
	case Screen::Select:     paintSelect(c);     break;
	case Screen::Installing: paintInstalling(c); break;
	case Screen::Done:       paintDone(c);       break;
	}

	QPainter p(this);
	px.blit(p);
}

/* ---- input --------------------------------------------------------------- */

void InstallerWindow::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	switch (m_screen) {
	case Screen::Welcome:
		m_continue.press(pos);
		update();
		break;
	case Screen::Select:
		m_diskList.press(pos);
		m_install.press(pos);
		update();
		break;
	case Screen::Done:
		m_restart.press(pos);
		update();
		break;
	default:
		break;
	}
}

void InstallerWindow::mouseReleaseEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	switch (m_screen) {
	case Screen::Welcome:
		if (m_continue.release(pos)) {
			showSelect();
		}
		break;
	case Screen::Select:
		if (m_install.release(pos)) {
			const int sel = m_diskList.state.selected;
			if (sel >= 0 && sel < static_cast<int>(m_disks.size())) {
				showInstalling(m_disks[sel].device);
			}
		}
		break;
	case Screen::Done:
		if (m_restart.release(pos)) {
			QProcess::startDetached("reboot", {});
		}
		break;
	default:
		break;
	}
}

void InstallerWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		if (m_screen != Screen::Installing) {
			close();
		}
		return;
	}
	if (m_screen == Screen::Select) {
		if (m_diskList.key(e->key(), e->text())) {
			update();
		}
	}
}
