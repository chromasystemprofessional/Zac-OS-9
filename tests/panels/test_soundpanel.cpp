/*
 * The Sound panel's Alert Sound menu: the choices are the ones the
 * Appearance panel and the System Folder's Sounds use, a recording added
 * while the panel is open is offered the next time the menu opens, and
 * choosing one stores it (Chime as no setting at all) and plays it.
 *
 * The panel's own class is used, so the pop-up, its rows and the setting
 * written are the ones a person gets; only the sound server is a stand-in.
 */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QWidget>
#include <cstdlib>
#include <functional>

#include "controlpanel.h"
#include "customthemes.h"
#include "panelkit.h"
#include "settings.h"

/* The panel as it is built: private members are how a test reaches the
 * menu it draws. controlpanel.h is already included, so nothing of Qt's
 * sees this. */
#define private public
#include "sound.cpp"
#undef private

static int fails;

static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static bool waitFor(const std::function<bool()> &done, int ms = 5000) {
	QElapsedTimer timer;
	timer.start();
	while (!done() && timer.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
	return done();
}

static void settle(int ms = 100) {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
}

static QString preference(const char *name) {
	char value[128];
	return pl_setting(name, value, sizeof(value)) ? QString::fromUtf8(value) : QString();
}

static bool writeFile(const QString &path, const QByteArray &body, bool executable = false) {
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		return false;
	}
	file.write(body);
	file.close();
	if (executable) {
		file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	}
	return true;
}

/* The pop-up menu the panel opened, if any. */
static QWidget *openMenu() {
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		if (widget->isVisible() && (widget->windowFlags() & Qt::Popup)) {
			return widget;
		}
	}
	return nullptr;
}

/* Presses the Alert Sound button: the menu opens, with the list read
 * afresh. */
static QWidget *openAlertSounds(SoundPanel &panel) {
	QMouseEvent press(QEvent::MouseButtonPress,
		QPointF(panel.m_alertSound.rect.center()), QPointF(panel.mapToGlobal(
			panel.m_alertSound.rect.center())), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&panel, &press);
	settle();
	return openMenu();
}

/* Moves the menu's highlight `steps` rows (down when positive) and chooses
 * it, as the arrow keys and Return do. */
static void chooseRow(QWidget *menu, int steps) {
	const Qt::Key key = steps < 0 ? Qt::Key_Up : Qt::Key_Down;
	for (int i = 0; i < std::abs(steps); i++) {
		QKeyEvent move(QEvent::KeyPress, key, Qt::NoModifier);
		QApplication::sendEvent(menu, &move);
	}
	QKeyEvent choose(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
	QApplication::sendEvent(menu, &choose);
	settle();
}

int main(int argc, char **argv) {
	if (argc != 2) {
		return 1;
	}
	QTemporaryDir root;
	if (!root.isValid()) {
		return 1;
	}
	/* A private desktop: the user's alert sounds are under test, the
	 * installed set comes from the source tree's assets, and the sound
	 * server is a pactl that says nothing. */
	qputenv("HOME", root.path().toUtf8());
	qputenv("XDG_DATA_HOME", (root.path() + "/data").toUtf8());
	qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
	qputenv("XDG_CACHE_HOME", (root.path() + "/cache").toUtf8());
	qputenv("XDG_RUNTIME_DIR", root.path().toUtf8());
	qputenv("TEST_LOG", (root.path() + "/played").toUtf8());
	check(writeFile(root.path() + "/bin/pactl", "#!/bin/sh\nexit 0\n", true),
		"a pactl that reports no sound server is in place");
	/* The player the alert sound is previewed with, noted down. */
	check(writeFile(root.path() + "/bin/pw-play",
		"#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$TEST_LOG\"\n", true),
		"a player that notes what it is asked to play is in place");
	qputenv("PATH", (root.path() + "/bin:/usr/bin:/bin").toUtf8());

	QApplication app(argc, argv);
	const QString folder = userAlertSoundsFolder();
	check(QDir().mkpath(folder), "the user's alert sounds folder is there");

	SoundPanel panel;
	settle();
	check(!panel.m_alertIds.isEmpty() &&
		panel.m_alertIds.size() == static_cast<int>(panel.m_alertSound.items.size()),
		"the panel's Alert Sound menu has a row for every sound");
	check(panel.m_alertIds.first() == "platinum" && panel.m_alertIds.last() == "none",
		"from Chime to None, as the Appearance panel lists them");
	check(!panel.m_alertSound.items.empty() &&
		panel.m_alertSound.items.front().text == "Chime",
		"with the built-in sound shown as Chime");
	check(panel.m_alertSound.selected == 0,
		"and Chime chosen when nothing else has been");

	/* A recording put in the folder while the panel is open -- as a drop
	 * into the System Folder's Sounds does -- is offered the next time
	 * the menu opens. */
	/* Named as importAlertSound() writes one: the panels offer only
	 * names pl_sound_play() can safely look up. */
	check(QFile::copy(QString::fromUtf8(argv[1]), folder + "/Later_Chime.wav"),
		"a recording arrives while the panel is open");
	QWidget *menu = openAlertSounds(panel);
	check(menu != nullptr, "the Alert Sound button opens its menu");
	check(panel.m_alertIds.contains("Later_Chime"),
		"which offers the recording that just arrived");
	const int later = panel.m_alertIds.indexOf("Later_Chime");
	check(later > 0 && panel.m_alertSound.items[later].text == "Later_Chime",
		"under its name");

	/* Choosing it stores it, and plays it once. */
	chooseRow(menu, later);
	check(preference("alert-sound") == "Later_Chime",
		"choosing a sound stores it as the alert sound");
	check(waitFor([] {
			QFile log(qEnvironmentVariable("TEST_LOG"));
			return log.open(QIODevice::ReadOnly) &&
				QString::fromUtf8(log.readAll()).contains("Later_Chime.wav");
		}), "and plays it, so the choice is heard");
	check(openMenu() == nullptr, "the menu closes once a sound is chosen");

	/* Chime is the default, stored as no setting at all, as the
	 * Appearance panel stores it. */
	menu = openAlertSounds(panel);
	check(menu != nullptr && panel.m_alertSound.selected == later,
		"opening it again shows the stored choice");
	chooseRow(menu, -later);
	check(preference("alert-sound").isEmpty(),
		"choosing Chime leaves no setting behind");

	return fails ? 1 : 0;
}
