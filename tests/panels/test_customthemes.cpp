#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QThread>
#include <cassert>
#include <functional>

#include "customthemes.h"
#include "appearance.h"
#include "settings.h"

static bool waitFor(const std::function<bool()> &ready) {
	QElapsedTimer timer;
	timer.start();
	while (!ready() && timer.elapsed() < 10000) {
		QApplication::processEvents();
		QThread::msleep(10);
	}
	return ready();
}
static void write(const QString &path, const QByteArray &bytes) {
	QFile file(path);
	assert(file.open(QIODevice::WriteOnly));
	assert(file.write(bytes) == bytes.size());
}
static void put16(QByteArray &out, quint16 v) {
	out.append(char(v >> 8));
	out.append(char(v & 0xFF));
}
static void put32(QByteArray &out, quint32 v) {
	put16(out, quint16(v >> 16));
	put16(out, quint16(v & 0xFFFF));
}
/* A one-icon Mac OS 9 theme file, for the drop-to-install test. */
static QByteArray makeThemeFork() {
	const QByteArray body(1024, '\0');
	QByteArray data;
	put32(data, quint32(body.size()));
	data += body;
	QByteArray type_list;
	put16(type_list, 0);
	type_list += "icl8";
	put16(type_list, 0);
	put16(type_list, 2 + 8);
	put16(type_list, 1024);
	put16(type_list, 0xFFFF);
	type_list += '\0';
	const quint32 off = 0;
	type_list += char((off >> 16) & 0xFF);
	type_list += char((off >> 8) & 0xFF);
	type_list += char(off & 0xFF);
	type_list += QByteArray(4, '\0');
	QByteArray map;
	put32(map, 256);
	put32(map, 256 + quint32(data.size()));
	put32(map, quint32(data.size()));
	put32(map, quint32(28 + type_list.size()));
	map += QByteArray(8, '\0');
	put16(map, 28);
	put16(map, 28 + quint16(type_list.size()));
	map += type_list;
	QByteArray fork;
	put32(fork, 256);
	put32(fork, 256 + quint32(data.size()));
	put32(fork, quint32(data.size()));
	put32(fork, quint32(28 + type_list.size()));
	fork += QByteArray(240, '\0');
	fork += data;
	fork += map;
	return fork;
}
static QString preference(const char *name) {
	char value[128];
	return pl_setting(name, value, sizeof(value)) ? QString::fromUtf8(value) : QString();
}
int main(int argc, char **argv) {
	assert(argc == 2);
	QTemporaryDir root;
	qputenv("HOME", root.path().toUtf8());
	qputenv("XDG_DATA_HOME", (root.path() + "/data").toUtf8());
	qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
	qputenv("XDG_CACHE_HOME", (root.path() + "/cache").toUtf8());
	qputenv("XDG_RUNTIME_DIR", root.path().toUtf8());
	qputenv("PATH", (root.path() + ":/usr/bin:/bin").toUtf8());
	qputenv("TEST_LOG", (root.path() + "/played").toUtf8());
	QApplication app(argc, argv);
	assert(soundThemesFolder() == root.path() + "/data/zacos9/appearance/Sound Themes");
	assert(userAlertSoundsFolder() == root.path() + "/data/zacos9/sounds");
	assert(appearanceThemesFolder() == root.path() + "/data/zacos9/appearance/Theme Files");
	const QString recording = root.path() + "/Recorded Chime.wav";
	assert(QFile::copy(QString::fromUtf8(argv[1]), recording));
	QString customAlert, importError;
	assert(importAlertSound(recording, &customAlert, &importError));
	assert(customAlert == "Recorded_Chime" &&
		QFile::exists(userAlertSoundsFolder() + "/" + customAlert + ".wav"));
	const QString badRecording = root.path() + "/not-a-wave.wav";
	write(badRecording, "not a wave file");
	assert(!importAlertSound(badRecording, &customAlert, &importError) && !importError.isEmpty());
	assert(!importAlertSound(recording, &customAlert, &importError) && importError.contains("already exists"));
	/* One list for the panels, the Sounds folder and the Appearance
	 * panel: the built-in Chime, the sounds on this computer, the user's
	 * own, and None at the end. */
	const QStringList sounds = alertSoundIds();
	assert(!sounds.isEmpty() && sounds.first() == "platinum" &&
		sounds.last() == "none");
	assert(sounds.contains("Recorded_Chime"));
	assert(sounds.count("platinum") == 1 && sounds.count("none") == 1);
	assert(alertSoundDisplayName("platinum") == "Chime" &&
		alertSoundDisplayName("Recorded_Chime") == "Recorded_Chime");
	QObject owner;
	int changes = 0;
	watchCustomThemes(&owner, [&] { ++changes; });
	const QString custom = soundThemesFolder() + "/Original Test";
	assert(QDir().mkpath(custom));
	assert(QFile::copy(QString::fromUtf8(argv[1]), custom + "/click.wav"));
	write(custom + "/theme.json", R"JSON({"version":1,"name":"Original Test",
		"events":{"button-click":"click.wav","checkbox-toggle":"click.wav",
		"window-collapse":"click.wav","window-expand":"click.wav",
		"window-drag":"click.wav","window-drag-end":"click.wav",
		"startup":"click.wav","shutdown":"click.wav"}})JSON");
	assert(waitFor([] { return soundThemeCount() == 3; }));
	assert(changes > 0);
	assert(soundThemeName(2) == "Original Test");
	AppearancePanel panel;
	panel.showTab(4);
	QKeyEvent selectAlert(QEvent::KeyPress, Qt::Key_R, Qt::NoModifier, "Recorded_Chime");
	QApplication::sendEvent(&panel, &selectAlert);
	assert(preference("alert-sound") == "Recorded_Chime");
	/* A recording dropped into the System Folder's Sounds while the panel
	 * is open is offered without reopening it: the panel follows the
	 * folder. The watcher says when the change has been taken in. */
	const int before = changes;
	assert(QFile::copy(QString::fromUtf8(argv[1]), userAlertSoundsFolder() + "/Later_Chime.wav"));
	assert(waitFor([&] { return changes > before; }));
	QKeyEvent selectLater(QEvent::KeyPress, Qt::Key_L, Qt::NoModifier, "Later_Chime");
	QApplication::sendEvent(&panel, &selectLater);
	assert(preference("alert-sound") == "Later_Chime");
	panel.showTab(4);
	panel.show();
	QKeyEvent tabToSets(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
	QApplication::sendEvent(&panel, &tabToSets);
	QKeyEvent selectSound(QEvent::KeyPress, Qt::Key_O, Qt::NoModifier, "Original Test");
	QApplication::sendEvent(&panel, &selectSound);
	assert(preference("sound-theme") == soundThemeId(2));
	const QString screenshot = qEnvironmentVariable("THEMES_SCREENSHOT");
	if (!screenshot.isEmpty()) {
		assert(panel.grab().save(screenshot));
	}
	QString error;
	assert(applySoundTheme(2, &error));
	const QString id = soundThemeId(2);
	assert(preference("sound-theme") == id);
	assert(!preference("sound.button-click").isEmpty());
	for (const char *key : { "sound.window-collapse", "sound.window-expand",
			"sound.window-drag", "sound.window-drag-end", "sound.startup",
			"sound.shutdown" }) {
		assert(!preference(key).isEmpty());
	}
	assert(preference("sound.window-open").isEmpty());
	write(root.path() + "/pw-play", "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$TEST_LOG\"\nsleep 0.4\n");
	QFile player(root.path() + "/pw-play");
	assert(player.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
	pl_sound_play("Recorded_Chime");
	assert(waitFor([&] {
		QFile playback(root.path() + "/played");
		return playback.open(QIODevice::ReadOnly) &&
			playback.readAll().contains("/data/zacos9/sounds/Recorded_Chime.wav");
	}));
	QFile customPlayback(root.path() + "/played");
	assert(customPlayback.open(QIODevice::ReadOnly));
	assert(customPlayback.readAll().contains("/data/zacos9/sounds/Recorded_Chime.wav"));
	customPlayback.close();
	assert(QFile::remove(root.path() + "/played"));
	assert(pl_setting_set("interface-volume", "3"));
	pl_sound_event("button-click");
	assert(waitFor([&] {
		QFile playback(root.path() + "/played");
		if (!playback.open(QIODevice::ReadOnly)) {
			return false;
		}
		const QByteArray played = playback.readAll();
		return played.contains("--volume 0.429") && played.contains("/cache/zacos9/sounds/");
	}));
	QFile log(root.path() + "/played");
	assert(log.open(QIODevice::ReadOnly));
	const QByteArray played = log.readAll();
	assert(played.contains("--volume 0.429") && played.contains("/cache/zacos9/sounds/"));
	log.close();
	QThread::msleep(100);
	pl_sound_event("checkbox-toggle");
	QThread::msleep(100);
	assert(log.open(QIODevice::ReadOnly));
	assert(log.readAll().count('\n') == 1);
	log.close();
	assert(QFile::remove(root.path() + "/played"));
	assert(pl_setting_set("interface-volume", "0"));
	previewSoundTheme(2);
	QThread::msleep(100);
	assert(!QFile::exists(root.path() + "/played"));
	assert(pl_setting_set("interface-volume", "5"));
	assert(applySoundTheme(0, &error));
	pl_sound_event("button-click");
	assert(preference("sound.button-click").isEmpty());
	assert(preference("sound.window-drag").isEmpty());
	QThread::msleep(100);
	assert(!QFile::exists(root.path() + "/played"));
	assert(applySoundTheme(2, &error));
	assert(pl_setting_set("highlight", "FFCC66"));
	assert(saveAppearanceTheme("My Settings", &error));
	assert(waitFor([] { return appearanceThemeCount() == 2; }));
	assert(!saveAppearanceTheme("My Settings", &error));
	assert(pl_setting_set("highlight", "CCEEFF"));
	assert(applyAppearanceTheme(1, &error));
	assert(preference("highlight") == "FFCC66");
	assert(preference("sound-theme") == id);
	/* A preset without a theme bundle drops the previous one: applying
	 * My Settings (no bundle) clears theme-bundle, a bundled preset sets
	 * it, and Silver Coat clears it again. */
	assert(pl_setting_set("theme-bundle", "Stale"));
	assert(applyAppearanceTheme(1, &error));
	assert(preference("theme-bundle").isEmpty());
	write(appearanceThemesFolder() + "/bundled.json",
		R"JSON({"version":1,"name":"Bundled","settings":{"theme-bundle":"MyBundle"}})JSON");
	assert(waitFor([] { return appearanceThemeCount() == 3; }));
	int bundled = -1;
	for (int i = 0; i < appearanceThemeCount(); ++i) {
		if (appearanceThemeName(i) == "Bundled") {
			bundled = i;
		}
	}
	assert(bundled >= 0 && applyAppearanceTheme(bundled, &error));
	assert(preference("theme-bundle") == "MyBundle");
	assert(applyAppearanceTheme(0, &error));
	assert(preference("theme-bundle").isEmpty());
	/* Silver Coat also resets the sound theme to none; put the test's
	 * sound theme back for the assertions below. */
	assert(applySoundTheme(2, &error));
	assert(QFile::remove(appearanceThemesFolder() + "/bundled.json"));
	assert(waitFor([] { return appearanceThemeCount() == 2; }));
	/* A preset without a program font drops the previous one, and a
	 * preset naming a missing font is refused. */
	assert(pl_setting_set("font-family", "Stale Font"));
	assert(applyAppearanceTheme(0, &error));
	assert(preference("font-family").isEmpty());
	write(appearanceThemesFolder() + "/badfont.json",
		R"JSON({"version":1,"name":"BadFont","settings":{"font-family":"No Such Font"}})JSON");
	assert(waitFor([] { return appearanceThemeCount() == 3; }));
	int badfont = -1;
	for (int i = 0; i < appearanceThemeCount(); ++i) {
		if (appearanceThemeName(i) == "BadFont") {
			badfont = i;
		}
	}
	assert(badfont >= 0 && !applyAppearanceTheme(badfont, &error) && error.contains("font"));
	assert(QFile::remove(appearanceThemesFolder() + "/badfont.json"));
	assert(waitFor([] { return appearanceThemeCount() == 2; }));
	assert(applySoundTheme(2, &error));
	/* Dropping a Mac OS 9 theme file into Appearance > Theme Files files
	 * it into its own folder (data fork, fork and preset together) and
	 * installs it. */
	const QString drop = appearanceThemesFolder() + "/Aqua";
	write(drop, makeThemeFork());
	auto waitCount = [&](int n) {
		QElapsedTimer timer;
		timer.start();
		while (appearanceThemeCount() != n && timer.elapsed() < 30000) {
			QApplication::processEvents();
			QThread::msleep(200);
		}
		return appearanceThemeCount() == n;
	};
	assert(waitCount(3));
	int aqua = -1;
	for (int i = 0; i < appearanceThemeCount(); ++i) {
		if (appearanceThemeName(i) == "Aqua") {
			aqua = i;
		}
	}
	assert(aqua >= 0);
	assert(preference("appearance-theme") == appearanceThemeId(aqua));
	assert(preference("theme-bundle") == "Aqua");
	const QString bundle = root.path() + "/config/zacos9/themes/Aqua";
	assert(QFileInfo::exists(bundle + "/menu-icon.png"));
	const QString folder = appearanceThemesFolder() + "/Aqua";
	assert(QFileInfo(folder).isDir());
	assert(QFileInfo::exists(folder + "/Aqua"));
	assert(QDir(folder).entryInfoList(QStringList("*.json"), QDir::Files).size() == 1);
	/* No re-imports while it sits there: the preset count and the bundle
	 * stay put across polls. */
	const QDateTime firstWrite = QFileInfo(bundle + "/theme.conf").lastModified();
	for (int i = 0; i < 15; ++i) {
		QApplication::processEvents();
		QThread::msleep(200);
	}
	assert(appearanceThemeCount() == 3);
	assert(QFileInfo(bundle + "/theme.conf").lastModified() == firstWrite);
	assert(QDir(folder).removeRecursively());
	for (int i = 0; i < 15; ++i) {
		QApplication::processEvents();
		QThread::msleep(200);
	}
	assert(appearanceThemeCount() == 2);
	write(appearanceThemesFolder() + "/bad.json",
		R"JSON({"version":1,"name":"Bad","settings":{"accent":"no-such-accent"}})JSON");
	assert(waitFor([] { return appearanceThemeCount() == 3; }));
	int invalid = -1;
	for (int i = 0; i < appearanceThemeCount(); ++i) {
		if (appearanceThemeName(i) == "Bad") {
			invalid = i;
		}
	}
	assert(!applyAppearanceTheme(invalid, &error) && error.contains("accent"));
	/* A Themes folder left by older ZacOS 9 files itself into Theme Files
	 * and keeps its installed bundle pointed at the moved fork. */
	const QString legacy = root.path() + "/data/zacos9/appearance/Themes";
	assert(QDir().mkpath(legacy));
	write(legacy + "/Old", makeThemeFork());
	const QString oldBundle = root.path() + "/config/zacos9/themes/Old";
	assert(QDir().mkpath(oldBundle));
	/* The old bundle records the extractor that built it; the panel only
	 * rebuilds a bundle that an older extractor made. */
	QByteArray extractor;
	QFile aquaConf(bundle + "/theme.conf");
	if (aquaConf.open(QIODevice::ReadOnly | QIODevice::Text)) {
		for (const QByteArray &line : aquaConf.readAll().split('\n')) {
			if (line.startsWith("extractor-sha256=")) {
				extractor = line + "\n";
			}
		}
	}
	assert(!extractor.isEmpty());
	write(oldBundle + "/theme.conf",
		QByteArray("source=" + legacy.toUtf8() + "/Old\nname=Old\n") + extractor);
	write(legacy + "/oldpreset.json",
		R"JSON({"version":1,"name":"OldSaved","settings":{"theme-bundle":"Old"}})JSON");
	const QString oldFolder = appearanceThemesFolder() + "/Old";
	assert(waitFor([&] {
		return QFileInfo(oldFolder).isDir() && QFileInfo::exists(oldFolder + "/Old");
	}));
	QFile conf(oldBundle + "/theme.conf");
	assert(conf.open(QIODevice::ReadOnly | QIODevice::Text));
	assert(QString::fromUtf8(conf.readAll()).contains("source=" + oldFolder + "/Old"));
	assert(waitFor([&] { return QFileInfo::exists(oldFolder + "/oldpreset.json"); }));
	for (int i = 0; i < 15; ++i) {
		QApplication::processEvents();
		QThread::msleep(200);
	}
	assert(appearanceThemeCount() == 4);
	/* Without a bundle claiming it, the filed theme installs like any
	 * other drop. */
	assert(QFile::remove(oldBundle + "/theme.conf"));
	assert(QDir().rmdir(oldBundle));
	{
		QElapsedTimer timer;
		timer.start();
		while (appearanceThemeCount() != 5 && timer.elapsed() < 60000) {
			QApplication::processEvents();
			QThread::msleep(200);
		}
		assert(appearanceThemeCount() == 5);
	}
	/* A leftover companion-named bundle from the old importer is swept:
	 * bundles can never legitimately start with "._". */
	const QString junkBundle = root.path() + "/config/zacos9/themes/._Junk";
	assert(QDir().mkpath(junkBundle));
	write(junkBundle + "/theme.conf", "name=._Junk\n");
	assert(waitFor([&] { return !QFileInfo::exists(junkBundle); }));
	/* A lone AppleDouble companion dropped on its own still installs,
	 * filed under the data-fork name with no "._" bundle left behind. */
	const QString lone = appearanceThemesFolder() + "/._Lone";
	write(lone, makeThemeFork());
	assert(waitCount(6));
	int loneIdx = -1;
	for (int i = 0; i < appearanceThemeCount(); ++i) {
		if (appearanceThemeName(i) == "Lone") {
			loneIdx = i;
		}
	}
	assert(loneIdx >= 0);
	const QString loneBundle = root.path() + "/config/zacos9/themes/Lone";
	assert(QFileInfo(loneBundle).isDir());
	assert(!QFileInfo::exists(root.path() + "/config/zacos9/themes/._Lone"));
	assert(QDir(appearanceThemesFolder() + "/Lone").entryInfoList(QStringList("*.json"), QDir::Files).size() == 1);
	write(appearanceThemesFolder() + "/classic-skin", "unsupported synthetic skin");
	assert(waitFor([] { return !customThemeErrors().isEmpty(); }));
	assert(QFile::remove(custom + "/theme.json"));
	assert(QFile::remove(custom + "/click.wav"));
	assert(QDir().rmdir(custom));
	assert(waitFor([] { return soundThemeCount() == 2; }));
	assert(preference("sound-theme") == id);
	assert(preference("sound.button-click").isEmpty());
	assert(!applySoundTheme(2, &error));

	/* Smart Scrolling for GTK 3: a marked block in the user's gtk.css,
	 * added and taken out without touching what else is there. */
	const QString gtk = root.path() + "/config/gtk-3.0/gtk.css";
	setGtkSmartScrolling(false);
	assert(!QFile::exists(gtk));
	QDir().mkpath(root.path() + "/config/gtk-3.0");
	write(gtk, "label { color: red; }");
	setGtkSmartScrolling(true);
	setGtkSmartScrolling(true);
	QFile css(gtk);
	assert(css.open(QIODevice::ReadOnly));
	const QString on = QString::fromUtf8(css.readAll());
	css.close();
	assert(on.startsWith("label { color: red; }\n"));
	assert(on.count("has-secondary-backward-stepper: true") == 1);
	setGtkSmartScrolling(false);
	assert(css.open(QIODevice::ReadOnly));
	assert(QString::fromUtf8(css.readAll()) == "label { color: red; }\n");
	return 0;
}
