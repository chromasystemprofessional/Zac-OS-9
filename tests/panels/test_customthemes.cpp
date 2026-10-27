#include <QApplication>
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
	assert(appearanceThemesFolder() == root.path() + "/data/zacos9/appearance/Themes");
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
	panel.showTab(3);
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
	panel.showTab(5);
	panel.show();
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
