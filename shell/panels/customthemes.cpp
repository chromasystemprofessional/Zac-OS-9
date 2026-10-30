#include "customthemes.h"
#include "conf.h"
#include "programs.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
QString setting(const QString &key) {
	char value[128];
	return pl_setting(key.toUtf8().constData(), value, sizeof(value)) ? QString::fromUtf8(value) : QString();
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

class Catalog : public QObject {
public:
	QList<Theme> sounds{{ "none", "None", {} }, { "original", "ZacOS Original", {} }};
	QList<Theme> presets{{ "platinum", "Silver Coat", {
		{ "accent", "" }, { "highlight", "" }, { "pattern", "" },
		{ "background", "pattern" }, { "wallpaper", "" },
		{ "wallpaper-mode", "fit" }, { "alert-sound", "" }, { "sound-theme", "none" }
	}}};
	QStringList errors, soundErrors;
	QList<Watch> watches;
	QProcess process;
	QTimer poll, timeout;
	QByteArray fingerprint;

	Catalog() : QObject(QCoreApplication::instance()) {
		for (const QString &folder : { soundThemesFolder(), appearanceThemesFolder(), cacheFolder() }) {
			if (!QDir().mkpath(folder)) {
				qWarning().noquote() << "Could not create theme folder:" << folder;
			}
		}
		poll.setInterval(2000);
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

	void notify() {
		for (const QString &error : errors + soundErrors) {
			qWarning().noquote() << error;
		}
		const auto copy = watches;
		for (const Watch &watch : copy) {
			if (watch.owner) {
				watch.changed();
			}
		}
	}

	void refresh() {
		if (process.state() != QProcess::NotRunning) {
			return;
		}
		QByteArray mark;
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
		if (mark == fingerprint) {
			return;
		}
		fingerprint = mark;
		errors.clear();
		presets = { presets.first() };
		for (const QFileInfo &file : QDir(appearanceThemesFolder()).entryInfoList(
				QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
			if (file.fileName().startsWith("._") || file.fileName() == ".DS_Store") {
				continue;
			}
			if (file.suffix().toLower() != "json") {
				errors << file.fileName() + ": full classic appearance skins are not supported; use a preset JSON.";
				continue;
			}
			QFile input(file.filePath());
			if (file.isSymLink() || file.size() > 65536 || !input.open(QIODevice::ReadOnly)) {
				errors << file.fileName() + ": preset cannot be read.";
				continue;
			}
			const QJsonObject obj = QJsonDocument::fromJson(input.readAll()).object();
			const QStringList keys{ "accent", "highlight", "pattern", "background", "wallpaper",
				"wallpaper-mode", "alert-sound", "sound-theme", "theme-bundle" };
			Theme preset{ "theme:" + QString::fromLatin1(QCryptographicHash::hash(
				file.fileName().toUtf8(), QCryptographicHash::Sha256).toHex().left(24)),
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

QString appearanceThemesFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/zacos9/appearance/Themes";
}
void watchCustomThemes(QObject *owner, std::function<void()> changed) {
	catalog().watches.append({ owner, std::move(changed) });
}
int soundThemeCount() { return catalog().sounds.size(); }
QString soundThemeId(int index) { return catalog().sounds.value(index).id; }
QString soundThemeName(int index) { return catalog().sounds.value(index).name; }
QStringList customThemeErrors() { return catalog().errors + catalog().soundErrors; }
int appearanceThemeCount() { return catalog().presets.size(); }
QString appearanceThemeId(int index) { return catalog().presets.value(index).id; }
QString appearanceThemeName(int index) { return catalog().presets.value(index).name; }

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
	if (!values.value("pattern").isEmpty() &&
			desktopPatternId(desktopPatternFind(values["pattern"])) != values["pattern"]) {
		*error = "The preset's desktop pattern is missing.";
		return false;
	}
	if (!values.value("wallpaper").isEmpty() && desktopWallpaperFind(values["wallpaper"]) < 0) {
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
	for (const QString key : { "accent", "highlight", "pattern", "wallpaper", "alert-sound" }) {
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
