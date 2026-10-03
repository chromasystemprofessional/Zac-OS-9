#include "storeclient.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <functional>
#include <unistd.h>

#include "settings.h"

static QString catalogFile() {
	const QString env = qEnvironmentVariable("ZACOS9_STORE_CATALOG");
	if (!env.isEmpty()) {
		return env;
	}
	return QString::fromUtf8(pl_data_dir()) + "/store/catalog.json";
}

static QJsonObject readCatalog() {
	QFile f(catalogFile());
	if (!f.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QJsonDocument::fromJson(f.readAll()).object();
}

QStringList storeCategories() {
	QStringList out;
	for (const QJsonValue &v : readCatalog().value("categories").toArray()) {
		out << v.toString();
	}
	return out;
}

std::vector<StoreItem> storeItems() {
	std::vector<StoreItem> out;
	for (const QJsonValue &v : readCatalog().value("items").toArray()) {
		const QJsonObject o = v.toObject();
		StoreItem item;
		item.id = o.value("id").toString();
		item.name = o.value("name").toString();
		item.category = o.value("category").toString();
		item.blurb = o.value("blurb").toString();
		item.featured = o.value("featured").toBool(false);
		for (const QJsonValue &p : o.value("packages").toArray()) {
			item.packages << p.toString();
		}
		const QJsonObject style = o.value("style").toObject();
		item.styleId = style.value("id").toString();
		item.styleLabel = style.value("label").toString();
		item.styleReset = style.value("reset").toString();
		item.styleBlurb = style.value("blurb").toString();
		if (item.styleLabel.isEmpty() || item.styleReset.isEmpty()) {
			item.styleId.clear(); /* an incomplete preset is no preset */
		}
		if (!item.id.isEmpty() && !item.name.isEmpty() && !item.packages.isEmpty()) {
			out.push_back(std::move(item));
		}
	}
	return out;
}

std::vector<const StoreItem *> storeItemsIn(const QStringList &category,
		const std::vector<StoreItem> &items) {
	std::vector<const StoreItem *> out;
	const bool featured = !category.isEmpty() && category.first() == "Featured";
	for (const StoreItem &item : items) {
		if (featured ? item.featured : !category.isEmpty() && item.category == category.first()) {
			out.push_back(&item);
		}
	}
	return out;
}

static QString helperPath() {
	const QString env = qEnvironmentVariable("ZACOS9_APPSTORE_HELPER");
	if (!env.isEmpty()) {
		return env;
	}
	/* Installed beside us (prefix/libexec/zacos9), or, running from a
	 * build tree (build/shell/...), the one in the sources. */
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/zacos9-appstore-helper",
			dir + "/../../appstore/zacos9-appstore-helper" }) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/libexec/zacos9/zacos9-appstore-helper";
}

void appstoreHelperCommand(QProcess *p, const QStringList &args) {
	if (geteuid() == 0) {
		p->start(helperPath(), args);
	} else {
		p->start("pkexec", QStringList{ helperPath() } + args);
	}
}

/* Starts a process through `start`, waits for it keeping the windows
 * drawn (never QProcess::waitForFinished: see CLAUDE.md), and reports
 * as runAppstoreHelper does. */
static QString runWatched(const std::function<void(QProcess *)> &start, bool *ok, QString *err) {
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	start(&p);
	p.closeWriteChannel();
	while (!done) {
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents,
			100);
	}
	const bool started = p.error() != QProcess::FailedToStart;
	*ok = started && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
	if (err) {
		*err = QString::fromUtf8(p.readAllStandardError()).trimmed().section('\n', -1);
		if (!*ok && err->isEmpty()) {
			*err = started && p.exitStatus() == QProcess::NormalExit
				? QString("The software helper stopped with status %1.").arg(p.exitCode())
				: QString("The software helper couldn’t be run (%1).").arg(p.errorString());
		}
	}
	return QString::fromUtf8(p.readAllStandardOutput());
}

QString runAppstoreHelper(const QStringList &args, bool *ok, QString *err) {
	return runWatched([&](QProcess *p) { appstoreHelperCommand(p, args); }, ok, err);
}

static QString styleHelperPath() {
	const QString env = qEnvironmentVariable("ZACOS9_APPSTYLE_HELPER");
	if (!env.isEmpty()) {
		return env;
	}
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/zacos9-appstyle",
			dir + "/../../appstore/zacos9-appstyle" }) {
		if (QFileInfo(candidate).isExecutable()) {
			return QFileInfo(candidate).canonicalFilePath();
		}
	}
	return "/usr/libexec/zacos9/zacos9-appstyle";
}

QString runStyleHelper(const QStringList &args, bool *ok, QString *err) {
	return runWatched([&](QProcess *p) { p->start(styleHelperPath(), args); }, ok, err);
}

bool styleIsOn(const QString &styleId) {
	bool ok = false;
	const QString out = runStyleHelper({ "status", styleId }, &ok);
	return ok && out.trimmed() == "on";
}

bool packagesInstalled(const QStringList &packages) {
	if (packages.isEmpty()) {
		return false;
	}
	QProcess p;
	p.start(helperPath(), QStringList{ "installed" } + packages);
	p.closeWriteChannel();
	if (!p.waitForFinished(5000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
		return false;
	}
	const QString out = QString::fromUtf8(p.readAllStandardOutput());
	int found = 0;
	for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
		if (line.endsWith("\tyes")) {
			found++;
		}
	}
	return found == packages.size();
}
