/*
 * zacos9-pack: the Pack, Mac OS 9's Application palette (packwindow.h).
 * zacos9-pack --at X,Y: the pointer let go there (the menu bar's tear-off),
 * so the palette's title bar goes under it; without --at it comes back where
 * it was. One Pack at a time: another run moves the first.
 */
#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>

#include "packwindow.h"
#include "platinumshell.h"
#include "settings.h"

/* In the session's runtime folder: one Pack per session, whatever its
 * display is called. */
static QString serverName() {
	const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
	const QString name = "zacos9-pack-" + qEnvironmentVariable("WAYLAND_DISPLAY", "wayland-0");
	return runtime.isEmpty() ? name : runtime + "/" + name;
}

static bool parseAt(const QStringList &args, QPoint *at) {
	const int i = args.indexOf("--at");
	if (i < 0 || i + 1 >= args.size()) {
		return false;
	}
	const QStringList xy = args[i + 1].split(',');
	bool ok1 = false, ok2 = false;
	if (xy.size() == 2) {
		*at = QPoint(xy[0].toInt(&ok1), xy[1].toInt(&ok2));
	}
	return ok1 && ok2;
}

int main(int argc, char *argv[]) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Pack");
	QGuiApplication::setDesktopFileName("zacos9-pack");
	if (QIcon::themeName().isEmpty()) {
		QIcon::setThemeName("hicolor");
	}
	QPoint at;
	const bool placed = parseAt(app.arguments(), &at);

	/* Already up: it moves to where it was asked for. */
	QLocalSocket other;
	other.connectToServer(serverName());
	if (other.waitForConnected(300)) {
		if (placed) {
			other.write(QString("at %1 %2\n").arg(at.x()).arg(at.y()).toUtf8());
			other.waitForBytesWritten(300);
		}
		return 0;
	}
	QLocalServer::removeServer(serverName());
	QLocalServer server;
	server.listen(serverName());

	platinumShellInit();
	PackWindow pack;
	if (placed) {
		pack.dropAt(at);
	}
	QObject::connect(&server, &QLocalServer::newConnection, [&server, &pack] {
		QLocalSocket *s = server.nextPendingConnection();
		QObject::connect(s, &QLocalSocket::readyRead, [s, &pack] {
			const QStringList w = QString::fromUtf8(s->readAll()).simplified().split(' ');
			if (w.size() == 3 && w[0] == "at") {
				pack.dropAt(QPoint(w[1].toInt(), w[2].toInt()));
			}
		});
		QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
	});
	/* Kept open across sessions: the menu bar starts it again at login. */
	pl_setting_set("pack-open", "true");
	pack.show();
	return app.exec();
}
