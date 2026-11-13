#include "customthemes.h"
#include "conf.h"
#include "programs.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMap>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include "custompatterns.h"
#include "settings.h"
#include "widgets.h"

namespace {
/* A resource fork by content, mirroring the extractor's own check: a
 * 16-byte header naming data and map sections that exactly fill the file,
 * else a tail map offset pointing back into it. AppleDouble files carry
 * the fork instead (magic 0x00051607). */
static bool looksLikeResourceFork(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly) || f.size() < 16) {
		return false;
	}
	const QByteArray head = f.read(16);
	auto u32 = [&head](int i) {
		return (quint32)(uchar)head[i] << 24 | (quint32)(uchar)head[i + 1] << 16 |
			(quint32)(uchar)head[i + 2] << 8 | (quint32)(uchar)head[i + 3];
	};
	const quint64 size = (quint64)f.size();
	if (u32(0) + (quint64)u32(8) + u32(12) == size && u32(4) > 0 &&
			u32(4) < size && u32(8) > 0 && u32(12) > 0) {
		return true;
	}
	if (!f.seek(size - 4)) {
		return false;
	}
	const QByteArray tail = f.read(4);
	const quint32 map = (quint32)(uchar)tail[0] << 24 | (quint32)(uchar)tail[1] << 16 |
		(quint32)(uchar)tail[2] << 8 | (quint32)(uchar)tail[3];
	return 0 < map + 28 && map + 28 <= size;
}

static bool isAppleDouble(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) && f.read(4) == QByteArray("\x00\x05\x16\x07", 4);
}

/* The installable fork for a dropped file: the file itself when it carries
 * one, else its AppleDouble (._name) or Netatalk .rsrc/name companion. */
static QString resolveThemeFork(const QFileInfo &file) {
	const QString path = file.absoluteFilePath();
	if (isAppleDouble(path) || looksLikeResourceFork(path)) {
		return path;
	}
	const QString dir = file.absolutePath(), base = file.fileName();
	for (const QString &cand : {dir + "/._" + base, dir + "/.rsrc/" + base,
			QDir::homePath() + "/.rsrc/" + base, QDir::homePath() + "/._" + base}) {
		if (QFileInfo::exists(cand) && (isAppleDouble(cand) || looksLikeResourceFork(cand))) {
			return cand;
		}
	}
	return QString();
}

static bool themeCompanionExists(const QFileInfo &file) {
	const QString dir = file.absolutePath(), base = file.fileName();
	return QFileInfo::exists(dir + "/._" + base) || QFileInfo::exists(dir + "/.rsrc/" + base);
}

/* Whether a bundle's theme.conf already names this fork as its source
 * and its folder still holds a preset: an installed theme is never
 * imported twice, while a re-dropped folder whose preset was deleted
 * regenerates it. */
/* The extractor's own SHA-256: a bundle records it when it is built, so a
 * ZacOS update that can decode more than the old one rebuilds the bundle
 * instead of keeping what the old extractor could not fill in. */
static QString extractorHash() {
	QFile f(themeExtractorPath());
	if (!f.open(QIODevice::ReadOnly)) {
		return QString();
	}
	return QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
}

static bool themeSourceImported(const QString &fork) {
	if (fork.isEmpty()) {
		return false;
	}
	const QDir forkDir = QFileInfo(fork).dir();
	if (QDir(forkDir).entryInfoList(QStringList("*.json"), QDir::Files).isEmpty()) {
		return false;
	}
	const QString want = extractorHash();
	const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes";
	for (const QFileInfo &bundle : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		QFile conf(bundle.filePath() + "/theme.conf");
		if (!conf.open(QIODevice::ReadOnly | QIODevice::Text)) {
			continue;
		}
		QTextStream in(&conf);
		QString line, source, hash;
		while (in.readLineInto(&line)) {
			if (line.startsWith("source=")) {
				source = line.mid(7);
			} else if (line.startsWith("extractor-sha256=")) {
				hash = line.mid(17);
			}
		}
		if (source != fork) {
			continue;
		}
		if (!want.isEmpty() && hash != want) {
			continue; /* built by an older extractor: import again */
		}
		return true;
	}
	return false;
}

static QString fileFingerprint(const QFileInfo &file) {
	return file.absoluteFilePath() + "\n" + QString::number(file.size()) + "\n" +
		QString::number(file.lastModified().toMSecsSinceEpoch());
}

const QStringList events{ "button-click", "checkbox-toggle", "menu-open", "menu-command",
	"window-open", "window-close", "trash-move", "trash-empty",
	"window-collapse", "window-expand", "window-drag", "window-drag-end",
	"startup", "shutdown" };
struct Theme {
	QString id, name;
	QMap<QString, QString> values;
	QString path; /* preset file, for the editor (empty: built in) */
};
struct Watch {
	QPointer<QObject> owner;
	std::function<void()> changed;
};
QString setting(const char *key) {
	char value[128];
	return pl_setting(key, value, sizeof(value)) ? QString::fromUtf8(value) : QString();
}

static QString setting(const QString &key) {
	return setting(key.toUtf8().constData());
}

bool writeSettings(const QMap<QString, QString> &values, QString *error) {
	const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9";
	if (!QDir().mkpath(folder)) {
		*error = "Could not create appearance preferences folder.";
		return false;
	}
	const QString path = folder + "/desktop.conf";
	if (QFile::exists(path) && !QFile(path).open(QIODevice::ReadOnly)) {
		*error = "Could not read " + path + ".";
		return false;
	}
	KeyValueFile conf = QFile::exists(path) ? KeyValueFile::read(path) : KeyValueFile::parse("[General]");
	for (auto it = values.cbegin(); it != values.cend(); ++it) {
		if (it.value().isEmpty()) {
			conf.remove(it.key());
			continue;
		}
		const QString line = it.key() + "=" + it.value();
		if (line.toUtf8().size() >= 255 || line.contains('\n') || line.contains('\r')) {
			*error = "Theme preference is too long or contains a line break.";
			return false;
		}
		conf.set(it.key(), it.value());
	}
	if (conf.text().count('\n') > 64) {
		*error = "Too many appearance preferences.";
		return false;
	}
	return conf.save(path, error);
}

QString cacheFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/zacos9/sounds";
}

bool alertSoundAvailable(const QString &id) {
	if (!QRegularExpression("\\A[A-Za-z0-9_-]{1,48}\\z").match(id).hasMatch()) {
		return false;
	}
	for (const QString &path : {
			userAlertSoundsFolder() + "/" + id + ".wav",
			QString::fromUtf8(pl_data_dir()) + "/sounds/" + id + ".wav" }) {
		const QFileInfo file(path);
		if (!file.isSymLink() && file.isFile()) {
			return true;
		}
	}
	return false;
}

/* Point installed bundles at moved forks: theme.conf's source= line names
 * the fork each bundle was built from. */
static void rewriteBundleSource(const QString &from, const QString &to);

/* A dropped theme's own folder: Theme Files/<base>/ holds the data fork,
 * its AppleDouble companion and the generated preset, so the install's
 * paperwork hides behind the theme file. Moves a root-level drop (a data
 * file with its companion, or a lone fork) in and returns the moved
 * file's new path, or "" when there is nothing to wrap. */
static QString wrapThemeFile(const QFileInfo &file, const QString &destRoot) {
	const QString dirPath = file.absolutePath();
	QString base = file.fileName();
	if (base.startsWith("._")) {
		base = base.mid(2);
	}
	if (base.isEmpty() || base == "." || base == "..") {
		return QString();
	}
	const QString folder = destRoot + "/" + base;
	const QString data = dirPath + "/" + base;
	const QString companion = dirPath + "/._" + base;
	if (dirPath == folder) {
		return file.absoluteFilePath();
	}
	/* The folder takes the data file's own name: move the file aside,
	 * make the folder, then file both companions inside. */
	const QString stash = folder + ".incoming";
	QFile::remove(stash);
	const bool hasData = QFileInfo::exists(data) && !QFileInfo(data).isDir();
	if (hasData && !QFile::rename(data, stash)) {
		return QString();
	}
	if (!QDir().mkpath(folder)) {
		if (hasData) {
			QFile::rename(stash, data);
		}
		return QString();
	}
	if (hasData && !QFile::rename(stash, folder + "/" + base)) {
		QFile::rename(stash, data);
		return QString();
	}
	if (QFileInfo::exists(companion) && !QFileInfo::exists(folder + "/._" + base) &&
			!QFile::rename(companion, folder + "/._" + base)) {
		qWarning() << "Could not file the theme drop:" << companion;
	}
	if (QFileInfo::exists(folder + "/" + base)) {
		return folder + "/" + base;
	}
	if (QFileInfo::exists(folder + "/._" + base)) {
		return folder + "/._" + base;
	}
	return QString();
}

/* File one loose drop into a folder under the new root, keeping installed
 * bundles pointed at the moved fork so nothing reimports. */
static bool migrateOneLooseTheme(const QFileInfo &file, const QString &destRoot) {
	const QString fork = resolveThemeFork(file);
	if (fork.isEmpty()) {
		return false;
	}
	const QString wrapped = wrapThemeFile(file, destRoot);
	if (wrapped.isEmpty() || wrapped == file.absoluteFilePath()) {
		return false;
	}
	const QString newFork = resolveThemeFork(QFileInfo(wrapped));
	rewriteBundleSource(fork, newFork.isEmpty() ? wrapped : newFork);
	return true;
}

/* File the loose drops left by older ZacOS 9 (a data fork and its
 * companion straight in Theme Files, or still in the old Themes folder)
 * into their own folders, keeping installed bundles pointed at the moved
 * forks so nothing reimports. Runs until both folders are clean. */
/* Presets belong with their theme: move each legacy preset next to the
 * bundle its theme-bundle names (via that bundle's recorded source);
 * presets naming no installed bundle land in the new root. */
static void migrateLoosePresets() {
	const QString root = appearanceThemesFolder();
	const QString legacy = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/appearance/Themes";
	if (!QDir(legacy).exists() && !QDir(root).exists()) {
		return;
	}
	QMap<QString, QString> bundleDir;
	const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes";
	for (const QFileInfo &bundle : QDir(config).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		QFile conf(bundle.filePath() + "/theme.conf");
		if (!conf.open(QIODevice::ReadOnly | QIODevice::Text)) {
			continue;
		}
		QString name, source;
		QTextStream in(&conf);
		QString line;
		while (in.readLineInto(&line)) {
			if (line.startsWith("name=")) {
				name = line.mid(5);
			} else if (line.startsWith("source=")) {
				source = line.mid(7);
			}
		}
		if (!name.isEmpty() && !source.isEmpty()) {
			bundleDir[name] = QFileInfo(source).absolutePath();
		}
	}
	for (const QString &dirPath : {legacy, root}) {
		QDir dir(dirPath);
		if (!dir.exists()) {
			continue;
		}
		for (const QFileInfo &file : dir.entryInfoList(
				QStringList("*.json"), QDir::Files | QDir::Hidden, QDir::Name)) {
			QFile input(file.filePath());
			if (!input.open(QIODevice::ReadOnly)) {
				continue;
			}
			const QJsonObject settings = QJsonDocument::fromJson(input.readAll()).object().value("settings").toObject();
			QString dest = bundleDir.value(settings.value("theme-bundle").toString(), root);
			input.close();
			if (dirPath == root && dest == root) {
				continue; /* already home */
			}
			if (QFileInfo::exists(dest + "/" + file.fileName())) {
				continue;
			}
			if (!QDir().mkpath(dest)) {
				continue;
			}
			if (!QFile::rename(file.filePath(), dest + "/" + file.fileName())) {
				qWarning() << "Could not file the theme preset:" << file.fileName();
			}
		}
	}
}

static void migrateLooseThemes() {
	const QString root = appearanceThemesFolder();
	if (!QDir().mkpath(root)) {
		return;
	}
	const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/appearance";
	const QString legacy = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/appearance/Themes";
	/* A theme file dropped into Appearance itself (or left there by an
	 * older ZacOS 9) still makes its way into Theme Files, as dropping
	 * one anywhere in the System Folder does in the Finder. */
	for (const QString &dirPath : {root, base, legacy}) {
		QDir dir(dirPath);
		if (!dir.exists()) {
			continue;
		}
		for (const QFileInfo &file : dir.entryInfoList(
				QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
			const QString name = file.fileName();
			if (name == ".DS_Store" || file.suffix().toLower() == "json") {
				continue;
			}
			if (name.startsWith("._") && QFileInfo::exists(dirPath + "/" + name.mid(2))) {
				continue;
			}
			migrateOneLooseTheme(file, root);
		}
	}
	if (QDir(legacy).exists() && QDir(legacy).entryInfoList(
			QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()) {
		QDir().rmdir(legacy);
	}
	migrateLoosePresets();
}

/* Point installed bundles at moved forks: theme.conf's source= line names
 * the fork each bundle was built from. */
static void rewriteBundleSource(const QString &from, const QString &to) {
	if (from.isEmpty() || from == to) {
		return;
	}
	const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes";
	for (const QFileInfo &bundle : QDir(config).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		const QString conf = bundle.filePath() + "/theme.conf";
		QFile f(conf);
		if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
			continue;
		}
		bool hit = false;
		QStringList kept;
		QTextStream in(&f);
		QString line;
		while (in.readLineInto(&line)) {
			if (line == "source=" + from) {
				line = "source=" + to;
				hit = true;
			}
			kept << line;
		}
		f.close();
		if (!hit) {
			continue;
		}
		if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
			continue;
		}
		QTextStream out(&f);
		for (const QString &keptLine : kept) {
			out << keptLine << "\n";
		}
	}
}

/* Mac OS 9 calls the drop folder Theme Files; older ZacOS 9 used Themes.
 * Move it (and any installed bundles' recorded source fork) across once. */
static void moveThemesFolder() {
	const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/appearance";
	const QString from = base + "/Themes", to = base + "/Theme Files";
	QDir dir(from);
	if (!dir.exists() || QDir(to).exists()) {
		return;
	}
	if (!QDir().rename(from, to)) {
		qWarning() << "Could not rename the Themes folder to Theme Files.";
		return;
	}
	const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes";
	for (const QFileInfo &bundle : QDir(config).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		const QString conf = bundle.filePath() + "/theme.conf";
		QFile f(conf);
		if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
			continue;
		}
		const QString text = QString::fromUtf8(f.readAll());
		f.close();
		if (!text.contains("source=" + from + "/")) {
			continue;
		}
		QString fixed = text;
		fixed.replace("source=" + from + "/", "source=" + to + "/");
		if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
			continue;
		}
		f.write(fixed.toUtf8());
	}
}

/* Bundles named for an AppleDouble companion (".-prefixed) can only
 * be leftovers from before the installer stopped importing companions
 * directly: nameFromFile() strips the "._" prefix, so no new bundle can
 * ever be created under such a name. Sweep them so their presets stop
 * resolving to stale art. */
static void sweepCompanionBundles() {
	const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes";
	QDir dir(root);
	if (!dir.exists()) {
		return;
	}
	for (const QFileInfo &bundle : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden)) {
		if (!bundle.fileName().startsWith("._")) {
			continue;
		}
		QDir(bundle.filePath()).removeRecursively();
	}
}

class Catalog : public QObject {
public:
	~Catalog() override {
		/* Closing the Appearance panel while the catalogue helper runs
		 * would otherwise destroy a running QProcess. */
		if (process.state() != QProcess::NotRunning) {
			process.kill();
			QElapsedTimer timer;
			timer.start();
			while (process.state() != QProcess::NotRunning && timer.elapsed() < 1000) {
				QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 10);
			}
		}
	}
	QList<Theme> sounds{{ "none", "None", {}, {} }, { "original", "ZacOS Original", {}, {} }};
	QList<Theme> presets{{ "platinum", "Silver Coat", {
		{ "accent", "" }, { "highlight", "" }, { "pattern", "" },
		{ "background", "pattern" }, { "wallpaper", "" },
		{ "wallpaper-mode", "fit" }, { "alert-sound", "" }, { "sound-theme", "none" }
	}, {} }};
	QStringList errors, soundErrors, importErrors;
	QList<Watch> watches;
	QProcess process;
	QTimer poll, timeout;
	QByteArray fingerprint;
	/* Installing a dropped Mac OS 9 theme file: one import at a time,
	 * through the Theme Installer, in whatever process gets there first. */
	QProcess importer;
	QTimer importTimeout;
	QStringList importQueue;
	QString importCurrent;
	QMap<QString, QString> importSeen;   /* path -> fingerprint, settling */
	QMap<QString, QString> importFailed; /* path -> fingerprint, refused */
	std::unique_ptr<QLockFile> importLock;

	Catalog() : QObject(QCoreApplication::instance()) {
		moveThemesFolder();
		sweepCompanionBundles();
		for (const QString &folder : { soundThemesFolder(), appearanceThemesFolder(), cacheFolder() }) {
			if (!QDir().mkpath(folder)) {
				qWarning().noquote() << "Could not create theme folder:" << folder;
			}
		}
		poll.setInterval(2000);
		importTimeout.setSingleShot(true);
		importTimeout.setInterval(60000);
		const QString runDir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
		if (!runDir.isEmpty()) {
			importLock.reset(new QLockFile(runDir + "/zacos9-theme-import.lock"));
		}
		connect(&importTimeout, &QTimer::timeout, this, [this] {
			importer.kill();
			finishImport(-1, QProcess::CrashExit);
		});
		connect(&importer, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) { finishImport(code, status); });
		connect(&importer, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				finishImport(-1, QProcess::CrashExit);
			}
		});

		timeout.setSingleShot(true);
		timeout.setInterval(30000);
		connect(&poll, &QTimer::timeout, this, [this] { refresh(); });
		connect(&timeout, &QTimer::timeout, this, [this] {
			soundErrors = { "Sound theme import timed out." };
			process.kill();
			notify();
		});
		connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				timeout.stop();
				soundErrors = { "Sound theme importer could not start: " + process.errorString() };
				notify();
			}
		});
		connect(&process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
			timeout.stop();
			if (code || status != QProcess::NormalExit) {
				soundErrors = { "Sound theme import failed: " +
					QString::fromUtf8(process.readAllStandardError()).trimmed() };
				notify();
				return;
			}
			QJsonParseError parseError;
			const QJsonDocument doc = QJsonDocument::fromJson(process.readAllStandardOutput(), &parseError);
			if (!doc.isObject() || !doc.object().value("themes").isArray() ||
					!doc.object().value("errors").isArray()) {
				soundErrors = { "Invalid sound theme catalog: " + parseError.errorString() };
				notify();
				return;
			}
			QList<Theme> next{ sounds[0], sounds[1] };
			soundErrors.clear();
			for (const QJsonValue &v : doc.object().value("errors").toArray()) {
				soundErrors << v.toString();
			}
			for (const QJsonValue &v : doc.object().value("themes").toArray()) {
				const QJsonObject o = v.toObject();
				Theme theme{ o.value("id").toString(), o.value("name").toString(), {}, {} };
				bool valid = !theme.id.isEmpty() && theme.id.size() < 100 && !theme.name.isEmpty();
				const QJsonObject mapping = o.value("events").toObject();
				for (auto it = mapping.begin(); it != mapping.end(); ++it) {
					const QFileInfo file(it.value().toString());
					if (!events.contains(it.key()) || file.isSymLink() || !file.isFile() ||
							file.absolutePath() != cacheFolder() || file.suffix() != "wav") {
						valid = false;
						break;
					}
					theme.values.insert("sound." + it.key(), file.fileName());
				}
				if (valid && !theme.values.isEmpty()) {
					next << theme;
				} else {
					soundErrors << "Invalid or empty sound theme: " + theme.name;
				}
			}
			sounds = next;
			const QString selected = setting("sound-theme");
			if (selected != "none" && selected != "original" && !selected.isEmpty()) {
				bool found = false;
				for (int i = 2; i < sounds.size(); ++i) {
					if (sounds[i].id == selected) {
						QString error;
						found = true;
						if (!applySoundTheme(i, &error)) {
							soundErrors << error;
						}
						break;
					}
				}
				if (!found) {
					QMap<QString, QString> cleared;
					for (const QString &event : events) {
						cleared.insert("sound." + event, "");
					}
					QString error;
					if (!writeSettings(cleared, &error)) {
						soundErrors << error;
					}
				}
			}
			notify();
		});
		poll.start();
		QTimer::singleShot(0, this, [this] { refresh(); });
	}

	/* A dropped Mac OS 9 theme file, or nothing to do: files that are
	 * already installed, still landing, refused before, or plain documents
	 * (which keep an import error) never reach the installer. Returns true
	 * when the file was newly queued. */
	bool handleDroppedTheme(const QFileInfo &file) {
		const QString path = file.absoluteFilePath();
		const QString fork = resolveThemeFork(file);
		if (fork.isEmpty()) {
			if (!themeCompanionExists(file)) {
				importErrors << file.fileName() + ": full classic appearance skins are not supported; use a preset JSON.";
			}
			/* else: a copy still landing; say nothing yet. */
			importSeen.remove(path);
			return false;
		}
		if (themeSourceImported(fork)) {
			importSeen.remove(path);
			return false;
		}
		const QString fp = fileFingerprint(file);
		if (importFailed.value(path) == fp) {
			importErrors << file.fileName() + ": ZacOS could not build a theme from this file.";
			return false;
		}
		if (importSeen.value(path) != fp) {
			importSeen.insert(path, fp);
			return false; /* wait for the copy to settle */
		}
		if (!importQueue.contains(path) && path != importCurrent) {
			importQueue.append(path);
			return true;
		}
		return false;
	}

	/* Forget imports for files that are gone (a replaced file retries
	 * under its new fingerprint). */
	void pruneImportState() {
		for (auto it = importSeen.begin(); it != importSeen.end();) {
			if (!QFileInfo::exists(it.key())) {
				it = importSeen.erase(it);
			} else {
				++it;
			}
		}
		for (auto it = importFailed.begin(); it != importFailed.end();) {
			if (!QFileInfo::exists(it.key())) {
				it = importFailed.erase(it);
			} else {
				++it;
			}
		}
		for (int i = importQueue.size() - 1; i >= 0; --i) {
			const QFileInfo file(importQueue[i]);
			if (!file.exists() || themeSourceImported(resolveThemeFork(file))) {
				importQueue.removeAt(i);
			}
		}
	}

	/* Scan the Theme Files folder for dropped Mac OS 9 theme files. A
	 * loose file first settles, then moves into its own folder (Theme
	 * Files/<name>/ holds the data fork, its AppleDouble companion and
	 * the generated preset) and imports from there. Runs every
	 * refresh, independently of the change fingerprint (settling a copy
	 * takes several quiet polls). Returns true when it queued anything. */
	bool scanDroppedThemes() {
		bool fresh = false;
		importErrors.clear();
		const QString root = appearanceThemesFolder();
		for (const QFileInfo &file : QDir(root).entryInfoList(
				QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
			const QString name = file.fileName();
			if (name == ".DS_Store" || file.suffix().toLower() == "json") {
				continue;
			}
			if (name.startsWith("._") && QFileInfo::exists(root + "/" + name.mid(2))) {
				continue; /* the data fork's turn handles the pair */
			}
			if (resolveThemeFork(file).isEmpty()) {
				/* No fork to install: a plain document keeps its
				 * error, unwrapped. */
				if (handleDroppedTheme(file)) {
					fresh = true;
				}
				continue;
			}
			const QString path = file.absoluteFilePath();
			const QString fp = fileFingerprint(file);
			if (importSeen.value(path) != fp) {
				importSeen.insert(path, fp);
				continue; /* wait for the copy to settle */
			}
			const QString wrapped = wrapThemeFile(file, root);
			if (wrapped.isEmpty()) {
				if (handleDroppedTheme(file)) {
					fresh = true;
				}
				continue;
			}
			importSeen.remove(path);
			importSeen.insert(wrapped, fileFingerprint(QFileInfo(wrapped)));
			if (handleDroppedTheme(QFileInfo(wrapped))) {
				fresh = true;
			}
		}
		for (const QFileInfo &dir : QDir(root).entryInfoList(
				QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
			for (const QFileInfo &file : QDir(dir.filePath()).entryInfoList(
					QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
				const QString cname = file.fileName();
				if (cname == ".DS_Store" || file.suffix().toLower() == "json") {
					continue;
				}
				if (cname.startsWith("._") && QFileInfo::exists(dir.filePath() + "/" + cname.mid(2))) {
					continue;
				}
				if (handleDroppedTheme(file)) {
					fresh = true;
				}
			}
		}
		return fresh;
	}

	/* Run the next queued import through the Theme Installer, in whatever
	 * process gets the cross-process lock first. */
	void pumpImporter() {
		if (importer.state() != QProcess::NotRunning || importQueue.isEmpty()) {
			return;
		}
		const QString path = importQueue.takeFirst();
		importCurrent = path;
		if (!importLock || !importLock->tryLock()) {
			/* Another process is importing; retry when it finishes. */
			importQueue.prepend(path);
			importCurrent.clear();
			return;
		}
		importer.start(siblingProgram("zacos9-themeinstaller"), { "--file", path });
		importTimeout.start();
	}

	void finishImport(int code, QProcess::ExitStatus status) {
		if (importCurrent.isEmpty()) {
			return; /* already handled (timeout after finished, or vice versa) */
		}
		importTimeout.stop();
		if (importLock) {
			importLock->unlock();
		}
		const QString path = importCurrent;
		importCurrent.clear();
		if (code == 0 && status == QProcess::NormalExit) {
			importSeen.remove(path);
			qWarning().noquote() << "Installed Mac OS 9 theme from" << path;
			/* Rebuild presets now so the new theme is in the Appearance
			 * list immediately, not on the next poll. */
			refresh();
		} else {
			const QString detail = QString::fromUtf8(importer.readAllStandardError()).trimmed();
			importFailed.insert(path, fileFingerprint(QFileInfo(path)));
			qWarning().noquote() << "Theme import failed for" << path << detail;
		}
		pumpImporter();
		notify();
	}

	void notify() {
		for (const QString &error : errors + soundErrors + importErrors) {
			qWarning().noquote() << error;
		}
		const auto copy = watches;
		for (const Watch &watch : copy) {
			if (watch.owner) {
				watch.changed();
			}
		}
	}

	/* One catalog at a time files themes: the import lock serialises
	 * with imports and with other processes' migrations, so a shared
	 * stash name can never eat another process's in-flight move. */
	void maybeMigrate() {
		if (!importCurrent.isEmpty() || !importQueue.isEmpty() ||
				importer.state() != QProcess::NotRunning) {
			return; /* an import is running or queued: leave files alone */
		}
		if (importLock) {
			if (!importLock->tryLock()) {
				return; /* another process is migrating or importing */
			}
			migrateLooseThemes();
			importLock->unlock();
			return;
		}
		migrateLooseThemes();
	}

	void refresh() {
		if (process.state() != QProcess::NotRunning) {
			return;
		}
		QByteArray mark;
		pruneImportState();
		/* Older ZacOS 9 left loose drops behind: file them on sight, so a
		 * running desktop heals without waiting for a restart. */
		maybeMigrate();
		sweepCompanionBundles();
		const bool fresh = scanDroppedThemes();
		/* The alert-sound folders too: a recording dropped into the System
		 * Folder's Sounds is a change the panels showing it must see. */
		for (const QString &folder : { soundThemesFolder(), appearanceThemesFolder(),
				userAlertSoundsFolder() }) {
			const auto entries = QDir(folder).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden,
				QDir::Name);
			for (const QFileInfo &entry : entries) {
				mark += entry.fileName().toUtf8() + QByteArray::number(entry.size()) +
					QByteArray::number(entry.lastModified().toMSecsSinceEpoch());
				if (entry.isDir() && !entry.isSymLink()) {
					for (const QFileInfo &child : QDir(entry.filePath()).entryInfoList(
							QDir::Files | QDir::Hidden, QDir::Name)) {
						mark += child.fileName().toUtf8() + QByteArray::number(child.size()) +
							QByteArray::number(child.lastModified().toMSecsSinceEpoch());
					}
				}
			}
		}
		mark = QCryptographicHash::hash(mark, QCryptographicHash::Sha256);
		if (mark == fingerprint && !fresh) {
			pumpImporter();
			return;
		}
		fingerprint = mark;
		errors.clear();
		presets = { presets.first() };
		/* Presets live beside their theme (one preset per theme folder);
		 * older ones sit in the root itself. */
		const QDir themeRoot(appearanceThemesFolder());
		QFileInfoList presetFiles;
		for (const QFileInfo &file : themeRoot.entryInfoList(
				QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
			if (file.fileName().startsWith("._") || file.fileName() == ".DS_Store") {
				continue;
			}
			if (file.isDir() && !file.isSymLink()) {
				for (const QFileInfo &child : QDir(file.filePath()).entryInfoList(
						QDir::Files | QDir::Hidden, QDir::Name)) {
					if (!child.fileName().startsWith("._") &&
							child.suffix().toLower() == "json") {
						presetFiles << child;
					}
				}
				continue;
			}
			if (file.suffix().toLower() != "json") {
				continue; /* dropped Mac themes: see scanDroppedThemes above */
			}
			presetFiles << file;
		}
		for (const QFileInfo &file : presetFiles) {
			QFile input(file.filePath());
			if (file.isSymLink() || file.size() > 65536 || !input.open(QIODevice::ReadOnly)) {
				errors << file.fileName() + ": preset cannot be read.";
				continue;
			}
			const QJsonObject obj = QJsonDocument::fromJson(input.readAll()).object();
			const QStringList keys{ "accent", "highlight", "pattern", "background", "wallpaper",
				"wallpaper-mode", "alert-sound", "sound-theme", "theme-bundle", "font-family" };
			Theme preset{ "theme:" + QString::fromLatin1(QCryptographicHash::hash(
				themeRoot.relativeFilePath(file.filePath()).toUtf8(), QCryptographicHash::Sha256).toHex().left(24)),
				obj.value("name").toString(), {}, file.absoluteFilePath() };
			bool valid = obj.value("version").toInt() == 1 && !preset.name.isEmpty();
			const QJsonObject values = obj.value("settings").toObject();
			for (auto it = values.begin(); it != values.end(); ++it) {
				if (!keys.contains(it.key()) || !it.value().isString() || it.value().toString().size() > 120) {
					valid = false;
				} else {
					preset.values.insert(it.key(), it.value().toString());
				}
			}
			if (valid && !preset.values.isEmpty()) {
				presets << preset;
			} else {
				errors << file.fileName() + ": invalid appearance preset.";
			}
		}
		process.start(siblingProgram("zacos9-soundthemes"), { soundThemesFolder(), cacheFolder() });
		timeout.start();
		pumpImporter();
		notify();
	}
};

Catalog &catalog() {
	static QPointer<Catalog> instance;
	if (!instance) {
		instance = new Catalog;
	}
	return *instance;
}
} // namespace

QString appearanceThemesFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/zacos9/appearance/Theme Files";
}

QStringList alertSoundIds() {
	QStringList ids = { "platinum" };
	for (const QString &folder : { QString::fromUtf8(pl_data_dir()) + "/sounds",
			userAlertSoundsFolder() }) {
		const QDir dir(folder);
		for (const QString &file : dir.entryList({ "*.wav" }, QDir::Files, QDir::Name)) {
			const QFileInfo info(dir.filePath(file));
			const QString id = info.completeBaseName();
			if (!info.isSymLink() && info.isFile() && !ids.contains(id) &&
					QRegularExpression("\\A[A-Za-z0-9_-]{1,48}\\z").match(id).hasMatch()) {
				ids << id;
			}
		}
	}
	ids << "none";
	return ids;
}

QString alertSoundDisplayName(const QString &id) {
	if (id == "platinum") {
		return "Chime";
	}
	QString name = id;
	if (!name.isEmpty()) {
		name[0] = name[0].toUpper();
	}
	return name;
}

QString soundThemesFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/zacos9/appearance/Sound Themes";
}
QString userAlertSoundsFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/zacos9/sounds";
}

bool importAlertSound(const QString &source, QString *id, QString *error) {
	const QFileInfo info(source);
	if (info.isSymLink() || !info.isFile() || info.size() < 12 || info.size() > 8 * 1024 * 1024) {
		*error = "Choose a regular WAVE file no larger than 8 MiB.";
		return false;
	}
	QFile input(source);
	if (!input.open(QIODevice::ReadOnly)) {
		*error = input.errorString();
		return false;
	}
	const QByteArray header = input.read(12);
	if (header.size() != 12 || header.left(4) != "RIFF" || header.mid(8, 4) != "WAVE") {
		*error = "The selected file is not a RIFF WAVE recording.";
		return false;
	}

	QString name = info.completeBaseName();
	name.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
	name = name.left(48);
	if (name.isEmpty() || name == "none" || name == "platinum") {
		name = "custom-alert";
	}
	const QString folder = userAlertSoundsFolder();
	if (!QDir().mkpath(folder)) {
		*error = "Could not create the personal alert-sounds folder.";
		return false;
	}
	const QString target = folder + "/" + name + ".wav";
	const QString systemTarget = QString::fromUtf8(pl_data_dir()) + "/sounds/" + name + ".wav";
	if (QFileInfo::exists(target) || QFileInfo(target).isSymLink() ||
			QFileInfo::exists(systemTarget)) {
		*error = "A sound with that name already exists. Rename the recording and try again.";
		return false;
	}
	QSaveFile output(target);
	if (!output.open(QIODevice::WriteOnly)) {
		*error = output.errorString();
		return false;
	}
	if (output.write(header) != header.size()) {
		output.cancelWriting();
		*error = output.errorString();
		return false;
	}
	qint64 copied = header.size();
	QByteArray buffer;
	while (!(buffer = input.read(64 * 1024)).isEmpty()) {
		copied += buffer.size();
		if (copied > 8 * 1024 * 1024) {
			output.cancelWriting();
			*error = "The WAVE recording grew beyond the 8 MiB limit.";
			return false;
		}
		if (output.write(buffer) != buffer.size()) {
			output.cancelWriting();
			*error = output.errorString();
			return false;
		}
	}
	if (input.error() != QFileDevice::NoError || copied != info.size()) {
		output.cancelWriting();
		*error = input.errorString().isEmpty() ? "The WAVE recording changed while it was copied."
			: input.errorString();
		return false;
	}
	if (!output.commit()) {
		*error = output.errorString().isEmpty() ? input.errorString() : output.errorString();
		return false;
	}
	*id = name;
	return true;
}

/* Every program font a theme install decoded, from each bundle's
 * theme.conf font-families line. */
QStringList themeFontFamilies() {
	QSet<QString> seen;
	const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9/themes";
	for (const QFileInfo &bundle : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		QFile conf(bundle.filePath() + "/theme.conf");
		if (!conf.open(QIODevice::ReadOnly | QIODevice::Text)) {
			continue;
		}
		QTextStream in(&conf);
		QString line;
		while (in.readLineInto(&line)) {
			if (!line.startsWith("font-families=")) {
				continue;
			}
			for (const QString &family : line.mid(14).split(',', Qt::SkipEmptyParts)) {
				seen.insert(family.trimmed());
			}
		}
	}
	QStringList names = seen.values();
	names.removeAll(QString());
	names.sort();
	return names;
}
void watchCustomThemes(QObject *owner, std::function<void()> changed) {
	catalog().watches.append({ owner, std::move(changed) });
}
int soundThemeCount() { return catalog().sounds.size(); }
QString soundThemeId(int index) { return catalog().sounds.value(index).id; }
QString soundThemeName(int index) { return catalog().sounds.value(index).name; }
QStringList customThemeErrors() { return catalog().errors + catalog().soundErrors + catalog().importErrors; }
int appearanceThemeCount() { return catalog().presets.size(); }
QString appearanceThemeId(int index) { return catalog().presets.value(index).id; }
QString appearanceThemeName(int index) { return catalog().presets.value(index).name; }
QMap<QString, QString> appearanceThemeValues(int index) {
	return catalog().presets.value(index).values;
}

bool applySoundTheme(int index, QString *error) {
	if (index < 0 || index >= catalog().sounds.size()) {
		*error = "The sound theme is no longer available.";
		return false;
	}
	QMap<QString, QString> settings;
	for (const QString &event : events) {
		settings.insert("sound." + event, "");
	}
	const Theme &theme = catalog().sounds[index];
	for (auto it = theme.values.cbegin(); it != theme.values.cend(); ++it) {
		settings.insert(it.key(), it.value());
	}
	settings.insert("sound-theme", theme.id);
	return writeSettings(settings, error);
}

void previewSoundTheme(int index) {
	const Theme theme = catalog().sounds.value(index);
	const int level = setting("interface-volume").isEmpty() ? 5 : setting("interface-volume").toInt();
	if (theme.id == "original") {
		const QString path = QString::fromUtf8(pl_data_dir()) + "/sounds/woodblock.wav";
		pl_sound_preview(path.toUtf8().constData(), level);
	} else if (!theme.values.isEmpty()) {
		/* The first key alphabetically is button-click, so previewing a
		 * set played a click and sounded broken. Use an event that says
		 * what the set is like. */
		QString file = theme.values.value("sound.window-open");
		if (file.isEmpty()) {
			file = theme.values.value("sound.menu-command");
		}
		if (file.isEmpty()) {
			file = theme.values.value("sound.startup");
		}
		if (file.isEmpty()) {
			file = theme.values.first();
		}
		const QString path = cacheFolder() + "/" + file;
		pl_sound_preview(path.toUtf8().constData(), level);
	}
}

/* The catalog's id for an installed picture: the prefix, the SHA-256 of
 * the file's name, then "image" (see zacos9-patterns). */
static QString catalogImageId(const char *prefix, const QString &fileName) {
	return QString::fromLatin1(prefix) + ":" + QString::fromLatin1(
		QCryptographicHash::hash(fileName.toUtf8(), QCryptographicHash::Sha256).toHex()) + ":image";
}

/* A preset may name a pattern or picture by its installed file stem
 * ("Paper-pattern-1"), as presets made before the catalog did. The
 * compositor resolves only catalog ids, so choosing such a theme changed
 * nothing on the desktop and it looked unselectable. */
static QString resolvedPatternId(const QString &id) {
	for (int i = 0; i < desktopPatternCount(); ++i) {
		if (desktopPatternId(i) == id) {
			return id;
		}
	}
	for (int i = 0; i < desktopPatternCount(); ++i) {
		if (desktopPatternName(i).startsWith(id + ".")) {
			return desktopPatternId(i);
		}
	}
	for (const QString &file : QDir(desktopPatternsFolder()).entryList({ id + ".*" }, QDir::Files)) {
		return catalogImageId("user", file);
	}
	return id;
}

static QString resolvedWallpaperId(const QString &id) {
	for (int i = 0; i < desktopWallpaperCount(); ++i) {
		if (desktopWallpaperId(i) == id) {
			return id;
		}
	}
	for (int i = 0; i < desktopWallpaperCount(); ++i) {
		if (desktopWallpaperName(i).startsWith(id + ".")) {
			return desktopWallpaperId(i);
		}
	}
	for (const QString &file : QDir(desktopWallpaperFolder()).entryList({ id + ".*" }, QDir::Files)) {
		return catalogImageId("wallpaper", file);
	}
	return id;
}

bool applyAppearanceTheme(int index, QString *error) {
	if (index < 0 || index >= catalog().presets.size()) {
		*error = "The appearance theme is no longer available.";
		return false;
	}
	const Theme preset = catalog().presets[index];
	QMap<QString, QString> values = preset.values;
	/* A preset without a theme bundle (Silver Coat, a saved preset) must
	 * drop the previous bundle: otherwise its menu icon and boot art stay
	 * overridden after switching back to it. */
	if (!values.contains("theme-bundle")) {
		values.insert("theme-bundle", "");
	}
	if (!values.contains("font-family")) {
		values.insert("font-family", "");
	}
	if (values.contains("accent") && !values["accent"].isEmpty() &&
			QString::fromUtf8(pl_accent_id(pl_accent_find(values["accent"].toUtf8().constData()))) != values["accent"]) {
		*error = "The theme specifies an unknown accent color.";
		return false;
	}
	if (values.contains("highlight") && !values["highlight"].isEmpty() &&
			!QRegularExpression("\\A[0-9A-Fa-f]{6}\\z").match(values["highlight"]).hasMatch()) {
		*error = "The theme specifies an invalid highlight color.";
		return false;
	}
	if (values.contains("background") && values["background"] != "pattern" && values["background"] != "wallpaper") {
		*error = "The theme specifies an unknown background mode.";
		return false;
	}
	if (values.contains("wallpaper-mode") &&
			!QStringList{ "fit", "fill", "stretch", "center", "tile" }.contains(values["wallpaper-mode"])) {
		*error = "The theme specifies an unknown wallpaper placement.";
		return false;
	}
	/* A theme is more than any one optional piece of art. When a sound
	 * set, font, alert or desktop picture cannot be found, leave that
	 * choice out and say so, rather than refusing the whole theme: an
	 * undecodable sound set used to make the theme unselectable. */
	QStringList missing;
	if (!values.value("pattern").isEmpty()) {
		if (desktopPatternAvailable(values["pattern"])) {
			values["pattern"] = resolvedPatternId(values["pattern"]);
		} else {
			missing << "desktop pattern";
			values["pattern"] = "";
		}
	}
	if (!values.value("wallpaper").isEmpty()) {
		if (desktopWallpaperAvailable(values["wallpaper"])) {
			values["wallpaper"] = resolvedWallpaperId(values["wallpaper"]);
		} else {
			missing << "desktop picture";
			values["wallpaper"] = "";
		}
	}
	if (values.contains("alert-sound") && !values["alert-sound"].isEmpty() && values["alert-sound"] != "none" &&
			!alertSoundAvailable(values["alert-sound"])) {
		missing << "alert sound";
		values["alert-sound"] = "";
	}
	if (!values.value("font-family").isEmpty() &&
			!QFontDatabase::families().contains(values["font-family"]) &&
			!themeFontFamilies().contains(values["font-family"])) {
		missing << "program font " + values["font-family"];
		values["font-family"] = "";
	}
	if (values.contains("sound-theme") && !values["sound-theme"].isEmpty()) {
		int sound = -1;
		for (int i = 0; i < soundThemeCount(); ++i) {
			if (soundThemeId(i) == values["sound-theme"]) {
				sound = i;
				break;
			}
		}
		if (sound < 0) {
			missing << "sound set";
			values["sound-theme"] = "";
		} else {
			for (const QString &event : events) {
				values.insert("sound." + event, "");
			}
			for (auto it = catalog().sounds[sound].values.cbegin(); it != catalog().sounds[sound].values.cend(); ++it) {
				values.insert(it.key(), it.value());
			}
		}
	}
	values.insert("appearance-theme", preset.id);
	if (!writeSettings(values, error)) {
		return false;
	}
	if (!missing.isEmpty()) {
		*error = "Applied " + preset.name + " without its " + missing.join(", ") + ".";
	}
	return true;
}

bool saveAppearanceTheme(const QString &name, QString *error) {
	if (name.trimmed().isEmpty()) {
		*error = "Give the appearance theme a name.";
		return false;
	}
	QJsonObject values;
	for (const QString key : { "accent", "highlight", "pattern", "wallpaper", "alert-sound",
			"font-family" }) {
		values.insert(key, setting(key));
	}
	values.insert("background", setting("background").isEmpty() ? "pattern" : setting("background"));
	values.insert("wallpaper-mode", setting("wallpaper-mode").isEmpty() ? "fit" : setting("wallpaper-mode"));
	values.insert("sound-theme", setting("sound-theme").isEmpty() ? "none" : setting("sound-theme"));
	const QString filename = QString::fromLatin1(QCryptographicHash::hash(
		name.toUtf8(), QCryptographicHash::Sha256).toHex().left(24)) + ".json";
	const QString path = appearanceThemesFolder() + "/" + filename;
	if (QFileInfo::exists(path)) {
		*error = "A theme with that name already exists. Choose a new name.";
		return false;
	}
	QSaveFile output(path);
	const QByteArray bytes = QJsonDocument(QJsonObject{
		{ "version", 1 }, { "name", name.trimmed() }, { "settings", values } }).toJson();
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
		*error = output.errorString();
		return false;
	}
	catalog().fingerprint.clear();
	catalog().refresh();
	return true;
}

/* ---- user themes (the Theme Editor) ---------------------------------- */

namespace {

QString themeConfigRoot() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes";
}

bool validThemeId(const QString &id) {
	if (id.isEmpty() || id.size() > 120 || id.startsWith('.') ||
			id.contains('/') || id.contains('\\')) {
		return false;
	}
	for (const QChar c : id) {
		if (c.unicode() < 0x20) {
			return false;
		}
	}
	return id != "." && id != "..";
}

/* A bundle installed in the user's own folder, then the system themes. */
QString themeBundleFolder(const QString &id) {
	if (!validThemeId(id)) {
		return QString();
	}
	const QString user = themeConfigRoot() + "/" + id;
	if (QFileInfo(user).isDir()) {
		return user;
	}
	const QString system = QString::fromUtf8(pl_data_dir()) + "/themes/" + id;
	if (QFileInfo(system).isDir()) {
		return system;
	}
	return QString();
}

QMap<QString, QString> readThemeConf(const QString &dir) {
	QMap<QString, QString> conf;
	QFile f(dir + "/theme.conf");
	if (dir.isEmpty() || !f.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return conf;
	}
	QTextStream in(&f);
	in.setEncoding(QStringConverter::Utf8);
	QString line;
	while (in.readLineInto(&line)) {
		const int eq = line.indexOf('=');
		if (eq <= 0) {
			continue;
		}
		const QString key = line.left(eq).trimmed();
		if (key.isEmpty() || line.size() > 4096) {
			continue;
		}
		conf.insert(key, line.mid(eq + 1));
	}
	return conf;
}

/* Older bundles sometimes used the menu colour keys for art paths.
 * Move those to the explicit image keys. */
static void migrateMenuArtKeys(QMap<QString, QString> &conf) {
	const QPair<QString, QString> moves[] = {
		{ "menu-background", "menu-background-image" },
		{ "menu-highlight", "menu-highlight-image" },
	};
	for (const auto &move : moves) {
		const QString art = conf.value(move.first);
		if (art.startsWith("chrome/") && conf.value(move.second).isEmpty()) {
			conf[move.second] = art;
			conf.remove(move.first);
		}
	}
}

const QStringList &paletteKeys() {
	static const QStringList keys{
		"title-base", "title-stripe-light", "title-stripe-dark", "title-text",
		"title-text-inactive", "frame-face", "frame-face-inactive", "frame-light",
		"frame-dark", "frame-border", "frame-border-inactive", "menu-bar",
		"menu-bar-text", "menu-bar-text-dimmed", "menu-bar-text-selected",
		"menu-bar-light", "menu-bar-shadow", "menu-bar-border", "menu-background",
		"menu-text", "menu-text-dimmed", "menu-text-selected", "menu-border",
		"menu-light", "menu-shadow", "menu-separator", "menu-separator-light",
	};
	return keys;
}

const QMap<QString, QString> &lightPalette() {
	static const QMap<QString, QString> colours{
		{ "title-base", "CCCCCC" }, { "title-stripe-light", "FFFFFF" },
		{ "title-stripe-dark", "777777" }, { "title-text", "000000" },
		{ "title-text-inactive", "666666" }, { "frame-face", "CCCCCC" },
		{ "frame-face-inactive", "DDDDDD" }, { "frame-light", "FFFFFF" },
		{ "frame-dark", "999999" }, { "frame-border", "000000" },
		{ "frame-border-inactive", "555555" }, { "menu-bar", "DDDDDD" },
		{ "menu-bar-text", "000000" }, { "menu-bar-text-dimmed", "888888" },
		{ "menu-bar-text-selected", "FFFFFF" }, { "menu-bar-light", "FFFFFF" },
		{ "menu-bar-shadow", "999999" }, { "menu-bar-border", "000000" },
		{ "menu-background", "DDDDDD" }, { "menu-text", "000000" },
		{ "menu-text-dimmed", "888888" }, { "menu-text-selected", "FFFFFF" },
		{ "menu-border", "000000" }, { "menu-light", "FFFFFF" },
		{ "menu-shadow", "999999" }, { "menu-separator", "888888" },
		{ "menu-separator-light", "FFFFFF" },
	};
	return colours;
}

QString normalisedColour(const QString &value) {
	QString v = value.trimmed();
	if (v.startsWith('#')) {
		v = v.mid(1);
	}
	if (v.size() != 6) {
		return QString();
	}
	for (const QChar c : v) {
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
			return QString();
		}
	}
	return v.toUpper();
}

bool safeRelativePath(const QString &path) {
	if (path.isEmpty() || path.startsWith('/') || path.contains('\\')) {
		return false;
	}
	for (const QString &part : path.split('/')) {
		if (part.isEmpty() || part == "." || part == "..") {
			return false;
		}
	}
	return true;
}

QString presetIdForPath(const QString &path) {
	QDir root(appearanceThemesFolder());
	const QString rel = root.relativeFilePath(path);
	return "theme:" + QString::fromLatin1(
		QCryptographicHash::hash(rel.toUtf8(), QCryptographicHash::Sha256).toHex().left(24));
}

QString sanitisedBundleName(const QString &name) {
	QString id = name.trimmed();
	for (int i = 0; i < id.size(); ++i) {
		const QChar c = id[i];
		if (c == '/' || c == '\\' || c == ':' || c.unicode() < 0x20) {
			id[i] = '-';
		}
	}
	id = id.left(60).trimmed();
	while (id.startsWith('.')) {
		id.remove(0, 1);
	}
	while (id.endsWith('.') || id.endsWith(' ')) {
		id.chop(1);
	}
	return id.isEmpty() ? QString("Theme") : id;
}

QString uniqueBundleId(const QString &name) {
	const QString base = sanitisedBundleName(name);
	QString id = base;
	for (int n = 2; QFileInfo::exists(themeConfigRoot() + "/" + id) ||
			QFileInfo::exists(QString::fromUtf8(pl_data_dir()) + "/themes/" + id); ++n) {
		id = base + " Custom";
		if (n > 2) {
			id += " " + QString::number(n - 1);
		}
	}
	return id;
}

bool validateDraftSettings(QMap<QString, QString> &settings, QString *error) {
	const QString accent = settings.value("accent");
	if (!accent.isEmpty() &&
			QString::fromUtf8(pl_accent_id(pl_accent_find(accent.toUtf8().constData()))) != accent) {
		*error = "Choose one of the accent colours.";
		return false;
	}
	const QString highlight = settings.value("highlight");
	if (!highlight.isEmpty() && normalisedColour(highlight).isEmpty()) {
		*error = "The highlight colour must be six hexadecimal digits.";
		return false;
	}
	if (!highlight.isEmpty()) {
		settings["highlight"] = normalisedColour(highlight);
	}
	const QString background = settings.value("background");
	if (!background.isEmpty() && background != "pattern" && background != "wallpaper") {
		*error = "The desktop background must be a pattern or a picture.";
		return false;
	}
	const QString mode = settings.value("wallpaper-mode");
	if (!mode.isEmpty() && !QStringList{ "fit", "fill", "stretch", "center", "tile" }.contains(mode)) {
		*error = "Choose how the desktop picture is placed.";
		return false;
	}
	const QString sound = settings.value("sound-theme");
	if (!sound.isEmpty() && sound != "none" && sound != "original") {
		bool found = false;
		for (int i = 0; i < soundThemeCount(); ++i) {
			if (soundThemeId(i) == sound) {
				found = true;
				break;
			}
		}
		if (!found) {
			*error = "The chosen sound set is no longer installed.";
			return false;
		}
	}
	const QString alert = settings.value("alert-sound");
	if (!alert.isEmpty() && alert != "platinum" && alert != "none" &&
			!alertSoundIds().contains(alert)) {
		*error = "The chosen alert sound is no longer installed.";
		return false;
	}
	const QString font = settings.value("font-family");
	if (!font.isEmpty() && !QFontDatabase::families().contains(font) &&
			!themeFontFamilies().contains(font)) {
		*error = "The chosen program font is no longer installed.";
		return false;
	}
	for (const QString key : { "pattern", "wallpaper" }) {
		if (settings.value(key).size() > 120 || settings.value(key).contains('\n') ||
				settings.value(key).contains('\r')) {
			*error = "The desktop art name is too long.";
			return false;
		}
	}
	return true;
}

/* Every key the editor writes must be safe to put on a theme.conf line. */
bool validateChrome(QMap<QString, QString> &chrome, QString *error) {
	for (const char *meta : { "name", "user-theme", "inherits", "source",
			"extractor-sha256", "editor-version" }) {
		chrome.remove(meta);
	}
	for (const char *key : { "accent", "highlight", "pattern" }) {
		chrome.remove(key); /* written from the preset's settings */
	}
	for (auto it = chrome.begin(); it != chrome.end(); ++it) {
		if (it.key().isEmpty() || it.key().size() > 64 ||
				!QRegularExpression("\\A[a-z0-9-]+\\z").match(it.key()).hasMatch()) {
			*error = "The theme has an invalid setting name: " + it.key();
			return false;
		}
		if (it.value().size() > 400 || it.value().contains('\n') || it.value().contains('\r')) {
			*error = "The theme has a setting that is too long: " + it.key();
			return false;
		}
		if (paletteKeys().contains(it.key())) {
			const QString colour = normalisedColour(it.value());
			if (colour.isEmpty()) {
				*error = "The theme's " + it.key() + " colour is not six hexadecimal digits.";
				return false;
			}
			it.value() = colour;
		} else if (it.value().contains('/') && !safeRelativePath(it.value())) {
			*error = "The theme names a file outside its own folder: " + it.value();
			return false;
		}
	}
	return true;
}

bool writeUserBundle(const QString &dir, const ThemeDraft &draft, QString *error) {
	if (!QDir().mkpath(dir)) {
		*error = "Could not create the theme's folder.";
		return false;
	}
	QMap<QString, QString> chrome = draft.chrome;
	if (!validateChrome(chrome, error)) {
		return false;
	}
	for (const QString &key : paletteKeys()) {
		if (chrome.value(key).isEmpty()) {
			chrome.insert(key, lightPalette().value(key));
		}
	}
	QByteArray text;
	text += "name=" + draft.name.toUtf8() + "\n";
	text += "user-theme=1\n";
	text += "editor-version=1\n";
	if (!draft.inherits.isEmpty()) {
		text += "inherits=" + draft.inherits.toUtf8() + "\n";
	}
	for (auto it = chrome.cbegin(); it != chrome.cend(); ++it) {
		if (!it.value().isEmpty()) {
			text += it.key().toUtf8() + "=" + it.value().toUtf8() + "\n";
		}
	}
	for (const char *key : { "accent", "highlight", "pattern" }) {
		if (!draft.settings.value(key).isEmpty()) {
			text += key + QByteArray("=") + draft.settings.value(key).toUtf8() + "\n";
		}
	}
	QSaveFile output(dir + "/theme.conf");
	if (!output.open(QIODevice::WriteOnly) || output.write(text) != text.size() ||
			!output.commit()) {
		*error = output.errorString().isEmpty() ? "Could not write the theme." : output.errorString();
		return false;
	}
	return true;
}

bool writePreset(const QString &path, const ThemeDraft &draft, const QString &bundleId,
		QString *error) {
	const QString folder = QFileInfo(path).absolutePath();
	if (!QDir().mkpath(folder)) {
		*error = "Could not create the theme's folder.";
		return false;
	}
	QJsonObject settings;
	settings.insert("accent", draft.settings.value("accent"));
	settings.insert("highlight", draft.settings.value("highlight"));
	settings.insert("pattern", draft.settings.value("pattern"));
	settings.insert("wallpaper", draft.settings.value("wallpaper"));
	settings.insert("background", draft.settings.value("background").isEmpty()
		? QString("pattern") : draft.settings.value("background"));
	settings.insert("wallpaper-mode", draft.settings.value("wallpaper-mode").isEmpty()
		? QString("fit") : draft.settings.value("wallpaper-mode"));
	settings.insert("alert-sound", draft.settings.value("alert-sound"));
	settings.insert("sound-theme", draft.settings.value("sound-theme").isEmpty()
		? QString("none") : draft.settings.value("sound-theme"));
	settings.insert("font-family", draft.settings.value("font-family"));
	settings.insert("theme-bundle", bundleId);
	QSaveFile output(path);
	const QByteArray bytes = QJsonDocument(QJsonObject{
		{ "version", 1 }, { "name", draft.name }, { "settings", settings } }).toJson();
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
			!output.commit()) {
		*error = output.errorString().isEmpty() ? "Could not write the theme preset." : output.errorString();
		return false;
	}
	return true;
}

bool withinFolder(const QString &path, const QString &folder) {
	const QString canonicalFolder = QFileInfo(folder).canonicalFilePath();
	const QString canonical = QFileInfo(path).canonicalFilePath();
	if (!canonicalFolder.isEmpty() && !canonical.isEmpty()) {
		return canonical.startsWith(canonicalFolder + "/");
	}
	const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
	const QString root = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
	return clean.startsWith(root + "/");
}

std::unique_ptr<QLockFile> lockThemeWrite(QString *error) {
	QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	if (runtime.isEmpty()) {
		runtime = QDir::tempPath();
	}
	std::unique_ptr<QLockFile> lock(new QLockFile(runtime + "/zacos9-theme-import.lock"));
	if (!lock->tryLock(200)) {
		*error = "Another theme is being installed or saved. Try again in a moment.";
		return nullptr;
	}
	return lock;
}

} // namespace

QString appearanceThemePath(int index) {
	return catalog().presets.value(index).path;
}

QString appearanceThemeBundle(int index) {
	return appearanceThemeValues(index).value("theme-bundle");
}

void themeDraftDropChromeArt(ThemeDraft *draft) {
	if (!draft) {
		return;
	}
	for (const ThemeArtSlot &slot : themeArtSlots()) {
		if (slot.defaultPath.isEmpty()) {
			draft->chrome.remove(slot.key);
		} else {
			draft->chrome[slot.key] = "off";
		}
		draft->artSources.remove(slot.key);
	}
	draft->chrome.remove("title-cluster");
}

const QList<ThemeArtSlot> &themeArtSlots() {
	static const QList<ThemeArtSlot> artSlots{
		{ "bar-left", "Left end", QString(), true, "Title bar" },
		{ "bar-middle", "Middle", QString(), true, "Title bar" },
		{ "bar-right", "Right end", QString(), true, "Title bar" },
		{ "bar-span", "Aqua span", QString(), true, "Title bar" },
		{ "bar-left-inactive", "Inactive left end", QString(), true, "Title bar" },
		{ "bar-middle-inactive", "Inactive middle", QString(), true, "Title bar" },
		{ "bar-right-inactive", "Inactive right end", QString(), true, "Title bar" },
		{ "bar-span-inactive", "Inactive Aqua span", QString(), true, "Title bar" },
		{ "bar-dialog-left", "Dialog left end", QString(), true, "Dialog title bar" },
		{ "bar-dialog-middle", "Dialog middle", QString(), true, "Dialog title bar" },
		{ "bar-dialog-right", "Dialog right end", QString(), true, "Dialog title bar" },
		{ "bar-dialog-left-inactive", "Inactive dialog left end", QString(), true, "Dialog title bar" },
		{ "bar-dialog-middle-inactive", "Inactive dialog middle", QString(), true, "Dialog title bar" },
		{ "bar-dialog-right-inactive", "Inactive dialog right end", QString(), true, "Dialog title bar" },
		{ "button-close", "Close", QString(), true, "Buttons" },
		{ "button-close-pressed", "Close, pressed", QString(), true, "Buttons" },
		{ "button-zoom", "Zoom", QString(), true, "Buttons" },
		{ "button-zoom-pressed", "Zoom, pressed", QString(), true, "Buttons" },
		{ "button-grow", "Collapse", QString(), true, "Buttons" },
		{ "button-grow-pressed", "Collapse, pressed", QString(), true, "Buttons" },
		{ "button-dialog-close", "Dialog close", QString(), true, "Dialog buttons" },
		{ "button-dialog-close-pressed", "Dialog close, pressed", QString(), true, "Dialog buttons" },
		{ "button-dialog-zoom", "Dialog zoom", QString(), true, "Dialog buttons" },
		{ "button-dialog-zoom-pressed", "Dialog zoom, pressed", QString(), true, "Dialog buttons" },
		{ "button-dialog-grow", "Dialog collapse", QString(), true, "Dialog buttons" },
		{ "button-dialog-grow-pressed", "Dialog collapse, pressed", QString(), true, "Dialog buttons" },
		{ "document-top", "Top edge", "chrome/document-top.png", false, "Window frame" },
		{ "document-top-inactive", "Inactive top edge", "chrome/document-top-inactive.png", false, "Window frame" },
		{ "document-bottom", "Bottom edge", "chrome/document-bottom.png", false, "Window frame" },
		{ "document-bottom-inactive", "Inactive bottom edge", "chrome/document-bottom-inactive.png", false, "Window frame" },
		{ "document-left", "Left edge", "chrome/document-left.png", false, "Window frame" },
		{ "document-left-inactive", "Inactive left edge", "chrome/document-left-inactive.png", false, "Window frame" },
		{ "document-right", "Right edge", "chrome/document-right.png", false, "Window frame" },
		{ "document-right-inactive", "Inactive right edge", "chrome/document-right-inactive.png", false, "Window frame" },
		{ "modal-top", "Dialog top edge", "chrome/modal-top.png", false, "Dialog frame" },
		{ "modal-bottom", "Dialog bottom edge", "chrome/modal-bottom.png", false, "Dialog frame" },
		{ "modal-left", "Dialog left edge", "chrome/modal-left.png", false, "Dialog frame" },
		{ "modal-right", "Dialog right edge", "chrome/modal-right.png", false, "Dialog frame" },
		{ "popup-tab", "Pop-up tab", "chrome/popup-tab.png", false, "Pop-up" },
		{ "popup-tab-inactive", "Inactive pop-up tab", "chrome/popup-tab-inactive.png", false, "Pop-up" },
		{ "menu-bar-image", "Menu bar background", "chrome/menu-background.png", false, "Menus" },
		{ "menu-background-image", "Menu background", "chrome/menu-background.png", false, "Menus" },
		{ "menu-highlight-image", "Selected item", "chrome/menu-highlight.png", false, "Menus" },
	};
	return artSlots;
}

bool themeArtWrite(const QString &source, const QString &path, bool argb, QString *error) {
	const QFileInfo info(source);
	if (info.isSymLink() || !info.isFile() || info.size() > 8 * 1024 * 1024) {
		*error = "Choose a regular image no larger than 8 MiB.";
		return false;
	}
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		*error = "Could not create the theme's chrome folder.";
		return false;
	}
	/* A theme's own .argb tile is already in the compositor's format:
	 * validate it and copy it through, so Duplicate carries it. */
	if (argb && source.endsWith(".argb", Qt::CaseInsensitive)) {
		QFile input(source);
		if (!input.open(QIODevice::ReadOnly)) {
			*error = input.errorString();
			return false;
		}
		const QByteArray bytes = input.readAll();
		if (bytes.size() < 8) {
			*error = "That ARGB tile is too short.";
			return false;
		}
		auto u32 = [&bytes](int at) {
			return (quint32)(uchar)bytes[at] | (quint32)(uchar)bytes[at + 1] << 8 |
				(quint32)(uchar)bytes[at + 2] << 16 | (quint32)(uchar)bytes[at + 3] << 24;
		};
		const quint32 w = u32(0), h = u32(4);
		if (w == 0 || h == 0 || w > 4096 || h > 4096 ||
				(qint64)bytes.size() != 8 + (qint64)w * h * 4) {
			*error = "That ARGB tile has an invalid size.";
			return false;
		}
		QSaveFile output(path);
		if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
				!output.commit()) {
			*error = output.errorString().isEmpty() ? "Could not write the theme's artwork."
				: output.errorString();
			return false;
		}
		return true;
	}
	QImage image(source);
	if (image.isNull() || image.width() < 1 || image.height() < 1 ||
			image.width() > 4096 || image.height() > 4096 ||
			(qint64)image.width() * image.height() > 4096LL * 1024) {
		*error = "That image could not be read, or is too large for a window tile.";
		return false;
	}
	QSaveFile output(path);
	if (!output.open(QIODevice::WriteOnly)) {
		*error = output.errorString();
		return false;
	}
	if (argb) {
		const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
		if (rgba.isNull()) {
			*error = "That image could not be converted.";
			return false;
		}
		unsigned char head[8] = {
			(unsigned char)(rgba.width() & 0xFF),
			(unsigned char)((rgba.width() >> 8) & 0xFF),
			(unsigned char)((rgba.width() >> 16) & 0xFF),
			(unsigned char)((rgba.width() >> 24) & 0xFF),
			(unsigned char)(rgba.height() & 0xFF),
			(unsigned char)((rgba.height() >> 8) & 0xFF),
			(unsigned char)((rgba.height() >> 16) & 0xFF),
			(unsigned char)((rgba.height() >> 24) & 0xFF),
		};
		if (output.write((const char *)head, sizeof(head)) != (qint64)sizeof(head)) {
			output.cancelWriting();
			*error = output.errorString();
			return false;
		}
		for (int y = 0; y < rgba.height(); ++y) {
			const uchar *line = rgba.constScanLine(y);
			const qint64 bytes = (qint64)rgba.width() * 4;
			if (output.write((const char *)line, bytes) != bytes) {
				output.cancelWriting();
				*error = output.errorString();
				return false;
			}
		}
	} else if (!image.save(&output, "PNG")) {
		output.cancelWriting();
		*error = "That image could not be written as a PNG.";
		return false;
	}
	if (!output.commit()) {
		*error = output.errorString().isEmpty() ? "Could not write the theme's artwork."
			: output.errorString();
		return false;
	}
	return true;
}

ThemeArtState themeArtState(const ThemeDraft &draft, const ThemeArtSlot &slot) {
	const QString value = draft.chrome.value(slot.key);
	if (!slot.defaultPath.isEmpty()) {
		if (value == "off") {
			return ThemeArtState::Platinum;
		}
		const QString own = themeBundleFolder(draft.bundleId);
		const bool own_has_file = !own.isEmpty() &&
			QFileInfo::exists(own + "/" + slot.defaultPath);
		const bool base_has_file = !draft.baseBundleDir.isEmpty() &&
			QFileInfo::exists(draft.baseBundleDir + "/" + slot.defaultPath);
		if (value == slot.defaultPath) {
			if (own_has_file) {
				return ThemeArtState::Custom;
			}
			return base_has_file ? ThemeArtState::Inherit : ThemeArtState::Custom;
		}
		if (!value.isEmpty()) {
			return ThemeArtState::Custom;
		}
		if (own_has_file) {
			return ThemeArtState::Custom;
		}
		return base_has_file ? ThemeArtState::Inherit : ThemeArtState::Platinum;
	}
	const QString inherited = draft.importedChrome.value(slot.key);
	if (!value.isEmpty() && !inherited.isEmpty() && value == inherited) {
		return ThemeArtState::Inherit;
	}
	return value.isEmpty() ? ThemeArtState::Platinum : ThemeArtState::Custom;
}

QMap<QString, QString> themePaletteDefaults(bool dark) {
	if (!dark) {
		return lightPalette();
	}
	static const QMap<QString, QString> colours{
		{ "title-base", "3A3A3A" }, { "title-stripe-light", "555555" },
		{ "title-stripe-dark", "242424" }, { "title-text", "FFFFFF" },
		{ "title-text-inactive", "C8C8C8" }, { "frame-face", "2E2E2E" },
		{ "frame-face-inactive", "3A3A3A" }, { "frame-light", "555555" },
		{ "frame-dark", "141414" }, { "frame-border", "000000" },
		{ "frame-border-inactive", "3A3A3A" }, { "menu-bar", "333333" },
		{ "menu-bar-text", "FFFFFF" }, { "menu-bar-text-dimmed", "A8A8A8" },
		{ "menu-bar-text-selected", "FFFFFF" }, { "menu-bar-light", "4A4A4A" },
		{ "menu-bar-shadow", "1A1A1A" }, { "menu-bar-border", "000000" },
		{ "menu-background", "2A2A2A" }, { "menu-text", "FFFFFF" },
		{ "menu-text-dimmed", "999999" }, { "menu-text-selected", "FFFFFF" },
		{ "menu-border", "000000" }, { "menu-light", "444444" },
		{ "menu-shadow", "151515" }, { "menu-separator", "666666" },
		{ "menu-separator-light", "3A3A3A" },
	};
	return colours;
}

bool appearanceThemeIsUser(int index) {
	if (index < 0 || index >= appearanceThemeCount()) {
		return false;
	}
	const QString bundle = appearanceThemeBundle(index);
	if (bundle.isEmpty()) {
		return true; /* a saved settings preset, not an imported bundle */
	}
	const QString dir = themeBundleFolder(bundle);
	return !dir.isEmpty() && readThemeConf(dir).value("user-theme") == "1";
}

bool themeDraftFromPreset(int index, ThemeDraft *draft, QString *error) {
	if (!draft || index < 0 || index >= appearanceThemeCount()) {
		*error = "Choose a theme to edit.";
		return false;
	}
	ThemeDraft d;
	d.name = appearanceThemeName(index);
	d.presetPath = appearanceThemePath(index);
	d.settings = appearanceThemeValues(index);
	const QString bundle = d.settings.take("theme-bundle");
	if (!bundle.isEmpty()) {
		const QString dir = themeBundleFolder(bundle);
		if (!dir.isEmpty()) {
			QMap<QString, QString> conf = readThemeConf(dir);
			migrateMenuArtKeys(conf);
			d.chrome = conf;
			if (conf.value("user-theme") == "1") {
				d.bundleId = bundle;
				d.inherits = conf.value("inherits");
				if (!d.inherits.isEmpty()) {
					d.baseBundleDir = themeBundleFolder(d.inherits);
					if (!d.baseBundleDir.isEmpty()) {
						d.importedChrome = readThemeConf(d.baseBundleDir);
					migrateMenuArtKeys(d.importedChrome);
					}
				}
			} else {
				d.inherits = bundle;
				d.linked = true;
				d.importedChrome = conf;
				d.baseBundleDir = dir;
			}
		}
	}
	*draft = d;
	return true;
}

bool themeDraftSave(ThemeDraft *draft, int sourceIndex, QString *error) {
	if (!draft) {
		*error = "No theme to save.";
		return false;
	}
	draft->name = draft->name.trimmed();
	if (draft->name.isEmpty()) {
		*error = "Give the theme a name.";
		return false;
	}
	if (draft->name.size() > 64) {
		*error = "The theme's name is too long (64 characters at most).";
		return false;
	}
	if (draft->name.startsWith('.') || draft->name.contains('/') || draft->name.contains('\\') ||
			draft->name.contains('\n') || draft->name.contains('\r')) {
		*error = "The theme's name cannot start with a dot or contain a slash.";
		return false;
	}
	for (int i = 0; i < appearanceThemeCount(); ++i) {
		if (i != sourceIndex &&
				appearanceThemeName(i).compare(draft->name, Qt::CaseInsensitive) == 0) {
			*error = "A theme with that name already exists. Choose a new name.";
			return false;
		}
	}
	if (!validateDraftSettings(draft->settings, error)) {
		return false;
	}
	const bool newBundle = draft->bundleId.isEmpty();
	const QString id = newBundle ? uniqueBundleId(draft->name) : draft->bundleId;
	if (!validThemeId(id)) {
		*error = "That theme name cannot be used for its folder. Choose another name.";
		return false;
	}
	QString userDir = themeConfigRoot() + "/" + id;
	if (!newBundle) {
		if (QFileInfo(userDir).isSymLink() || !QFileInfo(userDir).isDir() ||
				readThemeConf(userDir).value("user-theme") != "1") {
			*error = "Only themes made with the Theme Editor can be changed here.";
			return false;
		}
	}
	if (!draft->inherits.isEmpty()) {
		if (draft->inherits == id) {
			*error = "A theme cannot inherit its own artwork.";
			return false;
		}
		if (themeBundleFolder(draft->inherits).isEmpty()) {
			*error = "The imported theme this one copies is no longer installed.";
			return false;
		}
	}
	/* Keep the preset where it is for a user theme or a plain settings
	 * preset; a linked copy gets its own file, and the import's preset is
	 * never touched. */
	const bool updateInPlace = !draft->presetPath.isEmpty() && !draft->linked;
	QString presetPath = updateInPlace ? draft->presetPath
		: appearanceThemesFolder() + "/" + id + "/theme.json";
	if (!withinFolder(presetPath, appearanceThemesFolder()) ||
			QFileInfo(presetPath).isSymLink()) {
		*error = "The theme's preset is outside the Theme Files folder.";
		return false;
	}
	if (!QFileInfo(QFileInfo(presetPath).absolutePath()).exists() &&
			!QDir().mkpath(QFileInfo(presetPath).absolutePath())) {
		*error = "Could not create the theme's folder.";
		return false;
	}
	std::unique_ptr<QLockFile> lock = lockThemeWrite(error);
	if (!lock) {
		return false;
	}
	const bool createdBundle = !QFileInfo(userDir).exists();
	/* Validate every chosen image before anything is written. */
	const QList<ThemeArtSlot> &artSlots = themeArtSlots();
	auto slotFor = [&artSlots](const QString &key) -> const ThemeArtSlot * {
		for (const ThemeArtSlot &slot : artSlots) {
			if (slot.key == key) {
				return &slot;
			}
		}
		return nullptr;
	};
	for (auto it = draft->artSources.cbegin(); it != draft->artSources.cend(); ++it) {
		const ThemeArtSlot *slot = slotFor(it.key());
		const QFileInfo source(it.value());
		if (!slot) {
			*error = "The theme names an unknown piece of artwork: " + it.key();
			return false;
		}
		if (source.isSymLink() || !source.isFile() || source.size() > 8 * 1024 * 1024) {
			*error = "The image chosen for " + slot->label + " is missing or too large.";
			return false;
		}
	}
	QMap<QString, QString> checked = draft->chrome;
	if (!validateChrome(checked, error)) {
		return false;
	}
	if (!QDir().mkpath(userDir)) {
		*error = "Could not create the theme's folder.";
		return false;
	}
	if (QFileInfo(userDir).isSymLink()) {
		*error = "The theme's folder is a symbolic link; it was left alone.";
		return false;
	}
	/* Custom tiles live beside the theme's theme.conf, so Save is
	 * self-contained and a copied theme carries its own artwork. */
	for (auto it = draft->artSources.cbegin(); it != draft->artSources.cend(); ++it) {
		const ThemeArtSlot *slot = slotFor(it.key());
		const QString target = "chrome/custom-" + it.key() + (slot->argb ? ".argb" : ".png");
		if (!themeArtWrite(it.value(), userDir + "/" + target, slot->argb, error)) {
			if (createdBundle) {
				QDir(userDir).removeRecursively();
			}
			return false;
		}
		draft->chrome[it.key()] = target;
	}
	if (!writeUserBundle(userDir, *draft, error)) {
		if (createdBundle) {
			QDir(userDir).removeRecursively();
		}
		return false;
	}
	if (!writePreset(presetPath, *draft, id, error)) {
		if (createdBundle) {
			QDir(userDir).removeRecursively();
		}
		return false;
	}
	draft->bundleId = id;
	draft->presetPath = presetPath;
	draft->linked = false;

	/* Apply exactly what was saved, without waiting for the catalogue's
	 * two-second poll (it may be busy importing sounds). */
	QMap<QString, QString> values;
	/* Empty values remove the previous theme's choice from desktop.conf,
	 * just as applying a preset does. */
	for (const char *key : { "accent", "highlight", "pattern", "wallpaper",
			"alert-sound", "font-family" }) {
		values.insert(key, draft->settings.value(key));
	}
	values.insert("background", draft->settings.value("background").isEmpty()
		? QString("pattern") : draft->settings.value("background"));
	values.insert("wallpaper-mode", draft->settings.value("wallpaper-mode").isEmpty()
		? QString("fit") : draft->settings.value("wallpaper-mode"));
	const QString sound = draft->settings.value("sound-theme").isEmpty()
		? QString("none") : draft->settings.value("sound-theme");
	values.insert("sound-theme", sound);
	for (const QString &event : events) {
		values.insert("sound." + event, "");
	}
	for (int i = 0; i < soundThemeCount(); ++i) {
		if (soundThemeId(i) == sound) {
			for (auto it = catalog().sounds[i].values.cbegin();
					it != catalog().sounds[i].values.cend(); ++it) {
				values.insert(it.key(), it.value());
			}
			break;
		}
	}
	values.insert("theme-bundle", id);
	values.insert("appearance-theme", presetIdForPath(presetPath));
	if (!writeSettings(values, error)) {
		return false;
	}
	catalog().fingerprint.clear();
	catalog().refresh();
	return true;
}

bool themeDraftDelete(int index, QString *error) {
	if (index < 0 || index >= appearanceThemeCount()) {
		*error = "Choose a theme to delete.";
		return false;
	}
	if (!appearanceThemeIsUser(index)) {
		*error = "Imported themes stay in Theme Files. Edit one to make a copy you can change.";
		return false;
	}
	const QString path = appearanceThemePath(index);
	if (path.isEmpty()) {
		*error = "The built-in theme cannot be deleted.";
		return false;
	}
	if (!withinFolder(path, appearanceThemesFolder()) || QFileInfo(path).isSymLink()) {
		*error = "The theme's preset is outside the Theme Files folder.";
		return false;
	}
	std::unique_ptr<QLockFile> lock = lockThemeWrite(error);
	if (!lock) {
		return false;
	}
	const QString bundle = appearanceThemeBundle(index);
	const QString id = appearanceThemeId(index);
	const bool active = setting("appearance-theme") == id;
	const QString userDir = themeConfigRoot() + "/" + bundle;
	if (!bundle.isEmpty() && appearanceThemeIsUser(index) && QFileInfo(userDir).isSymLink()) {
		*error = "The theme's folder is a symbolic link; it was left alone.";
		return false;
	}
	if (!QFile::remove(path)) {
		*error = "Could not remove the theme's preset.";
		return false;
	}
	if (!bundle.isEmpty() && appearanceThemeIsUser(index)) {
		if (QFileInfo(userDir).isDir()) {
			QFile::remove(userDir + "/theme.conf");
			const QString chrome = userDir + "/chrome";
			for (const QFileInfo &file : QDir(chrome).entryInfoList(
					QStringList("custom-*"), QDir::Files | QDir::Hidden)) {
				QFile::remove(file.filePath());
			}
			QDir().rmdir(chrome);
			QDir().rmdir(userDir); /* only when nothing else was put there */
		}
		const QString folder = appearanceThemesFolder() + "/" + bundle;
		if (QFileInfo(folder).isDir()) {
			QDir().rmdir(folder);
		}
	}
	if (active) {
		QString applyError;
		if (!applyAppearanceTheme(0, &applyError)) {
			*error = applyError;
			return false;
		}
	}
	catalog().fingerprint.clear();
	catalog().refresh();
	return true;
}
