#include "customthemes.h"

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
	"window-collapse", "window-expand", "window-drag", "window-drag-end" };
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
	QFile input(path);
	QStringList lines;
	if (input.exists()) {
		if (!input.open(QIODevice::ReadOnly)) {
			*error = input.errorString();
			return false;
		}
		lines = QString::fromUtf8(input.readAll()).split('\n', Qt::SkipEmptyParts);
	} else {
		lines << "[General]";
	}
	for (auto it = values.cbegin(); it != values.cend(); ++it) {
		for (int i = lines.size() - 1; i >= 0; --i) {
			if (lines[i].section('=', 0, 0).trimmed() == it.key()) {
				lines.removeAt(i);
			}
		}
		if (!it.value().isEmpty()) {
			const QString line = it.key() + "=" + it.value();
			if (line.toUtf8().size() >= 255 || line.contains('\n') || line.contains('\r')) {
				*error = "Theme preference is too long or contains a line break.";
				return false;
			}
			lines << line;
		}
	}
	if (lines.size() > 64) {
		*error = "Too many appearance preferences.";
		return false;
	}
	QSaveFile output(path);
	const QByteArray bytes = (lines.join('\n') + '\n').toUtf8();
	if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) {
		*error = output.errorString();
		return false;
	}
	return true;
}

QString cacheFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/zacos9/sounds";
}

class Catalog : public QObject {
public:
	QList<Theme> sounds{{ "none", "None", {} }, { "original", "ZacOS Original", {} }};
	QList<Theme> presets{{ "platinum", "Platinum", {
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
		for (const QString &folder : { soundThemesFolder(), appearanceThemesFolder() }) {
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
				"wallpaper-mode", "alert-sound", "sound-theme" };
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
		const QString local = QCoreApplication::applicationDirPath() + "/zacos9-soundthemes";
		process.start(QFileInfo(local).isExecutable() ? local : "zacos9-soundthemes",
			{ soundThemesFolder(), cacheFolder() });
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

QString soundThemesFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/zacos9/appearance/Sound Themes";
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
			!QStringList{ "fit", "fill", "stretch", "center" }.contains(values["wallpaper-mode"])) {
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
		if (id.contains('/') || id.contains('\\') || id.contains("..") ||
				!QFileInfo::exists(QString::fromUtf8(pl_data_dir()) + "/sounds/" + id + ".wav")) {
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
