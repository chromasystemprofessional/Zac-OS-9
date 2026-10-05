#include "wininstall.h"

#include <QCloseEvent>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QLocale>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>

#include "icons.h"
#include "pixels.h"
#include "settings.h"

static constexpr int W = 460, H = 320;
static constexpr int MARGIN = 16;
static constexpr QRect WELL(MARGIN, 44, W - 2 * MARGIN, 150);
static constexpr int BTN_Y = H - MARGIN - PL_BUTTON_H;
static constexpr uint32_t FACE = GRAY(0xD);
static constexpr uint32_t RED = RGB(0x99, 0x00, 0x00);

static bool isInstaller(const QString &path) {
	const QString suffix = QFileInfo(path).suffix().toLower();
	return suffix == "exe" || suffix == "msi";
}

static QString wineBinary() {
	for (const char *name : { "wine", "wine64" }) {
		const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
		if (!path.isEmpty()) {
			return path;
		}
	}
	return QString();
}

static QString winePrefix() {
	const QString env = qEnvironmentVariable("WINEPREFIX");
	return env.isEmpty() ? QDir::homePath() + "/.wine" : env;
}

/* Where Wine files a Windows program's Start-menu shortcuts. */
static QSet<QString> wineEntries() {
	QSet<QString> out;
	const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/applications/wine";
	QDirIterator it(dir, { "*.desktop" }, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		out.insert(it.next());
	}
	return out;
}

static QString entryName(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return QString();
	}
	for (const QByteArray &line : f.readAll().split('\n')) {
		if (line.startsWith("Name=")) {
			return QString::fromUtf8(line.mid(5)).trimmed();
		}
	}
	return QString();
}

/* Where a Windows program's own folder goes in the prefix. */
static QStringList programRoots() {
	const QString c = winePrefix() + "/drive_c";
	QStringList roots = { c + "/Program Files", c + "/Program Files (x86)" };
	QDir users(c + "/users");
	for (const QString &u : users.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		roots << users.filePath(u) + "/AppData/Local/Programs";
	}
	return roots;
}

static QSet<QString> programDirs() {
	QSet<QString> out;
	for (const QString &root : programRoots()) {
		for (const QFileInfo &fi : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
			out.insert(fi.absoluteFilePath());
		}
	}
	return out;
}

/* Many installers leave no Start-menu shortcut Wine can turn into a desktop
 * entry (portable programs, a missing "create shortcuts" step). For a program
 * folder the installer added, make the entry ourselves from its main .exe:
 * the one named like the folder, else the biggest that isn't an uninstaller
 * or helper. Returns the program's name, or nothing. */
static QString makeEntryFor(const QString &dir, const QString &wine) {
	const QString folder = QFileInfo(dir).fileName();
	QString best;
	qint64 bestScore = -1;
	QDirIterator it(dir, { "*.exe" }, QDir::Files, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QFileInfo fi(it.next());
		QString base = fi.completeBaseName().toLower();
		if (base.contains("unins") || base.contains("uninst") || base.contains("setup") ||
				base.contains("update") || base.contains("crash") || base.contains("helper")) {
			continue;
		}
		const int depth = fi.absolutePath().mid(dir.size()).count('/');
		qint64 score = fi.size() / 1024 - depth * 100000;
		if (base.remove(' ').remove('+') == QString(folder).remove(' ').remove('+').toLower()) {
			score += 1000000000;
		}
		if (score > bestScore) {
			bestScore = score;
			best = fi.absoluteFilePath();
		}
	}
	if (best.isEmpty()) {
		return QString();
	}
	const QString entries = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/applications/wine/Programs";
	QDir().mkpath(entries);
	QSaveFile f(entries + "/" + folder + ".desktop");
	if (!f.open(QIODevice::WriteOnly)) {
		return QString();
	}
	auto quote = [](QString s) {
		s.replace('\\', "\\\\").replace('"', "\\\"").replace('$', "\\$").replace('`', "\\`");
		return "\"" + s + "\"";
	};
	QString exec = "env " + quote("WINEPREFIX=" + winePrefix()) + " " + quote(wine) + " " + quote(best);
	exec.replace('%', "%%");
	f.write(("[Desktop Entry]\nType=Application\nName=" + folder + "\nExec=" + exec + "\nPath=" +
		QFileInfo(best).absolutePath() + "\nStartupNotify=true\nCategories=Wine;\n").toUtf8());
	return f.commit() ? folder : QString();
}

/* Text centred on x = W/2 at a baseline. */
static void centred(pl_canvas *c, const QString &s, int baseline, pl_font font = PL_FONT_SYSTEM,
		uint32_t color = C_BLACK) {
	Text t(s, W - 2 * MARGIN - 8, font);
	const int w = t.t->ink_l < 0 ? 0 : t.t->ink_r - t.t->ink_l + 1;
	pl_text(c, t.t, (W - w) / 2 - std::max(0, t.t->ink_l), baseline, color);
}

/* ---- construction ------------------------------------------------------------------ */

WinInstallWindow::WinInstallWindow(const QString &file) {
	setWindowTitle("Windows Installer");
	setFixedSize(W, H);
	setAcceptDrops(true);

	m_install = PanelButton("Install", QRect(W - MARGIN - 90, BTN_Y, 90, PL_BUTTON_H), true);
	m_openOnly = PanelButton("Open Without Installing",
		QRect(W - MARGIN - 90 - 12 - 170, BTN_Y, 170, PL_BUTTON_H));
	m_done = PanelButton("Done", QRect(W - MARGIN - 90, BTN_Y, 90, PL_BUTTON_H), true);
	m_another = PanelButton("Install Another",
		QRect(W - MARGIN - 90 - 12 - 130, BTN_Y, 130, PL_BUTTON_H));
	m_install.clicked = [this] { install(); };
	m_openOnly.clicked = [this] { openWithoutInstalling(); };
	m_done.clicked = [this] { close(); };
	m_another.clicked = [this] {
		m_file.clear();
		setState(State::Empty);
	};

	m_sweep.setInterval(30);
	m_sweep.callOnTimeout([this] {
		m_sweepPos += 0.03;
		if (m_sweepPos > 1.3) {
			m_sweepPos = -0.3;
		}
		update();
	});

	if (!file.isEmpty()) {
		setFile(file);
	} else {
		setState(State::Empty);
	}
}

void WinInstallWindow::setFile(const QString &path) {
	if (!isInstaller(path) || !QFileInfo(path).isFile()) {
		m_message = "That isn't a Windows installer. Drag a .exe or .msi file here.";
		update();
		return;
	}
	m_file = QFileInfo(path).absoluteFilePath();
	m_message.clear();
	setState(State::Ready);
}

void WinInstallWindow::setState(State s) {
	m_state = s;
	switch (s) {
	case State::Ready:
		m_host.buttons = { &m_openOnly, &m_install };
		m_host.defaultButton = &m_install;
		/* Only an .exe can be a program in itself; an .msi is always a package. */
		m_openOnly.enabled = QFileInfo(m_file).suffix().toLower() == "exe";
		break;
	case State::Done:
		m_host.buttons = { &m_another, &m_done };
		m_host.defaultButton = &m_done;
		break;
	default:
		m_host.buttons.clear();
		m_host.defaultButton = nullptr;
		break;
	}
	const bool busy = s == State::Preparing || s == State::Installing;
	if (busy && !m_sweep.isActive()) {
		m_sweepPos = 0;
		m_sweep.start();
	} else if (!busy) {
		m_sweep.stop();
	}
	update();
}

/* ---- running Wine -------------------------------------------------------------------- */

void WinInstallWindow::install() {
	if (wineBinary().isEmpty()) {
		m_message = "Windows support (Wine) isn't installed on this computer.";
		update();
		return;
	}
	m_message.clear();
	m_added.clear();
	m_before = wineEntries();
	if (QDir(winePrefix()).exists()) {
		runInstaller();
		return;
	}
	/* Wine's first run builds its Windows folder (its "prefix"): half a
	 * minute or so, and it may offer to download Wine's .NET and HTML
	 * support. Do it on its own, so the installer starts on a ready one. */
	setState(State::Preparing);
	m_wine = new QProcess(this);
	connect(m_wine, &QProcess::finished, this, [this] {
		m_wine->deleteLater();
		m_wine = nullptr;
		runInstaller();
	});
	m_wine->start(wineBinary(), { "wineboot", "--init" });
}

void WinInstallWindow::runInstaller() {
	m_beforeDirs = programDirs();
	setState(State::Installing);
	QStringList args;
	if (QFileInfo(m_file).suffix().toLower() == "msi") {
		args << "msiexec" << "/i";
	}
	args << m_file;
	m_wine = new QProcess(this);
	m_wine->setWorkingDirectory(QFileInfo(m_file).absolutePath());
	/* Wine's console chatter goes nowhere; the installer has windows. */
	m_wine->setStandardOutputFile(QProcess::nullDevice());
	m_wine->setStandardErrorFile(QProcess::nullDevice());
	connect(m_wine, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
		m_wine->deleteLater();
		m_wine = nullptr;
		finished(code);
	});
	connect(m_wine, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			m_wine->deleteLater();
			m_wine = nullptr;
			m_message = "Wine couldn't be started.";
			setState(State::Ready);
		}
	});
	m_wine->start(wineBinary(), args);
}

void WinInstallWindow::finished(int exitCode) {
	m_added.clear();
	const QSet<QString> now = wineEntries();
	for (const QString &path : now) {
		if (m_before.contains(path)) {
			continue;
		}
		const QString name = entryName(path);
		if (!name.isEmpty() && !name.startsWith("Uninstall", Qt::CaseInsensitive)) {
			m_added << name;
		}
	}
	if (m_added.isEmpty()) {
		for (const QString &dir : programDirs()) {
			if (!m_beforeDirs.contains(dir)) {
				const QString name = makeEntryFor(dir, wineBinary());
				if (!name.isEmpty()) {
					m_added << name;
				}
			}
		}
	}
	m_added.removeDuplicates();
	m_added.sort(Qt::CaseInsensitive);
	if (m_added.isEmpty() && exitCode != 0) {
		m_message = QString("The installer stopped before finishing (code %1).").arg(exitCode);
	}
	setState(State::Done);
}

void WinInstallWindow::openWithoutInstalling() {
	const QString wine = wineBinary();
	if (wine.isEmpty()) {
		m_message = "Windows support (Wine) isn't installed on this computer.";
		update();
		return;
	}
	QProcess::startDetached(wine, { m_file }, QFileInfo(m_file).absolutePath());
	close();
}

/* ---- painting ---------------------------------------------------------------------- */

void WinInstallWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	panelText(c, "Install a Windows Program", MARGIN, 30);

	/* The well: white, sunk into the window; ringed while something is dragged over it. */
	pl_fill(c, WELL.left(), WELL.top(), WELL.right(), WELL.bottom(), C_WHITE);
	pl_hline(c, WELL.left(), WELL.right(), WELL.top(), GRAY(0x8));
	pl_vline(c, WELL.left(), WELL.top(), WELL.bottom(), GRAY(0x8));
	pl_hline(c, WELL.left(), WELL.right(), WELL.bottom(), C_WHITE);
	pl_vline(c, WELL.right(), WELL.top(), WELL.bottom(), C_WHITE);
	pl_outline(c, WELL.left() + 1, WELL.top() + 1, WELL.right() - 1, WELL.bottom() - 1, GRAY(0x5));
	if (m_dropHover) {
		for (int k = 2; k <= 3; k++) {
			pl_outline(c, WELL.left() + k, WELL.top() + k, WELL.right() - k, WELL.bottom() - k,
				pl_accent_current().dark);
		}
	}
	const int iconY = WELL.top() + 34;
	pl_image(c, (W - PL_ICON_LARGE) / 2, iconY, pl_icon(PL_ICON_WINDOWS, PL_ICON_LARGE),
		PL_ICON_LARGE, PL_ICON_LARGE);
	if (m_state == State::Empty) {
		centred(c, "Drag a Windows installer here", iconY + PL_ICON_LARGE + 24);
		centred(c, "a .exe or .msi file", iconY + PL_ICON_LARGE + 42, PL_FONT_VIEWS, GRAY(0x5));
	} else {
		const QFileInfo info(m_file);
		centred(c, info.fileName(), iconY + PL_ICON_LARGE + 24);
		centred(c, QLocale().formattedDataSize(info.size(), 0, QLocale::DataSizeTraditionalFormat), iconY + PL_ICON_LARGE + 42,
			PL_FONT_VIEWS, GRAY(0x5));
	}

	/* Under the well: what happens next, or what happened. */
	int y = WELL.bottom() + 24;
	auto para = [&](const QString &s, pl_font font = PL_FONT_SYSTEM, uint32_t color = C_BLACK) {
		for (const QString &line : panelWrap(s, W - 2 * MARGIN, font)) {
			panelText(c, line, MARGIN, y, font, color);
			y += 16;
		}
	};
	switch (m_state) {
	case State::Empty: para("Or open one from Sniffer."); break;
	case State::Ready:
		para("Click Install to run it. Its own windows will guide you through.");
		break;
	case State::Preparing:
		para("Setting up Windows support (first time only)…");
		break;
	case State::Installing:
		para("Installing… Follow the installer's windows; this one waits until it's done.");
		break;
	case State::Done:
		if (!m_added.isEmpty()) {
			para("Installed: " + m_added.join(", ") + ".");
			para(m_added.size() == 1 ? "You'll find it in the Applications folder."
			                          : "You'll find them in the Applications folder.",
				PL_FONT_VIEWS, GRAY(0x4));
		} else if (m_message.isEmpty()) {
			para("The installer finished. If it installed a program, you'll find it in the "
			     "Applications folder.");
		}
		break;
	}
	if (m_state == State::Preparing || m_state == State::Installing) {
		pl_progress_paint(c, MARGIN, y + 2, W - 2 * MARGIN, m_sweepPos, pl_accent_current());
	}
	if (!m_message.isEmpty()) {
		para(m_message, PL_FONT_SYSTEM, RED);
	}

	m_host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

/* ---- input ------------------------------------------------------------------------- */

void WinInstallWindow::mousePressEvent(QMouseEvent *e) {
	if (m_host.hostPress(e)) {
		update();
	}
}

void WinInstallWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_host.hostMove(e)) {
		update();
	}
}

void WinInstallWindow::mouseReleaseEvent(QMouseEvent *e) {
	if (m_host.hostRelease(e)) {
		update();
	}
}

void WinInstallWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	QWidget::keyPressEvent(e);
}

static QString droppedInstaller(const QMimeData *mime) {
	for (const QUrl &url : mime->urls()) {
		if (url.isLocalFile() && isInstaller(url.toLocalFile())) {
			return url.toLocalFile();
		}
	}
	return QString();
}

void WinInstallWindow::dragEnterEvent(QDragEnterEvent *e) {
	const bool idle = m_state == State::Empty || m_state == State::Ready || m_state == State::Done;
	if (idle && !droppedInstaller(e->mimeData()).isEmpty()) {
		e->setDropAction(Qt::CopyAction);
		e->accept();
		m_dropHover = true;
		update();
	}
}

void WinInstallWindow::dragLeaveEvent(QDragLeaveEvent *) {
	m_dropHover = false;
	update();
}

void WinInstallWindow::dropEvent(QDropEvent *e) {
	m_dropHover = false;
	const QString path = droppedInstaller(e->mimeData());
	if (!path.isEmpty()) {
		/* Copy, not move: the Finder must leave the file where it was. */
		e->setDropAction(Qt::CopyAction);
		e->accept();
		setFile(path);
	}
	update();
}

/* Closing mid-install would end Wine's run of the installer with it. */
void WinInstallWindow::closeEvent(QCloseEvent *e) {
	if (m_state == State::Preparing || m_state == State::Installing) {
		e->ignore();
	} else {
		e->accept();
	}
}
