#include "installer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QWheelEvent>

#include "logo.h"
#include "panelkit.h"
#include "pixels.h"
#include "settings.h"

static constexpr int W = 440, H = 310;
static constexpr int MARGIN = 16;
static constexpr int BTN_W = 80, BTN_H = 20;
static constexpr int BTN_Y = H - MARGIN - BTN_H;
static constexpr int BTN_R = W - MARGIN;    /* right edge of right button */
static constexpr uint32_t FACE = GRAY(0xD); /* standard Platinum panel face */
static constexpr int LINE_H = 14;           /* body text line height */

/* ---- DiskEntry ------------------------------------------------------------ */

QString DiskEntry::label() const {
	/* Only the drive's make and model, as the Mac OS installer names a disk: no
	 * size, no device name. A drive that reports none is just a disk. */
	return model.isEmpty() ? QStringLiteral("Hard Disk") : model;
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
	/* Frame first: setItems() sizes each row's text to the frame width. */
	m_diskList.frame = QRect(MARGIN, 68, W - 2 * MARGIN, 140);
	enumerateDisks();
	m_install = PanelButton("Install",
		QRect(BTN_R - BTN_W, BTN_Y, BTN_W, BTN_H), true);
	/* The name the installed disk goes by in the Finder (Mac OS allowed
	 * 27 characters; ':' separated a Mac path's parts, '/' a Unix one's). */
	m_diskName.rect = QRect(MARGIN + 82, 232, 220, PL_EDIT_H);
	m_diskName.accepts = [](QChar ch) { return ch.isPrint() && ch != ':' && ch != '/'; };
	m_diskName.edited = [this] {
		if (m_diskName.text.size() > 27) {
			m_diskName.setText(m_diskName.text.left(27));
		}
		update();
	};
	if (m_diskName.text.isEmpty()) {
		m_diskName.setText(QStringLiteral("Zacintosh HD"));
	}
	m_host.edits = { &m_diskName };
	m_host.setFocus(&m_diskName);
	m_diskName.selectAll();
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
	/* Separate channels: the helper's protocol is on stdout; its tools'
	 * chatter on stderr (where GRUB prints a bare "done" of its own) is
	 * only kept to show beside a failure. */
	connect(m_installProcess, &QProcess::readyReadStandardError, this, [this] {
		for (const QByteArray &raw : m_installProcess->readAllStandardError().split('\n')) {
			const QString line = QString::fromUtf8(raw).trimmed();
			if (!line.isEmpty()) {
				m_lastOutput = line;
			}
		}
	});
	connect(m_installProcess, &QProcess::readyReadStandardOutput, this, [this] {
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
				m_lastOutput.clear();
			} else if (!line.isEmpty()) {
				m_lastOutput = line;
			}
			update();
		}
	});
	connect(m_installProcess,
		QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
		this, [this](int code, QProcess::ExitStatus) {
			if (m_screen == Screen::Installing && code != 0 && !m_status.startsWith("Error: ")) {
				m_status = "Installation failed (exit " + QString::number(code) + ")";
				if (!m_lastOutput.isEmpty()) {
					m_status += ": " + m_lastOutput;
				}
				m_progress = 0.0;
				update();
			}
		});
	m_installProcess->start("pkexec", { helper, device, m_diskName.text.trimmed() });
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

/* The drive ZacOS 9 is running from (the installer USB stick or disc), by name
 * ("sdb"), or nothing: it is not somewhere to install to. */
static QString liveMediumDisk() {
	for (const char *where : { "/run/live/medium", "/lib/live/mount/medium", "/cdrom" }) {
		QProcess find;
		find.start("findmnt", { "-n", "-o", "SOURCE", where });
		if (!waitSync(&find, 3000)) continue;
		const QString source = QString::fromUtf8(find.readAllStandardOutput()).trimmed();
		if (!source.startsWith("/dev/")) continue;
		QProcess parent;
		parent.start("lsblk", { "-n", "-o", "PKNAME", source });
		if (!waitSync(&parent, 3000)) continue;
		const QString name = QString::fromUtf8(parent.readAllStandardOutput()).split('\n').value(0).trimmed();
		/* A partition names its disk; a whole disk (a disc, or an image written
		 * straight to a stick) is its own. */
		return name.isEmpty() ? source.section('/', -1) : name;
	}
	return QString();
}

void InstallerWindow::enumerateDisks() {
	m_disks.clear();
	const QString live = liveMediumDisk();
	/* -P: machine-readable pairs; -e 1,7,11: leave out RAM disks (1), loop
	 * devices (7) and optical drives (11) */
	QProcess p;
	p.start("lsblk", { "-P", "-n", "-e", "1,7,11", "-o", "NAME,TYPE,SIZE,FSTYPE,MODEL" });
	waitSync(&p, 5000);
	for (const QByteArray &raw : p.readAllStandardOutput().split('\n')) {
		const QString line = QString::fromUtf8(raw).trimmed();
		if (line.isEmpty()) continue;
		/* Parse  KEY="value" ... pairs */
		QMap<QString, QString> kv;
		int i = 0;
		while (i < line.size()) {
			int eq = line.indexOf('=', i);
			if (eq < 0) break;
			const QString key = line.mid(i, eq - i).trimmed();
			i = eq + 1;
			if (i >= line.size() || line[i] != '"') break;
			++i;
			int close = line.indexOf('"', i);
			if (close < 0) break;
			kv[key] = line.mid(i, close - i);
			i = close + 2;
		}
		const QString type = kv.value("TYPE");
		const QString name = kv.value("NAME");
		/* Whole drives only: partitions are not offered. */
		if (name.isEmpty() || type != "disk" || name == live) continue;
		DiskEntry d;
		d.device = "/dev/" + name;
		d.size   = kv.value("SIZE");
		d.isDisk = true;
		d.model  = kv.value("MODEL").trimmed();
		m_disks.push_back(d);
	}
	/* Two drives of one make: tell them apart by number, nothing more. */
	QMap<QString, int> total, seen;
	for (const DiskEntry &d : m_disks) total[d.label()]++;
	QStringList labels;
	for (const DiskEntry &d : m_disks) {
		QString label = d.label();
		if (total[label] > 1) {
			label += " (" + QString::number(++seen[d.label()]) + ")";
		}
		labels << label;
	}
	m_diskList.setItems(labels);
}

/* ---- painting ------------------------------------------------------------ */

void InstallerWindow::paintLogo(pl_canvas *c, int cx, int cy, int /*size*/) {
	const int s = PL_LOGO_SIZE_HQ;
	pl_image_blend(c, cx - s / 2, cy - s / 2, logo_pixels_hq(), s, s, false);
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
		"All data on the selected disk will be erased. "
		"Back up any files you want to keep before continuing.",
		MARGIN, 148, W - 2 * MARGIN);

	m_continue.paint(c);
}

void InstallerWindow::paintSelect(pl_canvas *c) {
	panelText(c, "Select a Destination", MARGIN, MARGIN + 13);
	panelText(c, "Select the disk to install ZacOS 9 on.",
		MARGIN, MARGIN + 13 + 18);

	m_diskList.paint(c, true);

	const int warn_y = m_diskList.frame.bottom() + 14;
	panelLabel(c, "Disk name:", m_diskName.rect.left() - 8, m_diskName.rect.top() + 15);
	m_host.paintControls(c, FACE);
	if (m_disks.empty()) {
		panelText(c, "No suitable disks were found.", MARGIN, warn_y);
	} else {
		const int sel = m_diskList.state.selected;
		if (sel >= 0 && sel < static_cast<int>(m_disks.size())) {
			/* U+26A0 in UTF-8 */
			panelText(c,
				"\xe2\x9a\xa0 " + m_diskList.items.value(sel) + " will be completely erased.",
				MARGIN, warn_y);
		}
	}

	m_install.enabled = m_diskList.state.selected >= 0 &&
		m_diskList.state.selected < static_cast<int>(m_disks.size()) &&
		!m_diskName.text.trimmed().isEmpty();
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
		m_host.hostPress(e);
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

void InstallerWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_screen == Screen::Select &&
			(m_diskList.move(e->position().toPoint()) | m_host.hostMove(e))) {
		update();
	}
}

void InstallerWindow::wheelEvent(QWheelEvent *e) {
	if (m_screen == Screen::Select &&
			m_diskList.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void InstallerWindow::mouseReleaseEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_diskList.release() | (m_screen == Screen::Select && m_host.hostRelease(e))) {
		update();
	}
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
		/* Up and Down choose a disk; everything else is the name field's. */
		const bool listKey = e->key() == Qt::Key_Up || e->key() == Qt::Key_Down;
		if (listKey ? m_diskList.key(e->key(), e->text()) : m_host.hostKey(e)) {
			update();
		}
	}
}
