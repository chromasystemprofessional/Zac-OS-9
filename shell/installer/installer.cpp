#include "installer.h"
#include "power.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QWheelEvent>

#include "logo.h"
#include "panelkit.h"
#include "pixels.h"
#include "settings.h"
#include "udisks.h"

static constexpr int W = 520, H = 440;
static constexpr int MARGIN = 16;
static constexpr int BTN_W = 80, BTN_H = 20;
static constexpr int BTN_Y = H - MARGIN - BTN_H;
static constexpr int BTN_R = W - MARGIN;    /* right edge of right button */
static constexpr uint32_t FACE = GRAY(0xD); /* standard Platinum panel face */
static constexpr int LINE_H = 14;           /* body text line height */
/* Select: the disks, then the map of the chosen one, its words beside it. */
static constexpr int MAP_TOP = 172, MAP_W = 190, RX = MARGIN + MAP_W + 16;

/* ---- DiskEntry ------------------------------------------------------------ */

QString DiskEntry::label() const {
	/* Only the drive's make and model, as the Mac OS installer names a disk: no
	 * size, no device name. A drive that reports none is just a disk. */
	const QString disk = model.isEmpty() ? QStringLiteral("Hard Disk") : model;
	if (isDisk) {
		return disk;
	}
	/* A partition prepared in Disk Setup: its name, and the disk it's on. */
	return (partLabel.isEmpty() ? "Partition " + device.section('/', -1) : "\u201c" + partLabel + "\u201d")
		+ " on " + disk;
}

/* zacos9-install, as root through pkexec; ZACOS9_INSTALL_HELPER runs
 * another one directly instead, for trying the window out. */
static void startHelper(QProcess *p, const QStringList &args) {
	const QString test = qEnvironmentVariable("ZACOS9_INSTALL_HELPER");
	if (!test.isEmpty()) {
		p->start(test, args);
	} else {
		p->start("pkexec", QStringList{ QStringLiteral("/usr/libexec/zacos9/zacos9-install") } + args);
	}
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
	m_diskList.frame = QRect(MARGIN, 52, W - 2 * MARGIN, 5 * PL_LIST_ROW_H + 2);
	m_uefi = QFileInfo::exists(qEnvironmentVariable("ZACOS9_FIRMWARE_EFI", "/sys/firmware/efi"));
	m_where = PanelRadios();
	m_where.add("Erase the whole disk", QPoint(MARGIN, 144));
	m_where.add("Next to what's on it", QPoint(MARGIN + 200, 144));
	m_where.hint = "Erase the whole disk for ZacOS 9 alone, or keep what's on it (Windows, Haiku...) "
		"and give ZacOS 9 room beside it.";
	m_where.changed = [this](int) { chooseTarget(false); };
	m_map.frame = QRect(MARGIN, MAP_TOP, MAP_W, BTN_Y - 14 - MAP_TOP);
	m_map.dragged = [this](int part, qulonglong at) { dragLine(part, at); };
	enumerateDisks();
	m_diskList.picked = [this](int) { chooseTarget(true); };
	chooseTarget(true);
	/* The name the installed disk goes by in the Finder (Mac OS allowed
	 * 27 characters; ':' separated a Mac path's parts, '/' a Unix one's). */
	m_diskName.rect = QRect(RX + 78, BTN_Y - 14 - PL_EDIT_H, W - MARGIN - RX - 78, PL_EDIT_H);
	m_diskName.accepts = [](QChar ch) { return ch.isPrint() && ch != ':' && ch != '/'; };
	m_diskName.edited = [this] {
		if (m_diskName.text.size() > 27) {
			m_diskName.setText(m_diskName.text.left(27));
		}
		/* ZacOS's block in the map goes by it too. */
		for (DiskOp &op : m_ops) {
			if (op.kind == DiskOp::Create && op.fsType == "ext4") {
				op.label = m_diskName.text.trimmed();
			}
		}
		if (beside()) {
			m_map.set(plannedTarget());
		}
		update();
	};
	if (m_diskName.text.isEmpty()) {
		m_diskName.setText(QStringLiteral("Zacintosh HD"));
	}
	m_prepare = PanelButton("Prepare Disk\u2026", QRect(MARGIN, BTN_Y, 112, BTN_H));
	m_prepare.hint = "Opens Disk Setup to partition or erase a disk first: to keep Windows beside ZacOS 9, "
		"make a ZacOS (ext4) partition there, then choose it here.";
	m_prepare.clicked = [this] { prepareDisk(); };
	m_host.edits = { &m_diskName };
	m_host.buttons = { &m_prepare };
	m_host.radios = { &m_where };
	m_host.setFocus(&m_diskName);
	m_diskName.selectAll();
	update();
}

const DiskEntry *InstallerWindow::selectedDisk() const {
	const int sel = m_diskList.state.selected;
	return sel >= 0 && sel < static_cast<int>(m_disks.size()) ? &m_disks[sel] : nullptr;
}

/* Install alone; or, for a disk with ZacOS 9 on it, Update (Reinstall at
 * the same version) as the default button and Erase & Install beside it. */
void InstallerWindow::updateSelectButtons() {
	const DiskEntry *d = selectedDisk();
	const bool has = d && !d->zacosPart.isEmpty();
	m_install = PanelButton(!has ? "Install" : d->zacosCmp == 0 ? "Reinstall" : "Update",
		QRect(BTN_R - BTN_W, BTN_Y, BTN_W, BTN_H), true);
	m_erase = PanelButton("Erase & Install", QRect(BTN_R - BTN_W - 12 - 112, BTN_Y, 112, BTN_H));
	m_where.enabled = d && d->isDisk && !has;
	update();
}

/* ---- next to what's on a disk (issue #42) ---- */

bool InstallerWindow::beside() const {
	const DiskEntry *d = selectedDisk();
	return d && d->isDisk && d->zacosPart.isEmpty() && m_where.selected == 1;
}

DiskDrive InstallerWindow::plannedTarget() const {
	DiskDrive d = m_target;
	for (const DiskOp &op : m_ops) {
		d = planned(d, op);
	}
	return d;
}

/* ZacOS's new partition in the drive as planned, or -1. */
int InstallerWindow::zacosPart(const DiskDrive &d) const {
	for (int i = 0; i < d.parts.size(); i++) {
		if (d.parts[i].block.isEmpty() && d.parts[i].type == "ext4") {
			return i;
		}
	}
	return -1;
}

/* The drives as UDisks shows them; nothing needs a password to look. */
void InstallerWindow::readDrives() {
	qDBusRegisterMetaType<UDisksInterfaces>();
	qDBusRegisterMetaType<UDisksObjects>();
	QDBusMessage m = QDBusMessage::createMethodCall(qEnvironmentVariable("ZACOS9_UDISKS_SERVICE",
		"org.freedesktop.UDisks2"), "/org/freedesktop/UDisks2", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
	const QDBusMessage reply = QDBusConnection::systemBus().call(m, QDBus::Block, 10000);
	m_drives = reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()
		? parseDrives(qdbus_cast<UDisksObjects>(reply.arguments().first())) : QList<DiskDrive>();
}

/* The chosen disk in the map: as it is, or with ZacOS next to what's there.
 * A disk just chosen starts at "next to what's on it" if anything is: an
 * Install clicked in haste then keeps it. */
void InstallerWindow::chooseTarget(bool chosen) {
	const DiskEntry *d = selectedDisk();
	m_target = DiskDrive();
	m_ops.clear();
	m_why.clear();
	m_toMeasure.clear();
	for (const DiskDrive &drive : m_drives) {
		bool mine = d && drive.device == d->device;
		for (const DiskPart &p : drive.parts) {
			mine = mine || (d && !d->isDisk && p.device == d->device);
		}
		if (mine) {
			m_target = drive;
		}
	}
	/* The live desktop's own volumes on it close for the install. */
	for (DiskPart &p : m_target.parts) {
		QStringList kept;
		for (const QString &where : p.mountPoints) {
			if (!where.startsWith("/media/") && !where.startsWith("/run/media/")) {
				kept << where;
			}
		}
		p.mountPoints = kept;
	}
	if (chosen) {
		m_where.selected = m_target.parts.isEmpty() ? 0 : 1;
	}
	m_map.enabled = false;
	m_map.set(m_target);
	m_map.select(-1, false);
	if (d && !d->isDisk) {
		/* A partition: that one's chosen in the map. */
		for (int i = 0; i < m_target.parts.size(); i++) {
			if (m_target.parts[i].device == d->device) {
				m_map.select(m_map.rowOfPart(i), false);
			}
		}
	}
	if (beside()) {
		if (m_target.block.isEmpty()) {
			m_why = "This disk couldn't be read (UDisks isn't answering).";
		} else {
			/* How small each partition that can shrink can be, then the plan. */
			for (const DiskPart &p : m_target.parts) {
				if (!p.block.isEmpty() && resizable(p.type) && !p.inUse() && !p.logical) {
					m_toMeasure << p.device;
				}
			}
			measureNext();
		}
	}
	updateSelectButtons();
}

void InstallerWindow::measureNext() {
	if (m_measure) {
		return;
	}
	if (m_toMeasure.isEmpty()) {
		planBeside();
		return;
	}
	const QString device = m_toMeasure.takeFirst();
	m_measure = new QProcess(this);
	connect(m_measure, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, device] {
		for (const QString &line : QString::fromUtf8(m_measure->readAllStandardOutput()).split('\n')) {
			for (DiskPart &p : m_target.parts) {
				if (p.device == device && line.startsWith("min-size ")) {
					p.minSize = line.mid(9).trimmed().toULongLong();
				}
			}
			if (line.startsWith("error ") && m_why.isEmpty()) {
				m_why = line.mid(6).trimmed() + "."; /* kept in case nothing else makes room */
			}
		}
		m_measure->deleteLater();
		m_measure = nullptr;
		if (beside()) {
			measureNext();
		}
	});
	connect(m_measure, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			m_measure->deleteLater();
			m_measure = nullptr;
			m_toMeasure.clear();
			planBeside();
		}
	});
	startHelper(m_measure, { "--min-size", device });
	update();
}

void InstallerWindow::planBeside() {
	const QString measured = m_why;
	const BesidePlan plan = besidePlan(m_target, m_uefi, m_diskName.text.trimmed());
	m_ops = plan.ops;
	/* A partition that couldn't be measured says why, if that's what's in the way. */
	m_why = plan.why.isEmpty() ? QString() : measured.isEmpty() ? plan.why : plan.why + " " + measured;
	const DiskDrive d = plannedTarget();
	m_map.enabled = m_why.isEmpty();
	m_map.set(d);
	m_map.select(m_map.rowOfPart(zacosPart(d)), false);
	updateSelectButtons();
}

/* A line dragged: anything but a partition already there made bigger
 * (zacos9-install only shrinks), and ZacOS kept at 16 GB at least. */
void InstallerWindow::dragLine(int part, qulonglong at) {
	QList<DiskOp> ops = m_ops;
	if (!moveBoundary(m_target, &ops, part, at) || besideSteps(m_target, ops).isEmpty()) {
		return;
	}
	DiskDrive d = m_target;
	for (const DiskOp &op : ops) {
		d = planned(d, op);
	}
	const int z = zacosPart(d);
	if (z < 0 || d.parts[z].size < ZacosLeast) {
		return;
	}
	m_ops = ops;
	m_map.set(d);
	update();
}

/* What happens to the disk, in words, beside the map. */
QString InstallerWindow::besideText() const {
	if (m_measure || !m_toMeasure.isEmpty()) {
		return "Looking at what's on the disk\u2026";
	}
	if (!m_why.isEmpty()) {
		return m_why;
	}
	const DiskDrive d = plannedTarget();
	const int z = zacosPart(d);
	if (z < 0) {
		return QString();
	}
	QString text = QString("ZacOS 9 gets %1 (the new partition). Drag the line above or below it for more or "
		"less room; it needs 16 GB at least.").arg(sizeText(d.parts[z].size));
	DiskDrive at = m_target;
	for (const DiskOp &op : m_ops) {
		if (op.kind == DiskOp::Resize) {
			const DiskPart &p = at.parts[op.part];
			text += QString(" %1 will be made smaller, %2 to %3: its files stay, but back up anything "
				"important first.").arg(p.label.isEmpty() ? p.device.section('/', -1) : "\u201c" + p.label + "\u201d",
				sizeText(p.size), sizeText(op.size));
		} else if (op.kind == DiskOp::Create && !op.partType.isEmpty()) {
			text += QString(" A %1 partition is made for starting up.").arg(sizeText(op.size));
		}
		at = planned(at, op);
	}
	return text;
}

/* Disk Setup's Partition tab, to make room for ZacOS 9 (a partition beside
 * Windows, say); the list is read again when it closes. */
void InstallerWindow::prepareDisk() {
	if (m_diskSetup) {
		return;
	}
	m_diskSetup = new QProcess(this);
	connect(m_diskSetup, &QProcess::finished, this, [this] {
		m_diskSetup->deleteLater();
		m_diskSetup = nullptr;
		enumerateDisks();
		chooseTarget(true);
	});
	connect(m_diskSetup, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			m_diskSetup->deleteLater();
			m_diskSetup = nullptr;
		}
	});
	m_diskSetup->start("zacos9-disksetup", { "--partition" });
}

void InstallerWindow::showInstalling(const QStringList &args, bool updating) {
	m_screen = Screen::Installing;
	setCursor(Qt::ArrowCursor); /* it may have been on a line in the map */
	m_updating = updating;
	m_progress = 0.0;
	m_step = 0;
	m_status = "Preparing...";
	update();

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
	startHelper(m_installProcess, args);
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
	readDrives();
	const QString live = liveMediumDisk();
	/* -P: machine-readable pairs; -e 1,7,11: leave out RAM disks (1), loop
	 * devices (7) and optical drives (11) */
	QProcess p;
	p.start("lsblk", { "-P", "-n", "-e", "1,7,11", "-o", "NAME,TYPE,SIZE,FSTYPE,MODEL,PKNAME,LABEL" });
	QMap<QString, QString> models; /* each disk's, for its partitions */
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
		/* Whole drives, and ext4 partitions on them (made for ZacOS in Disk
		 * Setup): only that partition is formatted, the rest of its disk
		 * kept. Never the live medium's drive. */
		if (type == "disk") {
			models[name] = kv.value("MODEL").trimmed();
		}
		const bool part = type == "part" && kv.value("FSTYPE") == "ext4";
		if (name.isEmpty() || (type != "disk" && !part) ||
				(!live.isEmpty() && (name == live || kv.value("PKNAME") == live))) continue;
		DiskEntry d;
		d.device = "/dev/" + name;
		d.size   = kv.value("SIZE");
		d.isDisk = !part;
		d.model  = part ? models.value(kv.value("PKNAME")) : kv.value("MODEL").trimmed();
		d.partLabel = part ? kv.value("LABEL") : QString();
		m_disks.push_back(d);
	}
	/* ZacOS 9 already on a disk: the helper looks, read-only (it needs root
	 * to open a disk). */
	QProcess version;
	version.start("dpkg-query", { "-W", "-f=${Version}", "zacos9" });
	waitSync(&version, 3000);
	m_liveVersion = QString::fromUtf8(version.readAllStandardOutput()).trimmed();
	QProcess detect;
	startHelper(&detect, { "--detect" });
	waitSync(&detect, 30000);
	for (const QByteArray &raw : detect.readAllStandardOutput().split('\n')) {
		const QStringList f = QString::fromUtf8(raw).trimmed().split(' ');
		if (f.size() < 5 || f[0] != "zacos9") continue;
		for (DiskEntry &d : m_disks) {
			if (d.device == f[1] && d.zacosPart.isEmpty()) {
				d.zacosPart = f[2];
				d.zacosVersion = f[3];
				d.zacosName = f.mid(4).join(' ');
				for (int cmp : { -1, 1 }) {
					QProcess v;
					v.start("dpkg", { "--compare-versions", d.zacosVersion, cmp < 0 ? "lt" : "gt", m_liveVersion });
					if (waitSync(&v, 3000) && v.exitCode() == 0) d.zacosCmp = cmp;
				}
			}
		}
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
		if (!d.zacosVersion.isEmpty()) {
			label += "  \xe2\x80\x94  ZacOS 9 " + d.zacosVersion;
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
		"This will install ZacOS 9 on a hard disk, or update the ZacOS 9 "
		"already on one, keeping its accounts, files and settings. "
		"ZacOS 9 can have a whole disk, or go next to what's already on one "
		"(Windows, say). Either way, back up any files you want to keep first.",
		MARGIN, 148, W - 2 * MARGIN);

	m_continue.paint(c);
}

void InstallerWindow::paintSelect(pl_canvas *c) {
	panelText(c, "Select a Destination", MARGIN, MARGIN + 13);
	panelText(c, "Select the disk to install ZacOS 9 on.", MARGIN, MARGIN + 13 + 16, PL_FONT_VIEWS);

	m_diskList.paint(c, true);
	const DiskEntry *d = selectedDisk();
	const bool has = d && !d->zacosPart.isEmpty();
	if (d && d->isDisk && !has) {
		m_where.paint(c);
	}
	if (!m_target.block.isEmpty()) {
		m_map.paint(c);
	}

	panelLabel(c, "Disk name:", m_diskName.rect.left() - 8, m_diskName.rect.top() + 15);
	m_host.paintControls(c, FACE);
	/* What happens, beside the map. */
	const int tw = W - MARGIN - RX;
	const int ty = MAP_TOP + 12;
	if (m_disks.empty()) {
		panelText(c, "No suitable disks were found.", RX, ty);
	} else if (has) {
		const QString have = "This disk has ZacOS 9 " + d->zacosVersion + " (\xe2\x80\x9c" +
			d->zacosName + "\xe2\x80\x9d). ";
		bodyText(c, d->zacosCmp > 0
			? have + "That is newer than this installer's " + m_liveVersion +
			  "; Erase & Install would erase it and everything on the disk."
			: have + (d->zacosCmp == 0 ? "Reinstall" : "Update") +
			  " keeps its accounts, files and settings; Erase & Install erases everything on it.",
			RX, ty, tw);
	} else if (beside()) {
		bodyText(c, besideText(), RX, ty, tw);
	} else if (d) {
		/* U+26A0 in UTF-8 */
		QStringList lost;
		for (const DiskPart &p : m_target.parts) {
			if (d->isDisk || p.device == d->device) {
				lost << (p.label.isEmpty() ? (p.efi ? QString("EFI System") : p.device.section('/', -1))
					: "\u201c" + p.label + "\u201d");
			}
		}
		const QString what = d->isDisk ? m_diskList.items.value(m_diskList.state.selected) : d->label();
		bodyText(c, "\xe2\x9a\xa0 " + what + " will be completely erased" +
			(lost.isEmpty() ? QString(".") : ", with everything on " + lost.join(", ") + "."), RX, ty, tw);
	}

	const bool ready = !beside() || (!m_measure && m_toMeasure.isEmpty() && m_why.isEmpty() && !m_ops.isEmpty());
	m_install.enabled = d && ready && (has ? d->zacosCmp <= 0 : !m_diskName.text.trimmed().isEmpty());
	m_install.paint(c);
	if (has) {
		m_erase.enabled = !m_diskName.text.trimmed().isEmpty();
		m_erase.paint(c);
	}
}

void InstallerWindow::paintInstalling(pl_canvas *c) {
	panelText(c, m_updating ? "Updating ZacOS 9\xE2\x80\xA6" : "Installing ZacOS 9\xE2\x80\xA6",
		MARGIN, MARGIN + 13);

	paintLogo(c, W / 2, 118, 48);

	panelText(c, m_status, MARGIN, 162);

	pl_progress_paint(c, MARGIN, 182, W - 2 * MARGIN, m_progress,
		pl_accent_current());
}

void InstallerWindow::paintDone(pl_canvas *c) {
	paintLogo(c, W / 2, 88, 64);

	Text ttl(m_updating ? "Update Complete" : "Installation Complete", W, PL_FONT_SYSTEM);
	const int ttl_w = ttl.t->ink_l < 0 ? 0 : ttl.t->ink_r - ttl.t->ink_l + 1;
	pl_text(c, ttl.t, (W - ttl_w) / 2, 146, C_BLACK);

	bodyText(c, m_updating
		? "ZacOS 9 has been updated; your accounts, files and settings are as they were. "
		  "Remove the installer disk and click Restart."
		: "ZacOS 9 has been installed. Remove the installer disk "
		  "and click Restart to start your new system.",
		MARGIN, 166, W - 2 * MARGIN);
	if (!m_restartError.isEmpty()) {
		bodyText(c, m_restartError, MARGIN, BTN_Y - 36, W - 2 * MARGIN);
	}

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
		if (m_map.press(pos)) {
			update();
			break;
		}
		m_diskList.press(pos);
		m_install.press(pos);
		if (selectedDisk() && !selectedDisk()->zacosPart.isEmpty()) {
			m_erase.press(pos);
		}
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
	const QPoint pos = e->position().toPoint();
	if (m_screen == Screen::Select) {
		setCursor(m_map.onLine(pos) ? Qt::SizeVerCursor : Qt::ArrowCursor);
	}
	if (m_screen == Screen::Select &&
			(m_map.move(pos) | m_diskList.move(pos) | m_host.hostMove(e))) {
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
	if (m_map.release() | m_diskList.release() | (m_screen == Screen::Select && m_host.hostRelease(e))) {
		update();
	}
	switch (m_screen) {
	case Screen::Welcome:
		if (m_continue.release(pos)) {
			showSelect();
		}
		break;
	case Screen::Select:
		if (const DiskEntry *d = selectedDisk()) {
			const bool has = !d->zacosPart.isEmpty();
			const bool erase = has && m_erase.release(pos);
			if (m_install.release(pos) && m_install.enabled) {
				if (has) {
					showInstalling({ "--update", d->zacosPart }, true);
				} else if (beside()) {
					showInstalling(QStringList{ "--beside", d->device, m_diskName.text.trimmed() } +
						besideSteps(m_target, m_ops), false);
				} else {
					showInstalling({ d->device, m_diskName.text.trimmed() }, false);
				}
			} else if (erase && m_erase.enabled) {
				showInstalling({ d->device, m_diskName.text.trimmed() }, false);
			}
		}
		break;
	case Screen::Done:
		if (m_restart.release(pos)) {
			/* systemctl, not reboot: /usr/sbin isn't on a user's PATH (issue #38). */
			m_restartError = systemPower(Power::Restart);
			update();
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
	/* Return is Continue's, the default button. */
	if (m_screen == Screen::Welcome && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)) {
		showSelect();
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
