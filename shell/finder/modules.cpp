#include "modules.h"
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

QString helperPath() {
	const QString env = qEnvironmentVariable("ZACOS9_MODULES_HELPER");
	if (!env.isEmpty()) {
		return env;
	}
	const QString name = "zacos9-modules-helper";
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : {
			dir + "/../libexec/zacos9/" + name,
			sourceTreePath("modules/" + name),
		}) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/libexec/zacos9/" + name;
}

static bool runHelper(const QStringList &args, QString *error) {
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError err) {
		if (err == QProcess::FailedToStart) {
			done = true;
		}
	});
	if (geteuid() == 0) {
		p.start(helperPath(), args);
	} else {
		p.start("pkexec", QStringList{ helperPath() } + args);
	}
	while (!done) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
			QEventLoop::WaitForMoreEvents, 100);
	}
	if (error) {
		*error = QString::fromUtf8(p.readAllStandardError()).trimmed().section('\n', -1);
	}
	const bool started = p.error() != QProcess::FailedToStart;
	return started && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

} // namespace

QStringList extensionModules(const QString &key) {
	return extensionEntry(key).modules;
}

QString runModulesHelper(const QStringList &args, bool *ok, QString *err) {
	QString error;
	const bool success = runHelper(args, &error);
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
		const bool success = runHelper(args, &error);
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
