/*
 * zacos9-disksetup: Disk Setup (Mac OS 9's Drive Setup and Disk First Aid
 * together, gap plan 7.1 and 7.6), all through UDisks2, which asks for an
 * administrator's password (polkit).
 *
 *   First Aid  verify and repair volumes; the drives' health (SMART).
 *   Partition  a drive's layout in a partition map (diskmap.h): new, delete,
 *              erase and resize partitions (drag the line under one), a new
 *              partition table. Changes wait in a list until Apply, which
 *              first says what will be erased (plan.h for the rules).
 *
 * `zacos9-disksetup --partition` opens on Partition (the installer's
 * Prepare Disk…).
 */

#include <QApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QTime>
#include <QTimer>
#include <QWheelEvent>
#include <climits>
#include <csignal>
#include <cstring>
#include <functional>

#include "alert.h"
#include "diskmap.h"
#include "inuse.h"
#include "mountpolicy.h"
#include "panelkit.h"
#include "plan.h"
#include "platinumshell.h"
#include "settings.h"
#include "udisks.h"
#include "widgets.h"

namespace {

constexpr int W = 520, H = 520, M = 12, TOP = 10, PANE = TOP + PL_TAB_H;
/* Partition: the map on the left, the chosen partition's fields from RX. */
constexpr int MAP_W = 200, MAP_H = 250, RX = M + 8 + MAP_W + 16;
constexpr uint32_t FACE = GRAY(0xD), PANE_BG = GRAY(0xE);
const QString UDisks = "org.freedesktop.UDisks2";

QString service() {
	const QString env = qEnvironmentVariable("ZACOS9_UDISKS_SERVICE");
	return env.isEmpty() ? UDisks : env;
}

/* A UDisks method, its answer to `done` (polkit may ask for a password). */
void call(QObject *context, const QString &path, const QString &iface, const QString &method,
		const QVariantList &args, std::function<void(const QDBusMessage &)> done) {
	QDBusMessage m = QDBusMessage::createMethodCall(service(), path, iface, method);
	m.setArguments(args);
	m.setInteractiveAuthorizationAllowed(true);
	auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m, 60 * 60 * 1000), context);
	QObject::connect(w, &QDBusPendingCallWatcher::finished, context, [w, done] {
		w->deleteLater();
		done(w->reply());
	});
}

QString errorOf(const QDBusMessage &reply) {
	return reply.type() == QDBusMessage::ErrorMessage
		? (reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage().trimmed()) : QString();
}

class DiskSetup : public QWidget {
public:
	explicit DiskSetup(int tab) : m_host(this) {
		setWindowTitle("Disk Setup");
		setFixedSize(W, H);
		for (const char *t : { "First Aid", "Partition" }) {
			m_tabTexts.emplace_back(t, 200, PL_FONT_SYSTEM);
		}
		for (const Text &t : m_tabTexts) {
			m_tabs.push_back(t.t);
		}
		/* First Aid. */
		m_volumeList.frame = QRect(M + 8, PANE + 30, W - 2 * M - 16, 8 * PL_LIST_ROW_H + 2);
		m_volumeList.picked = [this](int) { updateButtons(); };
		const int by = m_volumeList.frame.bottom() + 10;
		m_verify = PanelButton("Verify", QRect(W - M - 12 - 186, by, 90, PL_BUTTON_H));
		m_verify.hint = "Checks the volume for damage, changing nothing.";
		m_verify.clicked = [this] { firstAid(false); };
		m_repair = PanelButton("Repair", QRect(W - M - 12 - 90, by, 90, PL_BUTTON_H));
		m_repair.hint = "Checks the volume and fixes what it can.";
		m_repair.clicked = [this] { firstAid(true); };
		m_automatic = PanelCheckbox("Mount automatically", QPoint(M + 8, by + 4));
		m_automatic.hint = "Opens the volume by itself when you log in or connect its drive. "
			"Off, it stays closed until you mount it.";
		m_automatic.toggled = [this](bool on) {
			if (const VetVolume *v = chosenVolume()) {
				setVolumeMountsAutomatically(v->uuid, on);
				log(QString("“%1” %2.").arg(v->name, on ? "will mount automatically"
					: "won't mount automatically any more"));
				if (on && v->mountPoints.isEmpty()) {
					const QString name = v->name;
					call(this, v->path, "org.freedesktop.UDisks2.Filesystem", "Mount", { QVariantMap() },
							[this, name](const QDBusMessage &reply) {
						if (!errorOf(reply).isEmpty()) {
							log(QString("“%1” couldn't be opened: %2").arg(name, errorOf(reply)));
						}
						reload();
					});
				}
			}
		};
		/* Partition. */
		m_drivePopup.rect = QRect(M + 8 + 50, PANE + 12, W - 2 * M - 16 - 50, PL_POPUP_H);
		m_drivePopup.chosen = [this](int row) { chooseDrive(row); };
		m_map.frame = QRect(M + 8, PANE + 44, MAP_W, MAP_H);
		m_map.picked = [this](int) { fillFields(); updateButtons(); update(); };
		m_map.dragged = [this](int part, qulonglong at) {
			if (moveBoundary(currentDrive(), &m_ops, part, at)) {
				refreshPlan();
			}
		};
		m_host.hintAreas.push_back({ &m_map.frame, "The drive from start (top) to end: click a partition or free "
			"space to choose it, or drag the line under a partition to make it bigger or smaller." });
		const int my = m_map.frame.bottom() + 8;
		m_add = PanelButton("+", QRect(M + 8, my, 28, PL_BUTTON_H));
		m_add.hint = "Makes a partition in the chosen free space.";
		m_add.clicked = [this] { addPartition(); };
		m_remove = PanelButton("\u2212", QRect(M + 8 + 34, my, 28, PL_BUTTON_H));
		m_remove.hint = "Deletes the chosen partition and everything on it.";
		m_remove.clicked = [this] { removePartition(); };
		/* The chosen partition, beside the map. */
		const int rx = RX + 56, rw = W - M - 8 - rx;
		m_name.rect = QRect(rx, PANE + 70, rw, PL_EDIT_H);
		m_name.hint = "The partition's name, as the Finder shows it.";
		m_name.accepts = [](QChar ch) { return ch.isPrint() && ch != ':' && ch != '/'; };
		m_name.maxLength = 27;
		m_name.edited = [this] { fieldsEdited(); };
		m_format.rect = QRect(rx, PANE + 98, rw, PL_POPUP_H);
		m_format.hint = "What the partition is formatted as: ZacOS for ZacOS 9, FAT32 or exFAT for any computer, NTFS for Windows.";
		m_format.chosen = [this](int) { fieldsEdited(); };
		m_size.rect = QRect(rx, PANE + 126, 80, PL_EDIT_H);
		m_size.hint = "Its size in GB. Or drag the line under it in the map.";
		m_size.accepts = [](QChar ch) { return ch.isDigit() || ch == '.' || ch == ','; };
		m_size.maxLength = 9;
		m_size.edited = [this] { sizeEdited(); };
		m_erase = PanelButton("Erase", QRect(W - M - 8 - 80, PANE + 154, 80, PL_BUTTON_H));
		m_erase.hint = "Erases the chosen partition with the name and format above, when you click Apply.";
		m_erase.clicked = [this] { erasePartition(); };
		m_newTable = PanelButton("New Table\u2026", QRect(W - M - 8 - 104, my, 104, PL_BUTTON_H));
		m_newTable.hint = "Erases the whole drive and gives it a new, empty partition table.";
		m_newTable.clicked = [this] { newTable(); };
		m_revert = PanelButton("Revert", QRect(W - M - 12 - 182, H - M - 8 - PL_BUTTON_H, 86, PL_BUTTON_H));
		m_revert.hint = "Forgets the changes not yet applied.";
		m_revert.clicked = [this] { m_ops.clear(); refreshPlan(); };
		m_apply = PanelButton("Apply", QRect(W - M - 12 - 86, H - M - 8 - PL_BUTTON_H, 86, PL_BUTTON_H), true);
		m_apply.hint = "Makes the changes listed. You'll be told what will be erased first.";
		m_apply.clicked = [this] { apply(); };

		qDBusRegisterMetaType<UDisksInterfaces>();
		qDBusRegisterMetaType<UDisksObjects>();
		showTab(tab);
		reload();
	}

protected:
	void paintEvent(QPaintEvent *) override {
		Pixels px(W, H);
		pl_canvas *c = &px.c;
		pl_fill(c, 0, 0, W - 1, H - 1, FACE);
		pl_tabs_paint(c, M, TOP, W - M - 1, H - M, m_tabs.data(), 2, m_tab);
		m_tab == 0 ? paintFirstAid(c) : paintPartition(c);
		m_host.paintControls(c, PANE_BG);
		QPainter p(this);
		px.blit(p);
	}

	void mousePressEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		const int tab = pl_tabs_hit(M, TOP, m_tabs.data(), 2, pos.x(), pos.y());
		if (tab >= 0 && tab != m_tab) {
			showTab(tab);
		} else if (m_tab == 0 && m_volumeList.press(pos)) {
		} else if (m_tab == 1 && m_drivePopup.press(this, pos)) {
		} else if (m_tab == 1 && m_map.press(pos)) {
		} else {
			m_host.hostPress(e);
		}
		updateButtons();
		update();
	}
	void mouseMoveEvent(QMouseEvent *e) override {
		const QPoint pos = e->position().toPoint();
		if (m_tab == 0) {
			m_volumeList.move(pos);
		} else {
			m_map.move(pos);
			setCursor(m_map.onLine(pos) ? Qt::SizeVerCursor : Qt::ArrowCursor);
		}
		m_host.hostMove(e);
		update();
	}
	void mouseReleaseEvent(QMouseEvent *e) override {
		if (m_tab == 0) {
			m_volumeList.release();
		} else if (m_map.release()) {
			fillFields();
		}
		m_host.hostRelease(e);
		updateButtons();
		update();
	}
	void wheelEvent(QWheelEvent *e) override {
		if (m_tab == 0 && m_volumeList.wheel(e->position().toPoint(), e->angleDelta().y())) {
			update();
		}
	}
	void keyPressEvent(QKeyEvent *e) override {
		if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
			close();
			return;
		}
		/* Up and down choose in the list or the map; the rest are the fields'. */
		const bool handled = m_tab == 0 ? m_volumeList.key(e->key(), e->text())
			: !m_host.focus && m_map.key(e->key());
		if (handled && m_tab == 1) {
			fillFields();
		}
		if (!handled) {
			m_host.hostKey(e);
		}
		updateButtons();
		update();
	}

private:
	PanelHost m_host;
	std::vector<Text> m_tabTexts;
	std::vector<const plat_text *> m_tabs;
	int m_tab = 0;
	bool m_busy = false;
	QStringList m_log;

	/* First Aid. */
	PanelList m_volumeList;
	PanelButton m_verify, m_repair;
	PanelCheckbox m_automatic;
	QList<VetVolume> m_volumes;
	QMap<QString, VetDrive> m_health;

	/* Partition. */
	PanelPopup m_drivePopup;
	DiskMap m_map;
	PanelButton m_add, m_remove, m_erase, m_newTable, m_revert, m_apply;
	PanelEdit m_name, m_size;  /* the chosen partition's */
	PanelPopup m_format;
	QStringList m_formatTypes; /* m_format's items' formats */
	QList<DiskDrive> m_drives;
	int m_drive = -1;
	QList<DiskOp> m_ops;
	DiskDrive m_planned;       /* the chosen drive with m_ops done */
	/* How small partitions can shrink: zacos9-install --min-size, as root. */
	QProcess *m_measuring = nullptr;
	QString m_measuringBlock;
	QMap<QString, QString> m_measureProblem; /* by block: why it couldn't be measured */

	void showTab(int tab) {
		m_tab = tab;
		if (tab == 0) {
			m_host.edits = {};
			m_host.popups = {};
			m_host.setFocus(nullptr);
			m_host.buttons = { &m_verify, &m_repair };
			m_host.checks = { &m_automatic };
			m_host.defaultButton = &m_repair;
		} else {
			m_host.checks = {};
			m_host.buttons = { &m_add, &m_remove, &m_erase, &m_newTable, &m_revert, &m_apply };
			m_host.edits = { &m_name, &m_size };
			m_host.popups = { &m_format };
			m_host.defaultButton = &m_apply;
		}
		updateButtons();
		update();
	}

	void log(const QString &text) {
		m_log << QTime::currentTime().toString("h:mm AP") + "  " + text;
		update();
	}

	void paintLog(pl_canvas *c, int y, int bottom) {
		QStringList lines;
		for (const QString &entry : m_log) {
			lines << panelWrap(entry, W - 2 * M - 16, PL_FONT_VIEWS);
		}
		const int fit = std::max(0, (bottom - y) / 12 + 1);
		for (const QString &line : lines.mid(std::max(0, int(lines.size()) - fit))) {
			panelText(c, line, M + 8, y, PL_FONT_VIEWS);
			y += 12;
		}
	}

	/* ---- First Aid ---- */

	const VetVolume *chosenVolume() const {
		const int row = m_volumeList.state.selected;
		return row >= 0 && row < m_volumes.size() ? &m_volumes[row] : nullptr;
	}

	void paintFirstAid(pl_canvas *c) {
		panelText(c, "Volumes:", M + 8, PANE + 22);
		m_volumeList.paint(c, true);
		const VetVolume *v = chosenVolume();
		int y = m_volumeList.frame.bottom() + PL_BUTTON_H + 28;
		if (v && v->readOnly && !v->mountPoints.contains("/")) {
			const QString notice = QString("“%1” is open read-only: it wasn't put away properly. "
				"Click Repair to fix it and open it normally.").arg(v->name);
			for (const QString &line : panelWrap(notice, W - 2 * M - 16, PL_FONT_SYSTEM)) {
				panelText(c, line, M + 8, y, PL_FONT_SYSTEM, RGB(0x99, 0, 0));
				y += 15;
			}
			y += 4;
		}
		if (v && m_health.contains(v->drive)) {
			const VetDrive &d = m_health.value(v->drive);
			for (const QString &line : panelWrap(driveHealth(d), W - 2 * M - 16, PL_FONT_SYSTEM)) {
				panelText(c, line, M + 8, y, PL_FONT_SYSTEM, d.failing ? RGB(0x99, 0, 0) : C_BLACK);
				y += 15;
			}
		}
		pl_hline(c, M + 8, W - M - 9, y + 4, GRAY(0x8));
		paintLog(c, y + 20, H - M - 10);
	}

	void firstAid(bool repair) {
		const VetVolume *v = chosenVolume();
		if (!v || m_busy) {
			return;
		}
		if (v->mountPoints.contains("/")) {
			Alert::ask(QString("“%1” is the startup disk, so it can't be checked while ZacOS is running "
				"from it. Start up from another disk (the install medium) and check it from there.").arg(v->name),
				"OK", QString());
			return;
		}
		const QString verb = repair ? "repaired" : "verified";
		if (v->mountPoints.isEmpty()) {
			check(*v, repair, false);
			return;
		}
		/* Open: closed first, which the programs using it prevent. */
		const QList<Holder> holders = programsUsing(v->mountPoints.first());
		if (holders.isEmpty()) {
			check(*v, repair, true);
			return;
		}
		const QString names = holderNames(holders);
		const QString them = names.contains(", ") || names.contains(" and ") ? "them" : "it";
		if (!Alert::ask(QString("“%1” is in use by %2, so it can't be closed to be %3. Quit %4 now? "
				"Unsaved changes in %4 will be lost.").arg(v->name, names, verb, them),
				repair ? "Quit and Repair" : "Quit and Verify")) {
			return;
		}
		const VetVolume volume = *v;
		m_busy = true;
		updateButtons();
		log(QString("Quitting %1…").arg(names));
		const QStringList units = unitsOf(holders);
		quit(holders, [this, volume, repair, units] {
			const QList<Holder> still = programsUsing(volume.mountPoints.first());
			if (!still.isEmpty()) {
				m_busy = false;
				log(QString("%1 wouldn't quit, so “%2” stays open.").arg(holderNames(still), volume.name));
				updateButtons();
				return;
			}
			check(volume, repair, true, units);
		});
	}

	static QStringList unitsOf(const QList<Holder> &holders) {
		QStringList units;
		for (const Holder &h : holders) {
			if (!h.unit.isEmpty() && !units.contains(h.unit)) {
				units << h.unit;
			}
		}
		return units;
	}

	/* systemctl --user `verb` `units`, not waited for (quit() watches the
	 * programs themselves). */
	void systemctl(const QString &verb, const QStringList &units) {
		if (units.isEmpty()) {
			return;
		}
		auto *p = new QProcess(this);
		connect(p, &QProcess::finished, p, &QObject::deleteLater);
		p->start("systemctl", QStringList{ "--user", verb } + units);
	}

	/* Asks `holders` to quit (their user services stopped, which would
	 * otherwise start them again; the rest SIGTERM), then makes them
	 * (SIGKILL) if they haven't within a few seconds; `done` once they're
	 * gone or given up on. */
	void quit(const QList<Holder> &holders, std::function<void()> done) {
		systemctl("stop", unitsOf(holders));
		for (const Holder &h : holders) {
			if (h.unit.isEmpty()) {
				::kill(h.pid, SIGTERM);
			}
		}
		auto *timer = new QTimer(this);
		auto *tries = new int(0);
		connect(timer, &QTimer::timeout, this, [=] {
			bool gone = true;
			for (const Holder &h : holders) {
				if (::kill(h.pid, 0) == 0) {
					gone = false;
				}
			}
			++*tries;
			if (*tries == 20) { /* 5 s: it isn't listening */
				for (const Holder &h : holders) {
					::kill(h.pid, SIGKILL);
				}
			}
			if (gone || *tries >= 28) {
				timer->stop();
				timer->deleteLater();
				delete tries;
				done();
			}
		});
		timer->start(250);
	}

	/* Verifies or repairs `v`; `open` closes it first and opens it again
	 * after: normally once repaired, else as it was (a volume a crash left
	 * marked in use, as NTFS is, mounts read-only; the Sniffer does so,
	 * localvolumes.cpp, and Repair clears the mark). The user services
	 * stopped to close it, `restart`, are started again once it's open. */
	void check(const VetVolume &v, bool repair, bool open, const QStringList &restart = QStringList()) {
		const QString name = v.name, path = v.path, fs = "org.freedesktop.UDisks2.Filesystem";
		const bool wasReadOnly = v.readOnly;
		const auto failed = [this, name](const QString &step, const QDBusMessage &reply) {
			m_busy = false;
			log(QString("“%1” couldn't be %2: %3").arg(name, step, errorOf(reply)));
			reload();
			updateButtons();
		};
		const auto checked = [=](const QDBusMessage &reply) {
			const bool ok = reply.arguments().value(0).toBool();
			if (!errorOf(reply).isEmpty()) {
				log(QString("“%1” couldn't be %2: %3").arg(name, repair ? "repaired" : "verified", errorOf(reply)));
			} else if (ok) {
				log(repair ? QString("“%1” has been repaired, or needed no repair.").arg(name)
					: QString("“%1” appears to be OK.").arg(name));
			} else {
				log(repair ? QString("“%1” couldn't be fully repaired. Copy what's on it elsewhere.").arg(name)
					: QString("“%1” has problems. Click Repair to fix them.").arg(name));
			}
			if (!open) {
				m_busy = false;
				reload();
				updateButtons();
				return;
			}
			/* Open again: normally once repaired, else as it was. */
			const bool readOnly = wasReadOnly && !(repair && errorOf(reply).isEmpty() && ok);
			const auto opened = [this, name, readOnly, restart](const QDBusMessage &mounted) {
				m_busy = false;
				log(errorOf(mounted).isEmpty()
					? QString("“%1” is open %2again.").arg(name, readOnly ? "read-only " : "")
					: QString("“%1” couldn't be opened again: %2").arg(name, errorOf(mounted)));
				if (!restart.isEmpty()) {
					systemctl("start", restart);
					QStringList names;
					for (const QString &unit : restart) {
						names << unit.chopped(int(strlen(".service")));
					}
					log(QString("%1 started again.").arg(names.join(", ")));
				}
				reload();
				updateButtons();
			};
			const QVariantMap options = readOnly ? QVariantMap{ { "options", "ro" } } : QVariantMap();
			call(this, path, fs, "Mount", { options }, [=](const QDBusMessage &mounted) {
				if (!errorOf(mounted).isEmpty() && !readOnly) {
					/* Not even as it was: read-only, so what's on it stays reachable. */
					call(this, path, fs, "Mount", { QVariantMap{ { "options", "ro" } } },
						[this, name, opened](const QDBusMessage &ro) {
						opened(ro);
						if (errorOf(ro).isEmpty()) {
							log(QString("“%1” wouldn't open normally; Repair may fix it.").arg(name));
						}
					});
					return;
				}
				opened(mounted);
			});
		};
		log(QString("%1 “%2”…").arg(repair ? "Repairing" : "Verifying", name));
		m_busy = true;
		updateButtons();
		const QString method = repair ? "Repair" : "Check";
		if (!open) {
			call(this, path, fs, method, { QVariantMap() }, checked);
			return;
		}
		call(this, path, fs, "Unmount", { QVariantMap() }, [=](const QDBusMessage &closed) {
			if (!errorOf(closed).isEmpty()) {
				failed(QString("closed to be %1 (a program is using it)").arg(repair ? "repaired" : "verified"), closed);
				return;
			}
			call(this, path, fs, method, { QVariantMap() }, checked);
		});
	}

	/* ---- reading UDisks ---- */

	void reload() {
		QDBusMessage m = QDBusMessage::createMethodCall(service(), "/org/freedesktop/UDisks2",
			"org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
		auto *w = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(m, 15000), this);
		connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
			w->deleteLater();
			const QDBusMessage reply = w->reply();
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
				log("Disk Setup needs UDisks, which isn't running: " + reply.errorMessage());
				return;
			}
			const UDisksObjects objects = qdbus_cast<UDisksObjects>(reply.arguments().first());
			/* First Aid's volumes. */
			const QString was = chosenVolume() ? chosenVolume()->path : QString();
			m_volumes.clear();
			m_health.clear();
			parseUDisks(objects, &m_volumes, &m_health);
			for (VetVolume &v : m_volumes) {
				v.readOnly = !v.mountPoints.isEmpty() && volumeMountedReadOnly(v.device);
			}
			QStringList rows;
			int select = m_volumes.isEmpty() ? -1 : 0;
			for (int i = 0; i < m_volumes.size(); i++) {
				const VetVolume &v = m_volumes[i];
				rows << QString("%1   (%2, %3%4)").arg(v.name, formatName(v.type), sizeText(v.size),
					v.mountPoints.isEmpty() ? QString() : QString(", in use"));
				if (v.path == was) {
					select = i;
				}
			}
			m_volumeList.setItems(rows);
			m_volumeList.select(select, false);
			/* Partition's drives. */
			const QString drive = m_drive >= 0 && m_drive < m_drives.size() ? m_drives[m_drive].block : QString();
			m_drives = parseDrives(objects);
			m_drivePopup.items.clear();
			m_drive = m_drives.isEmpty() ? -1 : 0;
			for (int i = 0; i < m_drives.size(); i++) {
				const DiskDrive &d = m_drives[i];
				m_drivePopup.items.emplace_back(QString("%1 — %2 (%3)%4").arg(d.device.section('/', -1),
					d.model.isEmpty() ? QString("Drive") : d.model, sizeText(d.size),
					d.live ? QString(", ZacOS is running from it") : QString()));
				if (d.block == drive) {
					m_drive = i;
				}
			}
			m_drivePopup.selected = m_drive;
			if (m_log.isEmpty()) {
				log("Choose a volume to verify or repair, or a drive to partition.");
			}
			refreshPlan();
		});
	}

	/* ---- Partition ---- */

	void chooseDrive(int row) {
		if (row == m_drive) {
			return;
		}
		if (!m_ops.isEmpty() && !Alert::ask("Forget the changes not yet applied to this drive?", "Forget", "Cancel")) {
			return;
		}
		m_ops.clear();
		m_drive = row;
		m_drivePopup.selected = row;
		refreshPlan();
	}

	const DiskDrive &currentDrive() const {
		static const DiskDrive none;
		return m_drive >= 0 && m_drive < m_drives.size() ? m_drives[m_drive] : none;
	}

	void refreshPlan() {
		m_planned = currentDrive();
		for (const DiskOp &op : m_ops) {
			m_planned = planned(m_planned, op);
		}
		const bool fresh = m_map.drive.block != m_planned.block;
		m_map.set(m_planned);
		if (fresh || m_map.selected < 0) {
			m_map.select(m_map.rows.isEmpty() ? -1 : 0, false);
		}
		if (!m_map.dragging()) {
			fillFields();
		}
		updateButtons();
		update();
	}

	const DiskPart *chosenPart() const {
		const DiskMap::Row *r = m_map.chosen();
		return r && r->part >= 0 ? &m_planned.parts[r->part] : nullptr;
	}

	QString chosenFormat() const {
		return m_formatTypes.value(m_format.selected);
	}

	static QString gbText(qulonglong bytes) {
		return QLocale().toString(double(bytes) / 1e9, 'f', 1);
	}

	/* The fields show the chosen partition, as planned. */
	void fillFields() {
		const DiskPart *p = chosenPart();
		m_name.setText(p ? p->label : QString());
		/* The formats Disk Setup makes, and the one it has if that's another. */
		m_formatTypes = diskFormats();
		m_format.items.clear();
		for (const QString &t : m_formatTypes) {
			m_format.items.emplace_back(formatName(t) + (t == "ext4" ? QString("  \u2014 for ZacOS") : t == "vfat"
				? QString("  \u2014 any computer, files under 4 GB") : t == "exfat" ? QString("  \u2014 any computer")
				: QString("  \u2014 for Windows")));
		}
		if (p && !m_formatTypes.contains(p->type)) {
			m_formatTypes << p->type;
			m_format.items.emplace_back(formatName(p->type), false);
		}
		m_format.selected = p ? int(m_formatTypes.indexOf(p->type)) : 0;
		m_size.setText(p ? gbText(p->size) : QString());
		measureChosen();
		updateButtons();
	}

	/* A new partition takes its name and format as they're typed; one already
	 * on the drive waits for Erase. */
	void fieldsEdited() {
		const DiskMap::Row *r = m_map.chosen();
		const DiskPart *p = chosenPart();
		if (p && p->block.isEmpty() && !p->efi && !p->biosBoot) {
			for (DiskOp &op : m_ops) {
				if (op.kind == DiskOp::Create && op.offset == p->offset) {
					op.label = m_name.text.trimmed();
					op.fsType = chosenFormat();
				}
			}
			const int keep = r ? m_map.selected : -1;
			refreshPlan();
			m_map.select(keep, false);
		}
		updateButtons();
		update();
	}

	/* Its size as typed, once it's a size it can be. */
	void sizeEdited() {
		const DiskMap::Row *r = m_map.chosen();
		if (!r || r->part < 0) {
			return;
		}
		bool ok = false;
		const double gb = QLocale().toDouble(m_size.text.trimmed(), &ok);
		const double gb2 = ok ? gb : m_size.text.trimmed().replace(',', '.').toDouble(&ok);
		if (!ok || gb2 <= 0) {
			return;
		}
		const DiskPart &p = m_planned.parts[r->part];
		const DiskSpan span = boundaryLimits(m_planned, r->part);
		const qulonglong end = p.offset + qulonglong(gb2 * 1e9) / MiB * MiB;
		if (end >= span.least && end <= span.most && moveBoundary(currentDrive(), &m_ops, r->part, end)) {
			m_planned = currentDrive();
			for (const DiskOp &op : m_ops) {
				m_planned = planned(m_planned, op);
			}
			m_map.set(m_planned);
			updateButtons();
			update();
		}
	}

	/* How small an ext4 or NTFS partition can be, once, when it's chosen. */
	void measureChosen() {
		const DiskPart *p = chosenPart();
		if (!p || p->block.isEmpty() || p->minSize > 0 || !resizable(p->type) || p->inUse() || m_measuring ||
				m_measureProblem.contains(p->block)) {
			return;
		}
		m_measuring = new QProcess(this);
		m_measuringBlock = p->block;
		const QString test = qEnvironmentVariable("ZACOS9_INSTALL_HELPER");
		const QStringList args = { "--min-size", p->device };
		connect(m_measuring, &QProcess::finished, this, [this] {
			const QString out = QString::fromUtf8(m_measuring->readAllStandardOutput());
			m_measuring->deleteLater();
			m_measuring = nullptr;
			qulonglong bytes = 0;
			QString problem = "It couldn't be measured, so it can only be made bigger.";
			for (const QString &line : out.split('\n')) {
				if (line.startsWith("min-size ")) {
					bytes = line.mid(9).trimmed().toULongLong();
				} else if (line.startsWith("error ")) {
					problem = line.mid(6).trimmed() + ".";
				}
			}
			for (DiskDrive &d : m_drives) {
				for (DiskPart &x : d.parts) {
					if (x.block == m_measuringBlock && bytes > 0) {
						x.minSize = bytes;
					}
				}
			}
			if (bytes == 0) {
				m_measureProblem[m_measuringBlock] = problem;
			}
			refreshPlan();
		});
		connect(m_measuring, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart && m_measuring) {
				m_measureProblem[m_measuringBlock] = "It couldn't be measured, so it can only be made bigger.";
				m_measuring->deleteLater();
				m_measuring = nullptr;
				update();
			}
		});
		if (!test.isEmpty()) {
			m_measuring->start(test, args);
		} else {
			m_measuring->start("pkexec", QStringList{ "/usr/libexec/zacos9/zacos9-install" } + args);
		}
	}

	void paintPartition(pl_canvas *c) {
		panelText(c, "Drive:", M + 8, m_drivePopup.rect.y() + 15);
		m_drivePopup.paint(c);
		m_map.paint(c);
		/* The chosen partition or free space. */
		const DiskMap::Row *r = m_map.chosen();
		const DiskPart *p = chosenPart();
		int y = PANE + 56;
		const int tw = W - M - 8 - RX;
		if (r && r->gap >= 0) {
			panelText(c, "Free space, " + sizeText(m_map.gaps[r->gap].size), RX, y);
			y += 18;
			for (const QString &line : panelWrap("Click + to make a partition here.", tw, PL_FONT_VIEWS)) {
				panelText(c, line, RX, y, PL_FONT_VIEWS);
				y += 12;
			}
		} else if (p) {
			const QString title = p->block.isEmpty() ? QString("New partition")
				: p->device.section('/', -1) + (p->inUse() ? "  (in use)" : "");
			panelText(c, title, RX, y);
			panelLabel(c, "Name:", m_name.rect.left() - 6, m_name.rect.top() + 15);
			panelLabel(c, "Format:", m_format.rect.left() - 6, m_format.rect.top() + 15);
			panelLabel(c, "Size:", m_size.rect.left() - 6, m_size.rect.top() + 15);
			panelText(c, "GB", m_size.rect.right() + 6, m_size.rect.top() + 15);
			/* What its size can be, or why not. */
			const DiskSpan span = r ? boundaryLimits(m_planned, r->part) : DiskSpan();
			QString note;
			if (p->efi || p->biosBoot) {
				note = "A computer starts up from it. It stays as it is.";
			} else if (p->inUse()) {
				note = "Eject it to change it.";
			} else if (!p->block.isEmpty() && !resizable(p->type)) {
				note = formatName(p->type) + " partitions can't be resized here.";
			} else if (!p->block.isEmpty() && p->minSize == 0 && m_measuring && m_measuringBlock == p->block) {
				note = "Finding how small it can be…";
			} else if (!p->block.isEmpty() && m_measureProblem.contains(p->block)) {
				note = m_measureProblem.value(p->block);
			} else if (span.least < span.most) {
				note = QString("It can be %1 to %2 GB: drag the line under it, or type a size.")
					.arg(gbText(span.least - p->offset), gbText(span.most - p->offset));
			}
			int ny = m_erase.rect.bottom() + 16;
			for (const QString &line : panelWrap(note, tw, PL_FONT_VIEWS)) {
				panelText(c, line, RX, ny, PL_FONT_VIEWS);
				ny += 12;
			}
		}
		/* The changes waiting for Apply. */
		y = m_add.rect.bottom() + 20;
		panelText(c, m_ops.isEmpty() ? QString("No changes waiting.") : QString("Changes to make when you click Apply:"),
			M + 8, y, PL_FONT_SYSTEM);
		y += 15;
		DiskDrive d = currentDrive();
		for (const DiskOp &op : m_ops) {
			panelText(c, "•  " + op.describe(d), M + 14, y, PL_FONT_VIEWS, C_BLACK, W - 2 * M - 30);
			d = planned(d, op);
			y += 12;
		}
		paintLog(c, std::max(y + 6, m_revert.rect.top() - 26), m_revert.rect.top() - 6);
	}

	void updateButtons() {
		const VetVolume *v = chosenVolume();
		m_verify.enabled = !m_busy && v && v->canCheck;
		m_repair.enabled = !m_busy && v && v->canRepair;
		/* The startup disk always mounts. */
		m_automatic.enabled = !m_busy && v && !v->uuid.isEmpty() && !v->mountPoints.contains("/");
		m_automatic.on = v && volumeMountsAutomatically(v->uuid,
			m_health.contains(v->drive) && !m_health.value(v->drive).removable);
		const DiskMap::Row *r = m_map.chosen();
		const DiskPart *p = chosenPart();
		const bool drive = !m_busy && m_drive >= 0 && !m_planned.live;
		m_map.enabled = drive;
		m_add.enabled = drive && r && r->gap >= 0;
		m_remove.enabled = drive && p && !p->inUse();
		const bool special = p && (p->efi || p->biosBoot);
		const bool fresh = p && p->block.isEmpty();
		m_name.enabled = m_format.enabled = drive && p && !special && !p->inUse();
		const DiskSpan span = r && r->part >= 0 ? boundaryLimits(m_planned, r->part) : DiskSpan();
		m_size.enabled = drive && p && span.least < span.most;
		/* Erase: one already on the drive, with a new name or format typed. */
		m_erase.enabled = drive && p && !fresh && !special && !p->inUse() &&
			(m_name.text.trimmed() != p->label || chosenFormat() != p->type) && diskFormats().contains(chosenFormat());
		m_newTable.enabled = drive && !m_planned.system;
		m_revert.enabled = !m_busy && !m_ops.isEmpty();
		m_apply.enabled = !m_busy && !m_ops.isEmpty();
	}

	/* Queues `op` if it's allowed on the drive as planned so far. */
	void queue(const DiskOp &op) {
		const QString why = refusal(m_planned, op);
		if (!why.isEmpty()) {
			Alert::ask(why, "OK", QString());
			return;
		}
		m_ops << op;
		refreshPlan();
	}

	/* +: a ZacOS partition filling the chosen free space; its name, format
	 * and size are then the fields'. */
	void addPartition() {
		const DiskMap::Row *r = m_map.chosen();
		if (!r || r->gap < 0) {
			return;
		}
		const DiskGap g = m_map.gaps[r->gap];
		DiskOp op{ DiskOp::Create };
		op.offset = g.offset;
		op.size = g.size;
		op.fsType = "ext4";
		op.label = "Untitled";
		const int row = m_map.selected;
		queue(op);
		m_map.select(row, false);
		fillFields();
		m_host.setFocus(&m_name);
		m_name.selectAll();
		update();
	}

	/* −: a new partition's step forgotten, or one on the drive deleted. */
	void removePartition() {
		const DiskMap::Row *r = m_map.chosen();
		if (!r || r->part < 0) {
			return;
		}
		const DiskPart &p = m_planned.parts[r->part];
		if (p.block.isEmpty()) {
			for (int i = m_ops.size() - 1; i >= 0; i--) {
				if (m_ops[i].kind == DiskOp::Create && m_ops[i].offset == p.offset) {
					m_ops.removeAt(i);
					break;
				}
			}
			refreshPlan();
			return;
		}
		DiskOp op{ DiskOp::Delete };
		op.part = r->part;
		queue(op);
	}

	void erasePartition() {
		const DiskMap::Row *r = m_map.chosen();
		if (!r || r->part < 0) {
			return;
		}
		DiskOp op{ DiskOp::Format };
		op.part = r->part;
		op.fsType = chosenFormat();
		op.label = m_name.text.trimmed();
		queue(op);
	}

	void newTable() {
		const std::vector<PopupItem> kinds = { PopupItem("GPT  — for computers from the last fifteen years"),
			PopupItem("MBR  — for older computers") };
		panelMenuNear(this, kinds, -1, m_newTable.rect, m_newTable.rect.top(), PL_FONT_SYSTEM, [this](int i) {
			queue(DiskOp{ DiskOp::NewTable, i == 0 ? "gpt" : "dos" });
		});
	}

	/* ---- Apply ---- */

	void apply() {
		if (m_ops.isEmpty() || m_drive < 0 || m_busy) {
			return;
		}
		const DiskDrive start = m_drives[m_drive];
		const QStringList erased = erasedBy(start, m_ops);
		QString message = QString("Make %1 change%2 to %3?").arg(m_ops.size()).arg(m_ops.size() == 1 ? "" : "s")
			.arg(start.device);
		if (!erased.isEmpty()) {
			message += " Everything on " + erased.join(", ") + " will be erased. This can't be undone.";
		}
		if (!Alert::ask(message, erased.isEmpty() ? "Apply" : "Erase and Apply", "Cancel")) {
			return;
		}
		m_busy = true;
		updateButtons();
		run(start, 0);
	}

	/* The queue, one step at a time, on the drive as each step leaves it. */
	void run(DiskDrive d, int index) {
		if (index >= m_ops.size()) {
			log("All the changes have been made.");
			finish();
			return;
		}
		const DiskOp op = m_ops[index];
		log(op.describe(d) + "…");
		const QString part = op.part >= 0 && op.part < d.parts.size() ? d.parts[op.part].block : QString();
		const auto next = [this, d, op, index](const QDBusMessage &reply, const QString &created = QString()) {
			if (!errorOf(reply).isEmpty()) {
				log("That couldn't be done: " + errorOf(reply) + " The changes after it weren't made.");
				finish();
				return;
			}
			DiskDrive after = planned(d, op);
			if (!created.isEmpty()) {
				for (DiskPart &p : after.parts) {
					if (p.block.isEmpty() && p.offset == op.offset) {
						p.block = created;
					}
				}
			}
			run(after, index + 1);
		};
		const QString Block = "org.freedesktop.UDisks2.Block", Partition = "org.freedesktop.UDisks2.Partition",
			Filesystem = "org.freedesktop.UDisks2.Filesystem";
		switch (op.kind) {
		case DiskOp::NewTable:
			call(this, d.block, Block, "Format", { op.tableType, QVariantMap() },
				[next](const QDBusMessage &r) { next(r); });
			break;
		case DiskOp::Delete:
			call(this, part, Partition, "Delete", { QVariantMap() }, [next](const QDBusMessage &r) { next(r); });
			break;
		case DiskOp::Format:
			call(this, part, Block, "Format", { op.fsType,
				QVariantMap{ { "label", op.label }, { "update-partition-type", true } } },
				[next](const QDBusMessage &r) { next(r); });
			break;
		case DiskOp::Create:
			call(this, d.block, "org.freedesktop.UDisks2.PartitionTable", "CreatePartitionAndFormat",
				{ QVariant::fromValue(op.offset), QVariant::fromValue(op.size), op.partType,
					d.table == "gpt" ? op.label : QString(), QVariantMap(), op.fsType,
					QVariantMap{ { "label", op.label } } },
				[next](const QDBusMessage &r) {
					next(r, r.arguments().value(0).value<QDBusObjectPath>().path());
				});
			break;
		case DiskOp::Resize: {
			const qulonglong was = d.parts[op.part].size;
			const QVariant size = QVariant::fromValue(op.size);
			if (op.size < was) {
				/* Smaller: the files first, then the partition round them. */
				call(this, part, Filesystem, "Resize", { size, QVariantMap() }, [this, part, size, next, Partition](const QDBusMessage &r) {
					if (!errorOf(r).isEmpty()) {
						next(r);
						return;
					}
					call(this, part, Partition, "Resize", { size, QVariantMap() }, [next](const QDBusMessage &r2) { next(r2); });
				});
			} else {
				/* Bigger: the partition first, then the files fill it. */
				call(this, part, Partition, "Resize", { size, QVariantMap() }, [this, part, next, Filesystem](const QDBusMessage &r) {
					if (!errorOf(r).isEmpty()) {
						next(r);
						return;
					}
					call(this, part, Filesystem, "Resize", { QVariant::fromValue(qulonglong(0)), QVariantMap() },
						[next](const QDBusMessage &r2) { next(r2); });
				});
			}
			break;
		}
		}
	}

	void finish() {
		m_busy = false;
		m_ops.clear();
		reload();
	}
};

} // namespace

int main(int argc, char *argv[]) {
	/* zacos9-wm draws every frame; Qt must not add its own. */
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Disk Setup");
	QApplication::setDoubleClickInterval(pl_double_click_ms());
	QGuiApplication::setDesktopFileName("zacos9-disksetup"); /* Wayland app_id */
	platinumShellInit();
	DiskSetup setup(app.arguments().contains("--partition") ? 1 : 0);
	setup.show();
	return app.exec();
}
