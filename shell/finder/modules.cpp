#include "modules.h"
#include "process.h"
#include "programs.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QThread>
#include <unistd.h>

#include "extensioncatalog.h"

namespace {

/* zacos9-modules-helper, as root: whether it worked, and why not. */
bool modulesHelper(const QStringList &args, QString *error) {
	bool ok = false;
	runHelper("modules", "modules", "modules", args, &ok, error);
	return ok;
}

} // namespace

QStringList extensionModules(const QString &key) {
	return extensionEntry(key).modules;
}

QString runModulesHelper(const QStringList &args, bool *ok, QString *err) {
	QString error;
	const bool success = modulesHelper(args, &error);
	if (ok) *ok = success;
	if (err) *err = error;
	return success ? QString() : error;
}

void setExtensionEnabledAsync(QObject *receiver, const QString &key, bool on,
		const QStringList &modules,
		std::function<void(bool, const QString &)> done) {
	QPointer<QObject> guard(receiver);
	auto *thread = QThread::create([guard, key, on, modules, done = std::move(done)]() mutable {
		QStringList args;
		args << "extension" << key << (on ? "on" : "off");
		for (const QString &module : modules) {
			args << module;
		}
		QString error;
		const bool success = modulesHelper(args, &error);
		/* To the GUI thread through the application, which outlives this
		 * thread; whether the receiver still exists is asked only there,
		 * where it would be destroyed. */
		QMetaObject::invokeMethod(qApp,
			[guard, done = std::move(done), success, error]() mutable {
				if (guard) {
					done(success, error);
				}
			}, Qt::QueuedConnection);
	});
	QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
	thread->start();
}
