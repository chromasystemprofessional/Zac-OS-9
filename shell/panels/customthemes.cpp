#include "customthemes.h"
#include "conf.h"
#include "programs.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
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
static bool themeSourceImported(const QString &fork) {
	if (fork.isEmpty()) {
		return false;
	}
	const QDir forkDir = QFileInfo(fork).dir();
	if (QDir(forkDir).entryInfoList(QStringList("*.json"), QDir::Files).isEmpty()) {
		return false;
	}
	const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes";
	for (const QFileInfo &bundle : QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
		QFile conf(bundle.filePath() + "/theme.conf");
		if (!conf.open(QIODevice::ReadOnly | QIODevice::Text)) {
			continue;
		}
		QTextStream in(&conf);
		QString line;
		while (in.readLineInto(&line)) {
			if (line == "source=" + fork) {
				return true;
			}
		}
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
	QList<Theme> sounds{{ "none", "None", {} }, { "original", "ZacOS Original", {} }};
	QList<Theme> presets{{ "platinum", "Silver Coat", {
		{ "accent", "" }, { "highlight", "" }, { "pattern", "" },
		{ "background", "pattern" }, { "wallpaper", "" },
		{ "wallpaper-mode", "fit" }, { "alert-sound", "" }, { "sound-theme", "none" }
	}}};
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
				Theme theme{ o.value("id").toString(), o.value("name").toString(), {} };
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
				obj.value("name").toString(), {} };
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
		const QString path = cacheFolder() + "/" + theme.values.first();
		pl_sound_preview(path.toUtf8().constData(), level);
	}
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
	if (!values.value("pattern").isEmpty() && !desktopPatternAvailable(values["pattern"])) {
		*error = "The preset's desktop pattern is missing.";
		return false;
	}
	if (!values.value("wallpaper").isEmpty() && !desktopWallpaperAvailable(values["wallpaper"])) {
		*error = "The preset's wallpaper is missing.";
		return false;
	}
	if (values.contains("alert-sound") && !values["alert-sound"].isEmpty() && values["alert-sound"] != "none") {
		const QString id = values["alert-sound"];
		if (!alertSoundAvailable(id)) {
			*error = "The preset's alert sound is missing.";
			return false;
		}
	}
	if (!values.value("font-family").isEmpty() &&
			!QFontDatabase::families().contains(values["font-family"]) &&
			!themeFontFamilies().contains(values["font-family"])) {
		*error = "The preset's program font is missing.";
		return false;
	}
	if (values.contains("sound-theme")) {
		int sound = -1;
		for (int i = 0; i < soundThemeCount(); ++i) {
			if (soundThemeId(i) == values["sound-theme"]) {
				sound = i;
				break;
			}
		}
		if (sound < 0) {
			*error = "The preset's sound theme is missing.";
			return false;
		}
		for (const QString &event : events) {
			values.insert("sound." + event, "");
		}
		for (auto it = catalog().sounds[sound].values.cbegin(); it != catalog().sounds[sound].values.cend(); ++it) {
			values.insert(it.key(), it.value());
		}
	}
	values.insert("appearance-theme", preset.id);
	return writeSettings(values, error);
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
