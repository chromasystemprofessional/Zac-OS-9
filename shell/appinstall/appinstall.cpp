#include "appinstall.h"

#include <QCloseEvent>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>
#include <utility>

#include "alert.h"
#include "icons.h"
#include "pixels.h"
#include "settings.h"

static constexpr int W = 380, H = 108;
static constexpr int TEXT_X = 60;
static constexpr uint32_t FACE = GRAY(0xD);
static const QString HELPER = QStringLiteral("/usr/libexec/zacos9/zacos9-appstore-helper");

/* ---- files and desktop entries ---------------------------------------------------- */

static QString dataDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
}

/* Where AppImages and unpacked archives live: out of the way, since
 * Applications shows them through their desktop entries. */
static QString storeDir() {
	return dataDir() + "/zacos9/apps";
}

static const QStringList ARCHIVE_SUFFIXES = {
	".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2", ".tar.zst", ".tar",
};

static QString archiveStem(const QString &fileName) {
	for (const QString &s : ARCHIVE_SUFFIXES) {
		if (fileName.endsWith(s, Qt::CaseInsensitive)) {
			return fileName.left(fileName.size() - s.size());
		}
	}
	return fileName;
}

/* "Krita-5.2.6-x86_64" -> "krita-5-2-6-x86-64": a desktop-entry-safe name. */
static QString slug(const QString &s) {
	QString out;
	for (QChar ch : s.toLower()) {
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
			out += ch;
		} else if (!out.endsWith('-')) {
			out += '-';
		}
	}
	while (out.endsWith('-')) {
		out.chop(1);
	}
	return out.isEmpty() ? QStringLiteral("app") : out;
}

/* The [Desktop Entry] keys of a desktop file (untranslated ones). */
static QHash<QString, QString> readEntry(const QString &path) {
	QHash<QString, QString> keys;
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return keys;
	}
	bool inEntry = false;
	for (const QByteArray &raw : f.readAll().split('\n')) {
		const QString line = QString::fromUtf8(raw).trimmed();
		if (line.startsWith('[')) {
			inEntry = line == "[Desktop Entry]";
		} else if (inEntry && line.contains('=')) {
			const QString key = line.section('=', 0, 0).trimmed();
			if (!key.contains('[') && !keys.contains(key)) {
				keys.insert(key, line.section('=', 1).trimmed());
			}
		}
	}
	return keys;
}

/* An argument quoted for a desktop entry's Exec (the spec's own rules). */
static QString execQuote(const QString &arg) {
	QString out = arg;
	for (const char *c : { "\\", "\"", "`", "$" }) {
		out.replace(QLatin1String(c), QLatin1String("\\") + QLatin1String(c));
	}
	return "\"" + out + "\"";
}

/* The field codes (%f %U ...) of an Exec line, to keep on a new one. */
static QString fieldCodes(const QString &exec) {
	QStringList codes;
	for (const QString &tok : exec.split(' ', Qt::SkipEmptyParts)) {
		if (tok.size() == 2 && tok[0] == '%') {
			codes << tok;
		}
	}
	return codes.join(' ');
}

static bool writeEntry(const QString &id, const QList<QPair<QString, QString>> &keys) {
	QDir().mkpath(dataDir() + "/applications");
	QFile f(dataDir() + "/applications/" + id + ".desktop");
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	QString body = "[Desktop Entry]\n";
	for (const auto &kv : keys) {
		if (!kv.second.isEmpty()) {
			body += kv.first + "=" + kv.second + "\n";
		}
	}
	f.write(body.toUtf8());
	return true;
}

/* Is this a program: a native executable or a script, not a library? */
static bool isProgram(const QFileInfo &info) {
	if (!info.isFile() || !info.isExecutable()) {
		return false;
	}
	const QString name = info.fileName().toLower();
	if (name.contains(".so") || name.endsWith(".py") || name.endsWith(".desktop")) {
		return false;
	}
	for (const char *helper : { "crash", "update", "uninstall", "install", "helper", "sandbox",
			"plugin-container", "glxtest", "vaapitest", "pingsender", "fsnotifier" }) {
		if (name.contains(QLatin1String(helper))) {
			return false;
		}
	}
	QFile f(info.filePath());
	if (!f.open(QIODevice::ReadOnly)) {
		return false;
	}
	const QByteArray head = f.read(4);
	return head == QByteArray("\x7f" "ELF", 4) || head.startsWith("#!");
}

/* ---- the window --------------------------------------------------------------------- */

static bool installable(const QString &path) {
	const QString name = QFileInfo(path).fileName().toLower();
	if (name.endsWith(".deb") || name.endsWith(".appimage")) {
		return true;
	}
	for (const QString &s : ARCHIVE_SUFFIXES) {
		if (name.endsWith(s)) {
			return true;
		}
	}
	return false;
}

AppInstallWindow::AppInstallWindow(const QStringList &files) {
	QStringList refused;
	for (const QString &f : files) {
		if (installable(f)) {
			m_queue << f;
		} else {
			refused << QFileInfo(f).fileName();
		}
	}
	if (!refused.isEmpty()) {
		m_failures << (refused.size() == 1 ? "“" + refused.front() + "” isn't"
		                                   : refused.join(", ") + " aren't") +
			" an application to install (the Applications folder takes .deb, .AppImage and "
			".tar.gz/.tar.xz files, and Windows .exe and .msi installers)";
	}
	setWindowTitle("Install");
	setFixedSize(W, H);
	m_stop = PanelButton("Stop", QRect(W - 14 - 64, H - 14 - PL_BUTTON_H, 64, PL_BUTTON_H));
	m_stop.clicked = [this] { stop(); };
	m_host.buttons = { &m_stop };
	m_sweep.setInterval(30);
	m_sweep.callOnTimeout([this] {
		m_sweepPos += 0.03;
		if (m_sweepPos > 1.3) {
			m_sweepPos = -0.3;
		}
		update();
	});
	m_sweep.start();
	m_copyStep.setInterval(0);
}

void AppInstallWindow::next() {
	m_file.clear();
	m_target.clear();
	if (m_queue.isEmpty()) {
		reportFailures();
		m_stoppable = true;
		close();
		return;
	}
	m_file = m_queue.takeFirst();
	m_name = QFileInfo(m_file).fileName();
	m_progress = -1;
	m_stoppable = true;
	m_doing = "Installing: " + m_name;
	update();
	const QString lower = m_name.toLower();
	if (lower.endsWith(".deb")) {
		installDeb();
	} else if (lower.endsWith(".appimage")) {
		installAppImage();
	} else {
		installArchive();
	}
}

void AppInstallWindow::reportFailures() {
	if (m_failures.isEmpty()) {
		return;
	}
	const QStringList failures = std::exchange(m_failures, {});
	/* The alert is one paragraph: no line breaks. */
	Alert::ask((failures.size() == 1 ? QString("This couldn't be installed: ")
	                                 : QString("These couldn't be installed: ")) +
		failures.join("; ") + ".", "OK", QString());
}

void AppInstallWindow::fail(const QString &why) {
	m_failures << m_name + ": " + why;
	next();
}

void AppInstallWindow::succeed() {
	m_progress = 1;
	m_stoppable = false;
	m_doing = "Installed: " + m_name;
	update();
	QTimer::singleShot(400, this, [this] { next(); });
}

void AppInstallWindow::stop() {
	if (!m_stoppable || m_file.isEmpty()) {
		return;
	}
	m_queue.clear();
	if (m_proc) {
		m_proc->disconnect();
		m_proc->kill();
		m_proc->deleteLater();
		m_proc = nullptr;
	}
	if (m_copyStep.isActive()) {
		m_copyStep.stop();
		m_copyStep.disconnect();
		m_dst->remove();
		delete m_src;
		delete m_dst;
		m_src = m_dst = nullptr;
	}
	if (!m_target.isEmpty() && QFileInfo(m_target).isDir()) {
		QDir(m_target).removeRecursively();
	}
	next();
}

void AppInstallWindow::run(const QString &program, const QStringList &args,
		std::function<void(int)> done, std::function<void(const QString &)> line) {
	m_procErr.clear();
	m_proc = new QProcess(this);
	QProcess *p = m_proc;
	connect(p, &QProcess::readyReadStandardOutput, this, [p, line] {
		while (p->canReadLine()) {
			const QString l = QString::fromUtf8(p->readLine()).trimmed();
			if (line && !l.isEmpty()) {
				line(l);
			}
		}
	});
	connect(p, &QProcess::readyReadStandardError, this, [this, p] {
		for (const QByteArray &raw : p->readAllStandardError().split('\n')) {
			if (!raw.trimmed().isEmpty()) {
				m_procErr = QString::fromUtf8(raw).trimmed();
			}
		}
	});
	connect(p, &QProcess::finished, this, [this, p, done](int code, QProcess::ExitStatus status) {
		if (m_proc == p) {
			m_proc = nullptr;
		}
		p->deleteLater();
		done(status == QProcess::NormalExit ? code : -1);
	});
	connect(p, &QProcess::errorOccurred, this, [this, p, done](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart) {
			return;
		}
		if (m_proc == p) {
			m_proc = nullptr;
		}
		p->deleteLater();
		done(-1);
	});
	p->start(program, args);
}

/* ---- .deb ----------------------------------------------------------------------------- */

void AppInstallWindow::installDeb() {
	auto fields = std::make_shared<QHash<QString, QString>>();
	run("dpkg-deb", { "-f", m_file, "Package", "Version", "Maintainer" }, [this, fields](int code) {
		if (code != 0 || fields->value("Package").isEmpty()) {
			fail("it isn't a Debian package.");
			return;
		}
		const QString pkg = fields->value("Package");
		m_name = pkg;
		const QString who = fields->value("Maintainer").section('<', 0, 0).trimmed();
		const QString message = "Install “" + pkg + "” " + fields->value("Version") +
			(who.isEmpty() ? QString() : ", from " + who) + "? A package's setup runs with "
			"full access to this computer, so only install software from publishers you trust.";
		if (!Alert::ask(message, "Install", "Cancel")) {
			next();
			return;
		}
		/* apt can't be stopped safely part way through. */
		m_stoppable = false;
		m_doing = "Installing: " + pkg;
		update();
		auto dl = std::make_shared<double>(-1), pm = std::make_shared<double>(-1);
		run("pkexec", { HELPER, "install-deb", m_file }, [this](int code) {
			if (code == 0) {
				succeed();
			} else {
				fail(m_procErr.isEmpty() ? QString("apt couldn't install it.") : m_procErr);
			}
		}, [this, dl, pm](const QString &l) {
			/* APT::Status-Fd: "dlstatus:N:PERCENT:MESSAGE", "pmstatus:PKG:PERCENT:MESSAGE". */
			const QStringList f = l.split(':');
			if (f.size() < 4) {
				return;
			}
			const double pct = f[2].toDouble();
			const QString msg = f.mid(3).join(':');
			if (f[0] == "dlstatus") {
				*dl = pct;
				m_doing = msg;
			} else if (f[0] == "pmstatus") {
				*pm = pct;
				m_doing = msg;
			} else if (f[0] == "pmerror") {
				m_procErr = msg;
			}
			if (*pm >= 0) {
				m_progress = *dl >= 0 ? 0.4 + *pm / 100 * 0.6 : *pm / 100;
			} else if (*dl >= 0) {
				m_progress = *dl / 100 * 0.4;
			}
			update();
		});
	}, [fields](const QString &l) {
		fields->insert(l.section(':', 0, 0).trimmed(), l.section(':', 1).trimmed());
	});
}

/* ---- .AppImage ------------------------------------------------------------------------ */

void AppInstallWindow::installAppImage() {
	QDir().mkpath(storeDir());
	m_target = storeDir() + "/" + m_name;
	m_src = new QFile(m_file);
	m_dst = new QFile(m_target + ".part");
	if (!m_src->open(QIODevice::ReadOnly) || !m_dst->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		delete m_src;
		delete m_dst;
		m_src = m_dst = nullptr;
		fail("it couldn't be copied.");
		return;
	}
	m_doing = "Copying: " + m_name;
	const qint64 size = std::max<qint64>(1, m_src->size());
	connect(&m_copyStep, &QTimer::timeout, this, [this, size] {
		const QByteArray chunk = m_src->read(1 << 20);
		if (!chunk.isEmpty() && m_dst->write(chunk) != chunk.size()) {
			m_copyStep.stop();
			m_copyStep.disconnect();
			m_dst->remove();
			delete m_src;
			delete m_dst;
			m_src = m_dst = nullptr;
			fail("there isn't room for it.");
			return;
		}
		m_progress = double(m_dst->pos()) / size;
		update();
		if (m_src->atEnd()) {
			m_copyStep.stop();
			m_copyStep.disconnect();
			m_dst->close();
			QFile::remove(m_target);
			m_dst->rename(m_target);
			QFile::setPermissions(m_target, QFile::permissions(m_target) | QFile::ExeOwner |
				QFile::ExeGroup | QFile::ExeOther);
			delete m_src;
			delete m_dst;
			m_src = m_dst = nullptr;
			appImageCopied();
		}
	});
	m_copyStep.start();
}

/* Its name and icon, from the desktop entry and icon every AppImage
 * carries at its root (the AppImage runtime extracts them without FUSE). */
void AppInstallWindow::appImageCopied() {
	m_stoppable = false;
	m_progress = -1;
	m_doing = "Finishing: " + m_name;
	update();
	auto tmp = std::make_shared<QTemporaryDir>();
	const QString root = tmp->path() + "/squashfs-root";

	/* One file (or pattern) out of the AppImage. */
	auto extract = [this, tmp](const QString &pattern, std::function<void()> then) {
		QProcess *p = new QProcess(this);
		p->setWorkingDirectory(tmp->path());
		p->setStandardOutputFile(QProcess::nullDevice());
		p->setStandardErrorFile(QProcess::nullDevice());
		connect(p, &QProcess::finished, this, [p, then] {
			p->deleteLater();
			then();
		});
		connect(p, &QProcess::errorOccurred, this, [p, then](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				p->deleteLater();
				then();
			}
		});
		p->start(m_target, { "--appimage-extract", pattern });
	};
	/* The files at an AppImage's root are usually links into usr/share;
	 * extracting a link alone leaves it dangling, so fetch what it points
	 * at too (a few links deep). `then` gets the real file, or "". */
	auto resolve = std::make_shared<std::function<void(const QString &, int, std::function<void(QString)>)>>();
	*resolve = [extract, root, resolve](const QString &rel, int depth,
			std::function<void(QString)> then) {
		const QFileInfo info(root + "/" + rel);
		if (!info.isSymLink()) {
			then(info.isFile() ? info.filePath() : QString());
			return;
		}
		QString target = info.symLinkTarget();
		if (depth > 4 || !target.startsWith(root + "/")) {
			then(QString());
			return;
		}
		target = target.mid(root.size() + 1);
		extract(target, [resolve, target, depth, then] { (*resolve)(target, depth + 1, then); });
	};

	extract("*.desktop", [this, root, extract, resolve] {
		const QStringList found = QDir(root).entryList({ "*.desktop" }, QDir::Files | QDir::System);
		auto withEntry = [this, root, extract, resolve](QHash<QString, QString> entry) {
			const QString iconName = entry.value("Icon");
			auto finish = [this, entry](const QString &iconFile) {
				QString icon;
				if (!iconFile.isEmpty()) {
					const QString ext = iconFile.endsWith(".svg", Qt::CaseInsensitive) ? ".svg" : ".png";
					icon = storeDir() + "/" + slug(QFileInfo(m_target).completeBaseName()) + ext;
					QFile::remove(icon);
					QFile::copy(iconFile, icon);
				}
				QString name = entry.value("Name");
				if (name.isEmpty()) {
					name = QFileInfo(m_target).completeBaseName().section('-', 0, 0);
				}
				const QString codes = fieldCodes(entry.value("Exec"));
				const bool ok = writeEntry("appimage-" + slug(name), {
					{ "Type", "Application" },
					{ "Name", name },
					{ "Comment", entry.value("Comment") },
					{ "Exec", execQuote(m_target) + (codes.isEmpty() ? QString() : " " + codes) },
					{ "Icon", icon.isEmpty() ? entry.value("Icon") : icon },
					{ "Categories", entry.value("Categories") },
					{ "Terminal", entry.value("Terminal") },
					{ "StartupWMClass", entry.value("StartupWMClass") },
					{ "X-ZacOS9-Installed", m_target },
				});
				m_name = name;
				if (ok) {
					succeed();
				} else {
					fail("its Applications entry couldn't be written.");
				}
			};
			/* The icon the entry names (an SVG or PNG at the root), else .DirIcon. */
			auto dirIcon = [extract, resolve, finish] {
				extract(".DirIcon", [resolve, finish] { (*resolve)(".DirIcon", 0, finish); });
			};
			if (iconName.isEmpty()) {
				dirIcon();
				return;
			}
			extract(iconName + ".svg", [extract, resolve, iconName, finish, dirIcon] {
				(*resolve)(iconName + ".svg", 0, [extract, resolve, iconName, finish, dirIcon](QString svg) {
					if (!svg.isEmpty()) {
						finish(svg);
						return;
					}
					extract(iconName + ".png", [resolve, iconName, finish, dirIcon] {
						(*resolve)(iconName + ".png", 0, [finish, dirIcon](QString png) {
							if (!png.isEmpty()) {
								finish(png);
							} else {
								dirIcon();
							}
						});
					});
				});
			});
		};
		if (found.isEmpty()) {
			withEntry({});
			return;
		}
		(*resolve)(found.front(), 0, [withEntry](QString file) {
			withEntry(file.isEmpty() ? QHash<QString, QString>() : readEntry(file));
		});
	});
}

/* ---- .tar.* --------------------------------------------------------------------------- */

void AppInstallWindow::installArchive() {
	m_target = storeDir() + "/" + slug(archiveStem(m_name));
	if (QFileInfo(m_target).exists()) {
		QDir(m_target).removeRecursively(); /* a newer copy replaces an older one */
	}
	auto total = std::make_shared<int>(0);
	m_doing = "Opening: " + m_name;
	update();
	run("tar", { "-tf", m_file }, [this, total](int code) {
		if (code != 0 || *total == 0) {
			fail("it isn't an archive tar can open.");
			return;
		}
		QDir().mkpath(m_target);
		m_doing = "Unpacking: " + m_name;
		m_progress = 0;
		update();
		auto done = std::make_shared<int>(0);
		run("tar", { "-xvf", m_file, "-C", m_target }, [this](int code) {
			if (code != 0) {
				QDir(m_target).removeRecursively();
				fail("it couldn't be unpacked.");
				return;
			}
			archiveUnpacked();
		}, [this, total, done](const QString &) {
			++*done;
			m_progress = std::min(1.0, double(*done) / *total);
			update();
		});
	}, [total](const QString &) { ++*total; });
}

/* Which program in it to start, and with what name and icon: a desktop
 * entry inside says so; otherwise there must be one clear program. */
void AppInstallWindow::archiveUnpacked() {
	m_stoppable = false;
	m_progress = -1;
	m_doing = "Finishing: " + m_name;
	update();
	std::vector<QFileInfo> programs, icons;
	QHash<QString, QString> entry;
	const int baseDepth = m_target.count('/');
	QDirIterator it(m_target, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
	while (it.hasNext()) {
		const QFileInfo info(it.next());
		const int depth = info.filePath().count('/') - baseDepth;
		const QString lower = info.fileName().toLower();
		if (lower.endsWith(".desktop") && depth <= 4 && entry.isEmpty()) {
			const auto keys = readEntry(info.filePath());
			if (keys.value("Type") == "Application" && !keys.value("Exec").isEmpty()) {
				entry = keys;
			}
		} else if (lower.endsWith(".png") || lower.endsWith(".svg")) {
			icons.push_back(info);
		} else if (depth <= 3 && isProgram(info)) {
			programs.push_back(info);
		}
	}

	QFileInfo program;
	if (!entry.isEmpty()) {
		/* The entry names the program as installed somewhere else: find it here. */
		QString exe = entry.value("Exec").split(' ', Qt::SkipEmptyParts).value(0);
		exe = QFileInfo(exe.remove('"')).fileName();
		for (const QFileInfo &p : programs) {
			if (p.fileName() == exe) {
				program = p;
				break;
			}
		}
	}
	if (!program.exists()) {
		if (programs.size() == 1) {
			program = programs.front();
		} else {
			/* "blender-4.2.1-linux-x64" -> "blender": the program named like the archive. */
			const QString first = slug(archiveStem(m_name)).section('-', 0, 0);
			std::vector<QFileInfo> named;
			for (const QFileInfo &p : programs) {
				if (p.completeBaseName().toLower() == first || p.baseName().toLower() == first) {
					named.push_back(p);
				}
			}
			if (named.size() == 1) {
				program = named.front();
			}
		}
	}
	if (!program.exists()) {
		QDir(m_target).removeRecursively();
		fail(programs.empty() ? QString("there's no program inside it to start.")
			: QString("it has several programs inside, and nothing says which one starts it."));
		return;
	}

	const QString stem = program.baseName().toLower();
	QString icon;
	const QString wanted = entry.value("Icon").toLower();
	qint64 best = -1;
	for (const QFileInfo &i : icons) {
		const QString base = i.completeBaseName().toLower();
		const bool match = (!wanted.isEmpty() && (base == wanted || i.filePath() == entry.value("Icon")))
			|| base.contains(stem);
		if (!match) {
			continue;
		}
		const qint64 score = i.suffix().toLower() == "svg" ? (1LL << 40) : i.size();
		if (score > best) {
			best = score;
			icon = i.filePath();
		}
	}
	QString name = entry.value("Name");
	if (name.isEmpty()) {
		name = stem.left(1).toUpper() + stem.mid(1);
	}
	const QString codes = fieldCodes(entry.value("Exec"));
	const bool ok = writeEntry("archive-" + slug(name), {
		{ "Type", "Application" },
		{ "Name", name },
		{ "Comment", entry.value("Comment") },
		{ "Exec", execQuote(program.filePath()) + (codes.isEmpty() ? QString() : " " + codes) },
		{ "Path", program.absolutePath() },
		{ "Icon", icon.isEmpty() ? entry.value("Icon") : icon },
		{ "Categories", entry.value("Categories") },
		{ "Terminal", entry.value("Terminal") },
		{ "X-ZacOS9-Installed", m_target },
	});
	m_name = name;
	if (ok) {
		succeed();
	} else {
		fail("its Applications entry couldn't be written.");
	}
}

/* ---- painting and input ----------------------------------------------------------------- */

void AppInstallWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	pl_image(c, 14, 16, pl_icon(PL_ICON_APPLICATION, PL_ICON_LARGE), PL_ICON_LARGE, PL_ICON_LARGE);
	const int remaining = static_cast<int>(m_queue.size()) + (m_file.isEmpty() ? 0 : 1);
	panelText(c, QString("Items remaining to be installed: %1").arg(remaining), TEXT_X, 26);
	panelText(c, m_doing, TEXT_X, 44, PL_FONT_VIEWS, C_BLACK, W - TEXT_X - 14);
	pl_progress_paint(c, TEXT_X, 52, W - TEXT_X - 14, m_progress < 0 ? m_sweepPos : m_progress,
		pl_accent_current());
	m_stop.enabled = m_stoppable && !m_file.isEmpty();
	m_host.paintControls(c, FACE);
	QPainter p(this);
	px.blit(p);
}

void AppInstallWindow::mousePressEvent(QMouseEvent *e) {
	if (m_host.hostPress(e)) {
		update();
	}
}

void AppInstallWindow::mouseMoveEvent(QMouseEvent *e) {
	if (m_host.hostMove(e)) {
		update();
	}
}

void AppInstallWindow::mouseReleaseEvent(QMouseEvent *e) {
	if (m_host.hostRelease(e)) {
		update();
	}
}

void AppInstallWindow::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape ||
			((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Period)) {
		stop();
		return;
	}
	QWidget::keyPressEvent(e);
}

/* The close box is Stop for everything left; not while apt is running. */
void AppInstallWindow::closeEvent(QCloseEvent *e) {
	if (!m_file.isEmpty() && !m_stoppable) {
		e->ignore();
		return;
	}
	if (!m_file.isEmpty()) {
		stop();
	}
	e->accept();
}
