#include "startupdisk.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QTimer>
#include <unistd.h>

#include "alert.h"
#include "icons.h"
#include "panelkit.h"

static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int W = 380, H = 330, M = 12;
static constexpr int LIST_TOP = 34, LIST_BOTTOM = 230;

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

/* Runs it to the end, keeping the window drawn (pkexec may be asking for a
 * password; regenerating grub.cfg takes a few seconds). No
 * waitForFinished(): see CLAUDE.md. */
static QString runHelper(const QStringList &args, bool *ok, QString *err) {
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	if (geteuid() == 0 || !qEnvironmentVariableIsEmpty("ZACOS9_STARTUPDISK_HELPER")) {
		p.start(helperPath(), args);
	} else {
		p.start("pkexec", QStringList{ helperPath() } + args);
	}
	while (!done) {
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
	}
	const bool started = p.error() != QProcess::FailedToStart;
	*ok = started && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
	*err = QString::fromUtf8(p.readAllStandardError()).trimmed().section('\n', -1);
	if (!*ok && err->isEmpty()) {
		*err = started ? QString("The startup disk helper stopped with status %1.").arg(p.exitCode())
			: QString("The startup disk helper couldn't be run.");
	}
	return QString::fromUtf8(p.readAllStandardOutput());
}

struct StartupDiskPanel::Parts {
	PanelHost host;
	PanelList list;
	PanelCheckbox every;
	PanelButton others, restart;
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
	d->list.picked = [this](int row) { choose(row); };
	d->every = PanelCheckbox("Show every version", QPoint(M, LIST_BOTTOM + 10));
	d->every.toggled = [this](bool) { fill(); };
	d->others = PanelButton("Look for Other Systems", QRect(M, H - M - PL_BUTTON_H, 170, PL_BUTTON_H));
	d->others.clicked = [this] { lookForOthers(); };
	d->restart = PanelButton("Restart", QRect(W - M - 4 - 90, H - M - PL_BUTTON_H, 90, PL_BUTTON_H), true);
	d->restart.clicked = [this] { restart(); };
	d->host.buttons = { &d->others, &d->restart };
	d->host.checks = { &d->every };
	d->host.defaultButton = &d->restart;
	QTimer::singleShot(0, this, [this] { reload(); });
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
