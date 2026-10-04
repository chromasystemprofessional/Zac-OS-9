#include "filemanager1.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "folderwindow.h"
#include "infowindow.h"
#include "items.h"

static const char SERVICE[] = "org.freedesktop.FileManager1";
static const char PATH[] = "/org/freedesktop/FileManager1";
static const char IFACE[] = "org.freedesktop.FileManager1";

/* Local paths from file:// URIs (or plain paths), existing ones only. */
static QStringList localPaths(const QStringList &uris) {
	QStringList out;
	for (const QString &uri : uris) {
		const QUrl url(uri);
		const QString path = url.isLocalFile() ? url.toLocalFile()
			: uri.startsWith('/') ? uri : QString();
		if (!path.isEmpty() && QFileInfo::exists(path)) {
			out << QDir::cleanPath(path);
		}
	}
	return out;
}

/* The item's folder, in front, with the item selected. */
static FolderWindow *reveal(const QString &path) {
	const QFileInfo info(path);
	FolderWindow *w = FolderWindow::open(info.absolutePath());
	if (w) {
		w->selectByName(info.fileName());
	}
	return w;
}

class FileManager1 : public QDBusVirtualObject {
public:
	QString introspect(const QString &) const override {
		return QStringLiteral(
			"<interface name='org.freedesktop.FileManager1'>"
			"<method name='ShowFolders'><arg type='as' direction='in'/><arg type='s' direction='in'/></method>"
			"<method name='ShowItems'><arg type='as' direction='in'/><arg type='s' direction='in'/></method>"
			"<method name='ShowItemProperties'><arg type='as' direction='in'/><arg type='s' direction='in'/></method>"
			"</interface>");
	}

	bool handleMessage(const QDBusMessage &m, const QDBusConnection &c) override {
		if (m.interface() != IFACE) {
			return false;
		}
		const QStringList paths = localPaths(m.arguments().value(0).toStringList());
		const QString method = m.member();
		/* Reply first: the caller shouldn't wait on windows opening. */
		c.send(m.createReply());
		QTimer::singleShot(0, [method, paths] {
			for (const QString &path : paths) {
				if (method == "ShowFolders") {
					FolderWindow::open(QFileInfo(path).isDir() ? path : QFileInfo(path).absolutePath());
				} else if (method == "ShowItems") {
					reveal(path);
				} else if (method == "ShowItemProperties") {
					reveal(path);
					InfoWindow::open(path, iconKindFor(path), displayName(path));
				}
			}
		});
		return true;
	}
};

void fileManager1Start() {
	static FileManager1 object;
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.registerVirtualObject(PATH, &object) || !bus.registerService(SERVICE)) {
		fprintf(stderr, "zacos9-finder: can't be the file manager on D-Bus (%s): %s\n", SERVICE,
			qPrintable(bus.lastError().message()));
	}
}

bool fileManager1ShowFolders(const QStringList &uris) {
	QDBusConnection bus = QDBusConnection::sessionBus();
	if (!bus.interface() || !bus.interface()->isServiceRegistered(SERVICE)) {
		return false;
	}
	QStringList absolute;
	for (const QString &u : uris) {
		absolute << (u.contains("://") ? u : QUrl::fromLocalFile(QFileInfo(u).absoluteFilePath()).toString());
	}
	QDBusMessage call = QDBusMessage::createMethodCall(SERVICE, PATH, IFACE, "ShowFolders");
	call << absolute << QString();
	return bus.call(call, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage;
}
