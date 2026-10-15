#include "desktopprinter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

QString desktopPrinterQueue(const QString &path) {
	if (QFileInfo(path).suffix() != DESKTOP_PRINTER_SUFFIX) {
		return QString();
	}
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return QString();
	}
	const QStringList lines = QString::fromUtf8(f.read(4096)).split('\n');
	if (lines.value(0).trimmed() != "[Desktop Printer]") {
		return QString();
	}
	for (const QString &line : lines) {
		if (line.startsWith("Queue=")) {
			return line.mid(6).trimmed();
		}
	}
	return QString();
}

QString createDesktopPrinter(const QString &folder, const QString &name, const QString &queue, QString *error) {
	// "/" can't be in a file name; the rest of a printer's name can.
	QString base = QString(name).replace('/', '-').trimmed();
	if (base.isEmpty()) {
		base = queue;
	}
	QString path = folder + "/" + base + "." + DESKTOP_PRINTER_SUFFIX;
	if (QFileInfo::exists(path)) {
		if (desktopPrinterQueue(path) == queue) {
			return path; /* there already */
		}
		for (int n = 2; QFileInfo::exists(path); ++n) {
			path = QString("%1/%2 %3.%4").arg(folder, base).arg(n).arg(DESKTOP_PRINTER_SUFFIX);
		}
	}
	QDir().mkpath(folder);
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(("[Desktop Printer]\nQueue=" + queue + "\n").toUtf8()) < 0 ||
			!f.commit()) {
		*error = f.errorString();
		return QString();
	}
	return path;
}
