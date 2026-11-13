#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImage>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QThread>
#include <cassert>
#include <functional>

#include "customthemes.h"
#include "custompatterns.h"
#include "appearance.h"
#include "settings.h"
#include "theme.h"
#include "themeeditor.h"

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
	QImage artSample(12, 12, QImage::Format_ARGB32);
	artSample.fill(qRgba(40, 120, 200, 255));
	const QString artSamplePath = root.path() + "/art-sample.png";
	assert(artSample.save(artSamplePath));
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
	/* A second, distinct sample for window-open, so the preview can be
	 * checked to play that event rather than button-click. */
	QFile sample(QString::fromUtf8(argv[1]));
	assert(sample.open(QIODevice::ReadOnly));
	QByteArray windowOpen = sample.readAll();
	sample.close();
	const int data = windowOpen.indexOf("data");
	assert(data > 0 && data + 9 < windowOpen.size());
	windowOpen[data + 9] = char(windowOpen[data + 9] ^ 0x01);
	write(custom + "/open.wav", windowOpen);
	write(custom + "/theme.json", R"JSON({"version":1,"name":"Original Test",
		"events":{"button-click":"click.wav","checkbox-toggle":"click.wav",
		"window-collapse":"click.wav","window-expand":"click.wav",
		"window-drag":"click.wav","window-drag-end":"click.wav",
		"window-open":"open.wav",
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
	const QString editorShot = qEnvironmentVariable("THEME_EDITOR_SCREENSHOT");
	if (!editorShot.isEmpty()) {
		ThemeEditorWindow editor(-1);
		editor.show();
		QApplication::processEvents();
		assert(editor.grab().save(editorShot));
	}
	const QString themesShot = qEnvironmentVariable("THEMES_TAB_SCREENSHOT");
	if (!themesShot.isEmpty()) {
		AppearancePanel themesPanel;
		themesPanel.showTab(0);
		themesPanel.show();
		QApplication::processEvents();
		assert(themesPanel.grab().save(themesShot));
	}
	const QString artworkShot = qEnvironmentVariable("THEME_ARTWORK_SCREENSHOT");
	if (!artworkShot.isEmpty()) {
		ThemeDraft artDraft;
		artDraft.chrome = themePaletteDefaults(false);
		artDraft.chrome["bar-left"] = "chrome/custom-bar-left.argb";
		artDraft.artSources["bar-left"] = artSamplePath;
		artDraft.chrome["document-top"] = "off";
		ThemeArtworkWindow artwork(&artDraft, [] {}, [] {}, [] {}, [] {});
		artwork.show();
		QApplication::processEvents();
		assert(artwork.grab().save(artworkShot));
	}
	QString error;
	assert(applySoundTheme(2, &error));
	const QString id = soundThemeId(2);
	assert(preference("sound-theme") == id);
	assert(!preference("sound.button-click").isEmpty());
	for (const char *key : { "sound.window-collapse", "sound.window-expand",
			"sound.window-drag", "sound.window-drag-end", "sound.startup",
			"sound.shutdown", "sound.window-open" }) {
		assert(!preference(key).isEmpty());
	}
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
	/* Preview plays the set's window-open, not its button-click. */
	previewSoundTheme(2);
	assert(waitFor([&] {
		QFile playback(root.path() + "/played");
		return playback.open(QIODevice::ReadOnly) &&
			playback.readAll().contains(preference("sound.window-open").toUtf8());
	}));
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
	/* A preset without a program font drops the previous one; one naming
	 * a missing font still applies, without the font and with a note. */
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
	assert(badfont >= 0 && applyAppearanceTheme(badfont, &error));
	assert(error.contains("font") && preference("font-family").isEmpty());
	assert(QFile::remove(appearanceThemesFolder() + "/badfont.json"));
	assert(waitFor([] { return appearanceThemeCount() == 2; }));
	/* A preset whose sound set the catalogue could not decode (Drawing
	 * Board's) still applies: one missing piece of art must not make the
	 * theme unselectable. */
	write(appearanceThemesFolder() + "/badsound.json",
		R"JSON({"version":1,"name":"BadSound","settings":{"sound-theme":"user:00000000000000000000000000000000"}})JSON");
	assert(waitFor([] { return appearanceThemeCount() == 3; }));
	int badsound = -1;
	for (int i = 0; i < appearanceThemeCount(); ++i) {
		if (appearanceThemeName(i) == "BadSound") {
			badsound = i;
		}
	}
	assert(badsound >= 0 && applyAppearanceTheme(badsound, &error));
	assert(error.contains("sound set") && preference("sound-theme").isEmpty());
	assert(QFile::remove(appearanceThemesFolder() + "/badsound.json"));
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
	/* ---- the Theme Editor's user themes ---- */
	auto findTheme = [](const QString &name) {
		for (int i = 0; i < appearanceThemeCount(); ++i) {
			if (appearanceThemeName(i) == name) {
				return i;
			}
		}
		return -1;
	};
	QString editorError;
	ThemeDraft darkDraft;
	darkDraft.name = "Dark Test";
		darkDraft.settings = {
		{ "accent", "" }, { "highlight", "CCEEFF" }, { "pattern", "" },
		{ "wallpaper", "" }, { "background", "pattern" }, { "wallpaper-mode", "fit" },
		{ "alert-sound", "" }, { "sound-theme", "none" }, { "font-family", "" },
	};
	darkDraft.chrome = themePaletteDefaults(true);
	/* A saved theme clears choices it does not name, like a preset. */
	assert(pl_setting_set("alert-sound", "Recorded_Chime"));
	assert(pl_setting_set("pattern", "StalePattern"));
	assert(themeDraftSave(&darkDraft, -1, &editorError));
	assert(preference("alert-sound").isEmpty());
	assert(preference("pattern").isEmpty());
	assert(darkDraft.bundleId == "Dark Test");
	const QString darkConfPath = root.path() + "/config/zacos9/themes/Dark Test/theme.conf";
	assert(QFileInfo::exists(darkConfPath));
	assert(QFileInfo::exists(darkDraft.presetPath));
	{
		QFile conf(darkConfPath);
		assert(conf.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(conf.readAll());
		assert(text.contains("user-theme=1"));
		assert(text.contains("title-base=3A3A3A"));
		assert(text.contains("title-text=FFFFFF"));
		assert(text.contains("menu-bar=333333"));
		assert(!text.contains("source="));
		assert(!text.contains("extractor-sha256="));
	}
	assert(preference("theme-bundle") == "Dark Test");
	assert(waitFor([&] { return findTheme("Dark Test") >= 0; }));
	int darkIndex = findTheme("Dark Test");
	assert(darkIndex >= 0);
	assert(appearanceThemeId(darkIndex) == preference("appearance-theme"));
	assert(appearanceThemeBundle(darkIndex) == "Dark Test");
	assert(appearanceThemeIsUser(darkIndex));
	assert(appearanceThemeValues(darkIndex).value("highlight") == "CCEEFF");

	/* Editing an imported theme writes a linked copy: the import's own
	 * bundle and preset are left exactly as they were. */
	QByteArray aquaBefore;
	{
		QFile conf(bundle + "/theme.conf");
		assert(conf.open(QIODevice::ReadOnly));
		aquaBefore = conf.readAll();
	}
	const QString aquaPresetPath = appearanceThemePath(findTheme("Aqua"));
	assert(!aquaPresetPath.isEmpty());
	ThemeDraft copyDraft;
	assert(themeDraftFromPreset(findTheme("Aqua"), &copyDraft, &editorError));
	assert(copyDraft.linked);
	assert(copyDraft.inherits == "Aqua");
	assert(copyDraft.bundleId.isEmpty());
	copyDraft.name = "Aqua Dark";
	copyDraft.chrome = themePaletteDefaults(true);
	assert(themeDraftSave(&copyDraft, findTheme("Aqua"), &editorError));
	assert(copyDraft.bundleId == "Aqua Dark");
	const QString copyConf = root.path() + "/config/zacos9/themes/Aqua Dark/theme.conf";
	assert(QFileInfo::exists(copyConf));
	{
		QFile conf(copyConf);
		assert(conf.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(conf.readAll());
		assert(text.contains("inherits=Aqua"));
		assert(text.contains("user-theme=1"));
	}
	{
		QFile conf(bundle + "/theme.conf");
		assert(conf.open(QIODevice::ReadOnly));
		assert(conf.readAll() == aquaBefore);
	}
	assert(QFileInfo::exists(aquaPresetPath));
	assert(preference("theme-bundle") == "Aqua Dark");
	/* The linked copy takes its artwork from the import it names. */
	{
		char asset[4096];
		assert(zac_theme_asset("menu-icon.png", asset, sizeof(asset)));
		assert(QString::fromUtf8(asset).startsWith(bundle + "/"));
		uint32_t value = 0;
		assert(zac_theme_color("title-base", &value));
		assert(value == 0xFF3A3A3A);
	}
	assert(waitFor([&] { return findTheme("Aqua Dark") >= 0; }));
	int copyIndex = findTheme("Aqua Dark");
	assert(copyIndex >= 0 && appearanceThemeIsUser(copyIndex));
	/* Editing it again changes the same bundle and preset in place. */
	{
		ThemeDraft edit;
		assert(themeDraftFromPreset(copyIndex, &edit, &editorError));
		assert(!edit.linked && edit.bundleId == "Aqua Dark");
		const QString presetPath = edit.presetPath;
		edit.name = "Aqua Darker";
		edit.chrome["title-base"] = "1A1A1A";
		assert(themeDraftSave(&edit, copyIndex, &editorError));
		assert(edit.bundleId == "Aqua Dark");
		assert(edit.presetPath == presetPath);
	}
	assert(waitFor([&] { return findTheme("Aqua Darker") >= 0; }));
	int darkerIndex = findTheme("Aqua Darker");
	assert(darkerIndex >= 0);
	assert(preference("theme-bundle") == "Aqua Dark");
	assert(preference("appearance-theme") == appearanceThemeId(darkerIndex));

	/* A plain saved preset becomes a full theme in its own file. */
	int plainIndex = findTheme("My Settings");
	assert(plainIndex >= 0);
	QString plainPath;
	{
		ThemeDraft plain;
		assert(themeDraftFromPreset(plainIndex, &plain, &editorError));
		assert(!plain.linked && plain.bundleId.isEmpty());
		plainPath = plain.presetPath;
		assert(QFileInfo(plainPath).absolutePath() == appearanceThemesFolder());
		plain.name = "My Settings Edited";
		plain.chrome = themePaletteDefaults(true);
		assert(themeDraftSave(&plain, plainIndex, &editorError));
		assert(plain.presetPath == plainPath);
		assert(preference("theme-bundle") == plain.bundleId);
	}
	assert(waitFor([&] { return findTheme("My Settings Edited") >= 0; }));
	assert(findTheme("My Settings Edited") >= 0);

	/* Writing a chosen image as a chrome tile or a PNG skin. */
	{
		QString artError;
		const QString argbTarget = root.path() + "/test-tile.argb";
		assert(themeArtWrite(artSamplePath, argbTarget, true, &artError));
		QFile argb(argbTarget);
		assert(argb.open(QIODevice::ReadOnly));
		const QByteArray bytes = argb.readAll();
		assert(bytes.size() == 8 + 12 * 12 * 4);
		assert((uchar)bytes[0] == 12 && (uchar)bytes[4] == 12);
		assert((uchar)bytes[8] == 40 && (uchar)bytes[9] == 120 &&
			(uchar)bytes[10] == 200 && (uchar)bytes[11] == 255);
		const QString pngTarget = root.path() + "/test-skin.png";
		assert(themeArtWrite(artSamplePath, pngTarget, false, &artError));
		QImage skin(pngTarget);
		assert(!skin.isNull() && skin.size() == QSize(12, 12));
		assert(!themeArtWrite(root.path() + "/missing-image.png", pngTarget, false, &artError));
		auto findSlot = [](const QString &key) {
			for (const ThemeArtSlot &slot : themeArtSlots()) {
				if (slot.key == key) {
					return slot;
				}
			}
			return ThemeArtSlot{};
		};
		const ThemeArtSlot barMiddle = findSlot("bar-middle");
		const ThemeArtSlot docTop = findSlot("document-top");
		assert(barMiddle.argb && !barMiddle.key.isEmpty());
		assert(!docTop.argb && docTop.defaultPath == "chrome/document-top.png");
		ThemeDraft state;
		state.chrome = themePaletteDefaults(false);
		assert(themeArtState(state, barMiddle) == ThemeArtState::Platinum);
		state.importedChrome["bar-middle"] = "chrome/bar.argb";
		state.chrome["bar-middle"] = "chrome/bar.argb";
		assert(themeArtState(state, barMiddle) == ThemeArtState::Inherit);
		state.chrome["bar-middle"] = "chrome/custom.argb";
		assert(themeArtState(state, barMiddle) == ThemeArtState::Custom);
		state.chrome.remove("bar-middle");
		const QString baseArt = root.path() + "/art-base";
		assert(QDir().mkpath(baseArt + "/chrome"));
		assert(QFile::copy(artSamplePath, baseArt + "/chrome/document-top.png"));
		state.baseBundleDir = baseArt;
		assert(themeArtState(state, docTop) == ThemeArtState::Inherit);
		state.chrome["document-top"] = "off";
		assert(themeArtState(state, docTop) == ThemeArtState::Platinum);
		state.chrome["document-top"] = "chrome/custom-document-top.png";
		assert(themeArtState(state, docTop) == ThemeArtState::Custom);

		/* Save writes the chosen images into the bundle beside theme.conf
		 * and points the keys at those copies. */
		ThemeDraft artDraft;
		artDraft.name = "Art Test";
		artDraft.settings = darkDraft.settings;
		artDraft.chrome = themePaletteDefaults(true);
		artDraft.artSources["bar-middle"] = artSamplePath;
		artDraft.artSources["document-top"] = artSamplePath;
		assert(themeDraftSave(&artDraft, -1, &artError));
		const QString artDir = root.path() + "/config/zacos9/themes/Art Test";
		assert(QFileInfo::exists(artDir + "/chrome/custom-bar-middle.argb"));
		assert(QFileInfo::exists(artDir + "/chrome/custom-document-top.png"));
		{
			QFile conf(artDir + "/theme.conf");
			assert(conf.open(QIODevice::ReadOnly));
			const QString text = QString::fromUtf8(conf.readAll());
			assert(text.contains("bar-middle=chrome/custom-bar-middle.argb"));
			assert(text.contains("document-top=chrome/custom-document-top.png"));
		}
		assert(waitFor([&] { return findTheme("Art Test") >= 0; }));
		const int artIndex = findTheme("Art Test");
		ThemeDraft reloaded;
		assert(themeDraftFromPreset(artIndex, &reloaded, &artError));
		assert(themeArtState(reloaded, findSlot("bar-middle")) == ThemeArtState::Custom);
		assert(themeArtState(reloaded, findSlot("document-top")) == ThemeArtState::Custom);
		const QString artEditorShot = qEnvironmentVariable("THEME_EDITOR_ART_SCREENSHOT");
		if (!artEditorShot.isEmpty()) {
			ThemeEditorWindow editor(artIndex);
			editor.show();
			QApplication::processEvents();
			assert(editor.grab().save(artEditorShot));
		}
		const QString reloadedArtShot = qEnvironmentVariable("THEME_ARTWORK_RELOADED_SCREENSHOT");
		if (!reloadedArtShot.isEmpty()) {
			ThemeArtworkWindow artwork(&reloaded, [] {}, [] {}, [] {}, [] {});
			artwork.selectSlot("bar-middle");
			artwork.show();
			QApplication::processEvents();
			assert(artwork.grab().save(reloadedArtShot));
		}
		/* A duplicate carries the source bundle's own artwork files. */
		ThemeDraft duplicate = reloaded;
		for (const ThemeArtSlot &slot : themeArtSlots()) {
			const QString value = duplicate.chrome.value(slot.key);
			if (!value.isEmpty() && value != "off" &&
					QFileInfo(artDir + "/" + value).isFile()) {
				duplicate.artSources[slot.key] = artDir + "/" + value;
			}
		}
		duplicate.name = "Art Copy";
		duplicate.bundleId.clear();
		duplicate.presetPath.clear();
		assert(themeDraftSave(&duplicate, -1, &artError));
		assert(QFileInfo::exists(root.path() +
			"/config/zacos9/themes/Art Copy/chrome/custom-bar-middle.argb"));
		assert(QFileInfo::exists(root.path() +
			"/config/zacos9/themes/Art Copy/chrome/custom-document-top.png"));
		assert(waitFor([&] { return findTheme("Art Copy") >= 0; }));
		const int copyIndex = findTheme("Art Copy");
		assert(copyIndex >= 0 && themeDraftDelete(copyIndex, &artError));
		assert(waitFor([&] { return findTheme("Art Copy") < 0; }));
		const int artAgain = findTheme("Art Test");
		assert(artAgain >= 0 && themeDraftDelete(artAgain, &artError));
		assert(waitFor([&] { return findTheme("Art Test") < 0; }));
		assert(!QFileInfo(artDir).exists());
	}

	/* Dropping the import's chrome art leaves the palette and the
	 * inherited icons, pointers and fonts. */
	{
		ThemeDraft drop;
		drop.chrome = {
			{ "bar-middle", "chrome/bar.argb" },
			{ "button-close", "chrome/close.argb" },
			{ "menu-background-image", "chrome/menu.png" },
			{ "title-cluster", "left" },
			{ "accent", "ocean" },
			{ "frame-face", "303030" },
		};
		themeDraftDropChromeArt(&drop);
		assert(!drop.chrome.contains("bar-middle"));
		assert(!drop.chrome.contains("button-close"));
		assert(drop.chrome.value("menu-background-image") == "off");
		assert(drop.chrome.value("document-top") == "off");
		assert(!drop.chrome.contains("title-cluster"));
		assert(drop.chrome.value("accent") == "ocean");
		assert(drop.chrome.value("frame-face") == "303030");
	}

	/* Invalid drafts never write anything. */
	{
		ThemeDraft bad;
		bad.name = "Bad Theme";
		bad.settings = darkDraft.settings;
		bad.chrome = themePaletteDefaults(true);
		bad.chrome["title-base"] = "NOTHEX";
		assert(!themeDraftSave(&bad, -1, &editorError));
		assert(!QFileInfo::exists(root.path() + "/config/zacos9/themes/Bad Theme"));
		bad.chrome["title-base"] = "3A3A3A";
		bad.inherits = "No Such Import";
		assert(!themeDraftSave(&bad, -1, &editorError));
		bad.inherits.clear();
		bad.name = "Aqua";
		assert(!themeDraftSave(&bad, -1, &editorError));
		assert(editorError.contains("already exists"));
	}

	/* Imported themes are never deleted; user themes are. */
	int imported = findTheme("Aqua");
	assert(imported >= 0);
	assert(!themeDraftDelete(imported, &editorError));
	assert(editorError.contains("Imported"));
	assert(QFileInfo::exists(bundle + "/theme.conf"));
	int doomed = findTheme("My Settings Edited");
	assert(doomed >= 0);
	assert(themeDraftDelete(doomed, &editorError));
	assert(!QFileInfo::exists(plainPath));
	assert(waitFor([&] { return findTheme("My Settings Edited") < 0; }));
	int userTheme = findTheme("Aqua Darker");
	assert(userTheme >= 0);
	assert(themeDraftDelete(userTheme, &editorError));
	assert(!QFileInfo::exists(root.path() + "/config/zacos9/themes/Aqua Dark"));
	assert(waitFor([&] { return findTheme("Aqua Darker") < 0; }));
	assert(QFileInfo::exists(bundle + "/theme.conf"));
	int darkToDelete = findTheme("Dark Test");
	assert(darkToDelete >= 0);
	assert(themeDraftDelete(darkToDelete, &editorError));
	assert(waitFor([&] { return findTheme("Dark Test") < 0; }));
	/* Put the plain preset the rest of this test started with back. */
	assert(pl_setting_set("highlight", "FFCC66"));
	assert(pl_setting_set("sound-theme", soundThemeId(2).toUtf8().constData()));
	assert(saveAppearanceTheme("My Settings", &editorError));
	assert(waitFor([&] { return findTheme("My Settings") >= 0; }));
	/* Leave the imported theme selected for the tests below. */
	int aquaAgain = findTheme("Aqua");
	assert(aquaAgain >= 0 && applyAppearanceTheme(aquaAgain, &editorError));
	assert(applySoundTheme(2, &editorError));
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
	assert(QFile::remove(custom + "/open.wav"));
	assert(QDir().rmdir(custom));
	assert(waitFor([] { return soundThemeCount() == 2; }));
	assert(preference("sound-theme") == id);
	assert(preference("sound.button-click").isEmpty());
	assert(!applySoundTheme(2, &error));

	/* A preset from before the catalog names its pattern by the installed
	 * file ("Old-pattern-1"), which the compositor cannot resolve: applying
	 * it must store the catalog id instead, or the theme seems to do
	 * nothing and cannot really be selected. */
	{
		const QString folder = desktopPatternsFolder();
		assert(QDir().mkpath(folder));
		QImage tile(2, 2, QImage::Format_ARGB32);
		tile.fill(QColor(0x40, 0x80, 0xc0));
		assert(tile.save(folder + "/Old-pattern-1.png", "PNG"));
		const QString path = appearanceThemesFolder() + "/old-pattern.json";
		write(path, R"JSON({"version":1,"name":"Old Pattern","settings":{"pattern":"Old-pattern-1"}})JSON");
		assert(waitFor([&] {
			for (int i = 0; i < appearanceThemeCount(); ++i) {
				if (appearanceThemeName(i) == "Old Pattern") {
					return true;
				}
			}
			return false;
		}));
		int oldPattern = -1;
		for (int i = 0; i < appearanceThemeCount(); ++i) {
			if (appearanceThemeName(i) == "Old Pattern") {
				oldPattern = i;
			}
		}
		assert(oldPattern >= 0 && applyAppearanceTheme(oldPattern, &error));
		const QString resolved = preference("pattern");
		assert(resolved.startsWith("user:") && resolved.endsWith(":image"));
		assert(QFile::remove(path));
		/* Let the catalogue notice the removal and its helper finish
		 * before the test ends. */
		QElapsedTimer settle;
		settle.start();
		while (settle.elapsed() < 800) {
			QApplication::processEvents();
			QThread::msleep(20);
		}
	}

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
