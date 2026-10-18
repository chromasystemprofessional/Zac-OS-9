#include "startupdisk.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>
#include <QPainter>
#include <QProcess>
#include <QTimer>
#include <unistd.h>

#include "alert.h"
#include "icons.h"
#include "panelkit.h"

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int W = 380, H = 400, M = 12;
static constexpr int LIST_TOP = 34, LIST_BOTTOM = 230;
static constexpr int CLASSIC_Y = 306; /* the Old Dog pop-up */
static constexpr int CLASSIC_X = M + 150;

QList<StartupEntry> parseStartupEntries(const QString &output, QString *current) {
	QList<StartupEntry> out;
	for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
		const QStringList f = line.split('\t');
		if (f.value(0) == "default" && current) {
			*current = f.value(1);
		} else if (f.value(0) == "entry" && f.size() >= 5) {
			out << StartupEntry{ f[1], f[2], f[3], f[4].toInt() };
		}
	}
	return out;
}

int startupEntryIcon(const StartupEntry &entry) {
	if (entry.kind == "windows") {
		return PL_ICON_WINDOWS;
	}
	if (entry.kind == "mac") {
		return PL_ICON_CLASSIC;
	}
	if (entry.kind == "firmware") {
		return PL_ICON_EXT_CHIP;
	}
	return PL_ICON_DISK;
}

/* An HFS volume's master directory block, or an HFS+ volume header, at
 * `at`: its name (HFS only) and whether a System Folder is blessed. */
static bool readVolume(QFile &f, qint64 at, QString *name, bool *blessed) {
	if (!f.seek(at + 1024)) {
		return false;
	}
	const QByteArray h = f.read(96);
	if (h.size() < 96) {
		return false;
	}
	const auto be32 = [&h](int o) { return qFromBigEndian<quint32>(h.constData() + o); };
	if (h.startsWith("BD")) {
		// drVN, a Pascal string; drFndrInfo[0], the blessed folder.
		*name = QString::fromLatin1(h.mid(37, qMin(static_cast<int>(static_cast<uchar>(h[36])), 27)));
		*blessed = be32(92) != 0;
		return true;
	}
	if (h.startsWith("H+") || h.startsWith("HX")) {
		*blessed = be32(80) != 0; /* finderInfo[0] */
		return true;
	}
	return false;
}

ClassicDisk classicDisk(const QString &path) {
	ClassicDisk disk;
	disk.path = path;
	const QString suffix = QFileInfo(path).suffix().toLower();
	disk.cd = suffix == "iso" || suffix == "toast" || suffix == "cdr";
	disk.name = QFileInfo(path).completeBaseName();
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return disk;
	}
	QString name;
	bool blessed = false;
	if (f.read(2) == "ER") {
		// An Apple partition map: entries in 512-byte blocks from block 1.
		for (int i = 1; i < 64 && !blessed; ++i) {
			if (!f.seek(512 * i)) {
				break;
			}
			const QByteArray e = f.read(80);
			if (e.size() < 80 || !e.startsWith("PM")) {
				break;
			}
			if (e.mid(48, 32).startsWith("Apple_HFS")) {
				const qint64 start = qFromBigEndian<quint32>(e.constData() + 8);
				readVolume(f, start * 512, &name, &blessed);
			}
		}
	} else {
		readVolume(f, 0, &name, &blessed);
	}
	disk.system = blessed;
	if (!name.isEmpty()) {
		disk.name = name;
	}
	return disk;
}

QStringList parseClassicCheck(const QString &output, QString *startup) {
	QStringList disks;
	for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
		// A blank disk the launcher would make isn't there yet.
		if (line.startsWith("disk: ") && !line.endsWith("(blank, made at first start)")) {
			disks << line.mid(6);
		} else if (line.startsWith("startup: ") && startup) {
			*startup = line.mid(9);
		}
	}
	return disks;
}

QString classicConfPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9/classic.conf";
}

bool setClassicStartupDisk(const QString &path) {
	QStringList lines;
	QFile in(classicConfPath());
	if (in.open(QIODevice::ReadOnly)) {
		for (const QString &line : QString::fromUtf8(in.readAll()).split('\n', Qt::SkipEmptyParts)) {
			if (!line.startsWith("startup-disk=")) {
				lines << line;
			}
		}
	}
	lines << "startup-disk=" + path;
	QDir().mkpath(QFileInfo(classicConfPath()).path());
	QSaveFile out(classicConfPath());
	return out.open(QIODevice::WriteOnly) && out.write((lines.join('\n') + '\n').toUtf8()) >= 0 && out.commit();
}

static QString classicLauncher() {
	const QString env = qEnvironmentVariable("ZACOS9_CLASSIC");
	if (!env.isEmpty()) {
		return env;
	}
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/zacos9-classic", dir + "/../classic/zacos9-classic" }) {
		if (QFileInfo(candidate).isExecutable()) {
			return candidate;
		}
	}
	return "zacos9-classic";
}

/* The helper, as root: installed beside us, or the source tree's. */
static QString helperPath() {
	const QString env = qEnvironmentVariable("ZACOS9_STARTUPDISK_HELPER");
	if (!env.isEmpty()) {
		return env;
	}
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/zacos9-startupdisk-helper",
			dir + "/../../startupdisk/zacos9-startupdisk-helper" }) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/libexec/zacos9/zacos9-startupdisk-helper";
}

/* Runs a program to the end, keeping the window drawn (pkexec may be
 * asking for a password; regenerating grub.cfg takes a few seconds). No
 * waitForFinished(): see CLAUDE.md. */
static QString runProgram(const QString &what, const QString &program, const QStringList &args, bool *ok,
		QString *err) {
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	p.start(program, args);
	while (!done) {
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
	}
	const bool started = p.error() != QProcess::FailedToStart;
	*ok = started && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
	*err = QString::fromUtf8(p.readAllStandardError()).trimmed().section('\n', -1);
	if (!*ok && err->isEmpty()) {
		*err = started ? QString("%1 stopped with status %2.").arg(what).arg(p.exitCode())
			: QString("%1 couldn't be run.").arg(what);
	}
	return QString::fromUtf8(p.readAllStandardOutput());
}

static QString runHelper(const QStringList &args, bool *ok, QString *err) {
	if (geteuid() == 0 || !qEnvironmentVariableIsEmpty("ZACOS9_STARTUPDISK_HELPER")) {
		return runProgram("The startup disk helper", helperPath(), args, ok, err);
	}
	return runProgram("The startup disk helper", "pkexec", QStringList{ helperPath() } + args, ok, err);
}

struct StartupDiskPanel::Parts {
	PanelHost host;
	PanelList list;
	PanelCheckbox every;
	PanelButton others, restart;
	PanelPopup classic;
	QList<ClassicDisk> classicDisks; /* those with a System Folder */
	QString classicStartup, classicProblem;
	QList<StartupEntry> entries;
	QList<int> shown; /* list rows -> entries */
	QString current, problem;
	bool busy = false;
	explicit Parts(QWidget *w) : host(w) {}
};

StartupDiskPanel::StartupDiskPanel() : d(std::make_unique<Parts>(this)) {
	setWindowTitle("Startup Disk");
	setFixedSize(W, H);
	d->list.frame = QRect(M, LIST_TOP, W - 2 * M, LIST_BOTTOM - LIST_TOP);
	d->host.hintAreas.push_back({ &d->list.frame, "The systems this computer can start up from. Click one to start from it next time." });
	d->list.picked = [this](int row) { choose(row); };
	d->every = PanelCheckbox("Show every version", QPoint(M, LIST_BOTTOM + 10));
	d->every.hint = "Lists every installed version of each system, not just the newest.";
	d->every.toggled = [this](bool) { fill(); };
	d->others = PanelButton("Look for Other Systems", QRect(M, H - M - PL_BUTTON_H, 170, PL_BUTTON_H));
	d->others.hint = "Looks on every disk for other systems to start up from.";
	d->others.clicked = [this] { lookForOthers(); };
	d->restart = PanelButton("Restart", QRect(W - M - 4 - 90, H - M - PL_BUTTON_H, 90, PL_BUTTON_H), true);
	d->restart.hint = "Restarts the computer from the selected system.";
	d->restart.clicked = [this] { restart(); };
	d->classic.rect = QRect(CLASSIC_X, CLASSIC_Y, W - M - CLASSIC_X, PL_POPUP_H);
	d->classic.hint = "Chooses the Mac OS disk that Classic starts up from.";
	d->classic.label = "Old Dog starts up with:";
	d->classic.chosen = [this](int row) { chooseClassic(row); };
	d->host.buttons = { &d->others, &d->restart };
	d->host.checks = { &d->every };
	d->host.popups = { &d->classic };
	d->host.defaultButton = &d->restart;
	QTimer::singleShot(0, this, [this] {
		reload();
		reloadClassic();
	});
}

StartupDiskPanel::~StartupDiskPanel() = default;

QStringList StartupDiskPanel::rows() const {
	return d->list.items;
}

int StartupDiskPanel::selectedRow() const {
	return d->list.state.selected;
}

QString StartupDiskPanel::current() const {
	return d->current;
}

void StartupDiskPanel::setShowEveryVersion(bool on) {
	d->every.on = on;
	fill();
}

void StartupDiskPanel::pick(int row) {
	d->list.select(row, true);
}

QStringList StartupDiskPanel::classicRows() const {
	QStringList rows;
	for (const PopupItem &item : d->classic.items) {
		rows << item.text;
	}
	return rows;
}

int StartupDiskPanel::classicSelected() const {
	return d->classic.enabled ? d->classic.selected : -1;
}

QString StartupDiskPanel::classicProblem() const {
	return d->classicProblem;
}

void StartupDiskPanel::pickClassic(int row) {
	chooseClassic(row);
}

void StartupDiskPanel::reloadClassic() {
	bool ok;
	QString err;
	const QString out = runProgram("Old Dog's launcher", classicLauncher(), { "--check" }, &ok, &err);
	d->classicDisks.clear();
	d->classicStartup.clear();
	d->classicProblem.clear();
	if (ok) {
		// The launcher attaches the chosen disk first: without a choice,
		// the first with a System Folder is the one the Mac starts from.
		for (const QString &path : parseClassicCheck(out, &d->classicStartup)) {
			const ClassicDisk disk = classicDisk(path);
			if (disk.system) {
				d->classicDisks << disk;
			}
		}
		if (d->classicDisks.isEmpty()) {
			d->classicProblem = "Old Dog has no disk with a System Folder.";
		}
	} else {
		d->classicProblem = err;
	}
	d->classic.items.clear();
	d->classic.selected = 0;
	for (int i = 0; i < d->classicDisks.size(); ++i) {
		const ClassicDisk &disk = d->classicDisks[i];
		QString text = disk.cd ? disk.name + " (CD)" : disk.name;
		for (int j = 0; j < d->classicDisks.size(); ++j) {
			if (j != i && d->classicDisks[j].name == disk.name) {
				text += " – " + QFileInfo(disk.path).fileName();
				break;
			}
		}
		d->classic.items.push_back(PopupItem(text));
		if (disk.path == d->classicStartup) {
			d->classic.selected = i;
		}
	}
	if (d->classic.items.empty()) {
		d->classic.items.push_back(PopupItem("None"));
	}
	d->classic.enabled = !d->classicDisks.isEmpty();
	update();
}

void StartupDiskPanel::chooseClassic(int row) {
	if (row < 0 || row >= d->classicDisks.size()) {
		return;
	}
	if (!setClassicStartupDisk(d->classicDisks[row].path)) {
		Alert::ask("Old Dog's startup disk couldn't be changed: " + classicConfPath() + " couldn't be written.",
			"OK", QString());
	}
	reloadClassic();
}

void StartupDiskPanel::reload() {
	d->busy = true;
	update();
	bool ok;
	QString err;
	const QString out = runHelper({ "list" }, &ok, &err);
	d->busy = false;
	d->problem = ok ? QString() : err;
	d->entries = ok ? parseStartupEntries(out, &d->current) : QList<StartupEntry>();
	fill();
}

void StartupDiskPanel::fill() {
	d->shown.clear();
	QStringList rows;
	std::vector<int> icons;
	int selected = -1;
	for (int i = 0; i < d->entries.size(); ++i) {
		const StartupEntry &e = d->entries[i];
		// Inside a submenu (other kernels, recovery mode) only if asked
		// for, or if that's the one it starts with.
		if (e.depth > 0 && !d->every.on && e.id != d->current) {
			continue;
		}
		if (e.id == d->current) {
			selected = rows.size();
		}
		d->shown << i;
		rows << e.title;
		icons.push_back(startupEntryIcon(e));
	}
	d->list.setItems(rows, icons);
	d->list.select(selected, false);
	if (selected >= 0) {
		d->list.ensureVisible(selected);
	}
	d->restart.enabled = !d->entries.isEmpty();
	update();
}

void StartupDiskPanel::choose(int row) {
	if (row < 0 || row >= d->shown.size()) {
		return;
	}
	const StartupEntry e = d->entries[d->shown[row]];
	if (e.id == d->current) {
		return;
	}
	d->busy = true;
	update();
	bool ok;
	QString err;
	runHelper({ "set", e.id }, &ok, &err);
	d->busy = false;
	if (!ok) {
		Alert::ask("The startup system couldn't be changed. " + err, "OK", QString());
	}
	reload();
}

void StartupDiskPanel::restart() {
	QString title;
	for (const StartupEntry &e : d->entries) {
		if (e.id == d->current) {
			title = e.title;
		}
	}
	if (!Alert::ask(title.isEmpty() ? QString("Restart the computer now?")
			: QString("Restart the computer now? It will start up with “%1”.").arg(title), "Restart", "Cancel")) {
		return;
	}
	const QString command = qEnvironmentVariable("ZACOS9_RESTART_COMMAND", "systemctl reboot");
	const QStringList words = QProcess::splitCommand(command);
	QProcess::startDetached(words.value(0), words.mid(1));
}

void StartupDiskPanel::lookForOthers() {
	d->busy = true;
	update();
	bool ok;
	QString err;
	runHelper({ "detect" }, &ok, &err);
	d->busy = false;
	if (!ok) {
		Alert::ask("Other systems couldn't be looked for. " + err, "OK", QString());
	}
	reload();
}

void StartupDiskPanel::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	Text prompt("Select the system you want to start up with:", W - 2 * M, PL_FONT_SYSTEM);
	pl_text(c, prompt.t, M, LIST_TOP - 10, C_BLACK);
	d->list.paint(c, true);
	QString status;
	if (d->busy) {
		status = "One moment…";
	} else if (!d->problem.isEmpty()) {
		status = d->problem;
	} else {
		for (const StartupEntry &e : d->entries) {
			if (e.id == d->current) {
				status = "Starts up with “" + e.title + "”.";
			}
		}
	}
	int y = LIST_BOTTOM + 44;
	for (const QString &line : panelWrap(status, W - 2 * M, PL_FONT_VIEWS)) {
		Text t(line, W - 2 * M, PL_FONT_VIEWS);
		pl_text(c, t.t, M, y, d->problem.isEmpty() ? C_BLACK : RGB(0xAA, 0x22, 0x22));
		y += 12;
	}
	// Old Dog's own startup disk, under an etched line.
	pl_hline(c, M, W - M - 1, CLASSIC_Y - 12, GRAY(0x8));
	pl_hline(c, M, W - M - 1, CLASSIC_Y - 11, C_WHITE);
	if (!d->classicProblem.isEmpty()) {
		int py = CLASSIC_Y + PL_POPUP_H + 14;
		for (const QString &line : panelWrap(d->classicProblem, W - 2 * M, PL_FONT_VIEWS)) {
			Text t(line, W - 2 * M, PL_FONT_VIEWS);
			pl_text(c, t.t, M, py, GRAY(0x6));
			py += 12;
		}
	}
	d->host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void StartupDiskPanel::mousePressEvent(QMouseEvent *e) {
	if (!d->busy && d->list.press(e->position().toPoint())) {
		update();
		return;
	}
	d->host.hostPress(e);
	update();
}

void StartupDiskPanel::mouseMoveEvent(QMouseEvent *e) {
	if (d->list.move(e->position().toPoint())) {
		update();
		return;
	}
	d->host.hostMove(e);
	update();
}

void StartupDiskPanel::mouseReleaseEvent(QMouseEvent *e) {
	d->list.release();
	d->host.hostRelease(e);
	update();
}

void StartupDiskPanel::mouseDoubleClickEvent(QMouseEvent *e) {
	mousePressEvent(e);
}

void StartupDiskPanel::wheelEvent(QWheelEvent *e) {
	if (d->list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void StartupDiskPanel::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (!d->busy && d->list.key(e->key(), e->text())) {
		update();
		return;
	}
	d->host.hostKey(e);
	update();
}
