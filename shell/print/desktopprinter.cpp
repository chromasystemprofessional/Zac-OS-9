#include "desktopprinter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <vector>

namespace {
QString g_default;
QElapsedTimer g_read;
bool g_reading = false;
std::vector<std::pair<QPointer<QObject>, std::function<void()>>> g_watchers;
}

QString desktopDefaultPrinter() {
	if (!g_reading && (!g_read.isValid() || g_read.elapsed() > 10000)) {
		g_reading = true;
		g_read.start();
		auto *p = new QProcess;
		QObject::connect(p, &QProcess::finished, [p] {
			const QRegularExpressionMatch m = QRegularExpression("destination: (\\S+)")
				.match(QString::fromUtf8(p->readAllStandardOutput()));
			const QString now = m.hasMatch() ? m.captured(1) : QString();
			p->deleteLater();
			g_reading = false;
			if (now != g_default) {
				g_default = now;
				for (auto &[context, changed] : g_watchers) {
					if (context) {
						changed();
					}
				}
			}
		});
		QObject::connect(p, &QProcess::errorOccurred, [p](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				g_reading = false;
				p->deleteLater();
			}
		});
		p->start("lpstat", { "-d" });
	}
	return g_default;
}

void watchDefaultPrinter(QObject *context, std::function<void()> changed) {
	g_watchers.emplace_back(context, std::move(changed));
}

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
