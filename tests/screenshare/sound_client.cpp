/*
 * The viewer's SoundClient on its own, for test_host.py:
 *
 *   test-screenshare-sound HOST PORT FINGERPRINT USER PASSWORD SECONDS
 *
 * Listens for SECONDS (ZACOS9_PACAT plays), then prints "playing" if it
 * played and "stopped: WHY" for each time it stopped.
 */
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>

#include "sound.h"

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);
	const QStringList a = app.arguments();
	if (a.size() != 7) {
		fprintf(stderr, "usage: test-screenshare-sound HOST PORT FINGERPRINT USER PASSWORD SECONDS\n");
		return 2;
	}
	screenshare::SoundClient sound;
	sound.playing = [] {
		printf("playing\n");
		fflush(stdout);
	};
	sound.stopped = [](const QString &why) {
		printf("stopped: %s\n", qPrintable(why));
		fflush(stdout);
		QTimer::singleShot(200, qApp, &QCoreApplication::quit);
	};
	sound.open(a[1], static_cast<quint16>(a[2].toUInt()), a[3], a[4], a[5]);
	QTimer::singleShot(a[6].toInt() * 1000, &app, &QCoreApplication::quit);
	return app.exec();
}
