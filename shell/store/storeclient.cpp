#include "storeclient.h"
#include "programs.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTemporaryDir>
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
		const QJsonObject extra = o.value("extra").toObject();
		if (!extra.isEmpty()) {
			item.source = "extra";
			item.extra = extra.value("id").toString();
			item.extraStart = extra.value("start").toString();
			/* Both become parts of URLs, tags and a command line. */
			static const QRegularExpression name("\\A[a-z0-9][a-z0-9.-]*\\z");
			if (!name.match(item.extra).hasMatch() || item.packages.size() != 1 ||
					(!item.extraStart.isEmpty() && !name.match(item.extraStart).hasMatch())) {
				continue;
			}
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
	 * build tree, the one in the sources. */
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &candidate : { dir + "/../libexec/zacos9/zacos9-appstore-helper",
			sourceTreePath("appstore/zacos9-appstore-helper") }) {
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
			sourceTreePath("appstore/zacos9-appstyle") }) {
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

bool packagesInstalled(const QStringList &packages, bool *success, QString *err) {
	if (packages.isEmpty()) {
		if (success) {
			*success = false;
		}
		if (err) {
			*err = "No package was selected.";
		}
		qWarning() << "No package was selected.";
		return false;
	}
	bool ok = false;
	QString error;
	const QString out = runWatched([&](QProcess *p) {
		p->start(helperPath(), QStringList{ "installed" } + packages);
	}, &ok, &error);
	if (success) {
		*success = ok;
	}
	if (err) {
		*err = error;
	}
	if (!ok) {
		qWarning().noquote() << error;
		return false;
	}
	int found = 0;
	for (const QString &line : out.split('\n', Qt::SkipEmptyParts)) {
		if (line.endsWith("\tyes")) {
			found++;
		}
	}
	return found == packages.size();
}

QString runFlatpak(const QStringList &args, bool *ok, QString *err) {
	return runWatched([&](QProcess *p) { p->start("flatpak", QStringList{ "--user" } + args); }, ok, err);
}

bool flathubEnabled(bool *ok, QString *err) {
	const QString output = runFlatpak({ "remote-list", "--show-disabled", "--columns=name,url,options" }, ok, err);
	for (const QString &line : output.split('\n', Qt::SkipEmptyParts)) {
		const QStringList fields = line.split('\t');
		if (fields.first() == "flathub") {
			if ((fields.size() != 2 && fields.size() != 3) || (fields[1] != "https://dl.flathub.org/repo/" &&
					fields[1] != "https://dl.flathub.org/repo")) {
				*ok = false;
				*err = "The existing flathub remote is not the official Flathub source.";
				return false;
			}
			return !fields.value(2).split(',').contains("disabled");
		}
	}
	return false;
}

bool flatpakInstalled(const QString &id, bool *ok, QString *err) {
	const QString output = runFlatpak({ "list", "--app", "--columns=application" }, ok, err);
	return *ok && output.split('\n', Qt::SkipEmptyParts).contains(id);
}

QString softwareCatalogHelper() {
	const QString override = qEnvironmentVariable("ZACOS9_SOFTWARE_CATALOG_HELPER");
	if (!override.isEmpty()) {
		return override;
	}
	const QString dir = QCoreApplication::applicationDirPath();
	for (const QString &path : { dir + "/../libexec/zacos9/zacos9-software-catalog",
			sourceTreePath("appstore/zacos9-software-catalog") }) {
		if (QFileInfo(path).isExecutable()) {
			return path;
		}
	}
	return "/usr/libexec/zacos9/zacos9-software-catalog";
}

bool parseSoftwareCatalog(const QByteArray &json, const QString &source,
		std::vector<StoreItem> *items, QString *error) {
	QJsonParseError parseError;
	const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
	if (!doc.isObject() || !doc.object().value("items").isArray()) {
		*error = "Invalid software catalog: " + parseError.errorString();
		return false;
	}
	const QRegularExpression package("\\A[a-z0-9][a-z0-9+.-]+\\z");
	const QRegularExpression appId("\\A[A-Za-z0-9_-]+(?:\\.[A-Za-z0-9_-]+){2,}\\z");
	std::vector<StoreItem> next;
	for (const QJsonValue &value : doc.object().value("items").toArray()) {
		const QJsonObject obj = value.toObject();
		StoreItem item;
		item.source = source;
		item.id = obj.value("id").toString();
		item.name = obj.value("name").toString();
		item.blurb = obj.value("blurb").toString();
		item.category = obj.value("category").toString();
		item.iconPath = obj.value("icon").toString();
		const QStringList categories = { "Internet", "Graphics", "Productivity",
			"Multimedia", "Games", "Utilities" };
		if (!item.category.isEmpty() && !categories.contains(item.category)) {
			*error = "Invalid category in software catalog.";
			return false;
		}
		if (!item.iconPath.isEmpty() && !QFileInfo(item.iconPath).isAbsolute()) {
			*error = "Invalid icon path in software catalog.";
			return false;
		}
		for (const QJsonValue &p : obj.value("packages").toArray()) {
			const QString name = p.toString();
			if (!(source == "flathub" ? appId : package).match(name).hasMatch()) {
				*error = "Invalid application identifier in software catalog.";
				return false;
			}
			item.packages << name;
		}
		if (item.id.isEmpty() || item.name.isEmpty() || item.packages.isEmpty() ||
				obj.value("source").toString() != source ||
				(source == "flathub" && item.packages.size() != 1)) {
			*error = "Incomplete application in software catalog.";
			return false;
		}
		next.push_back(std::move(item));
	}
	*items = std::move(next);
	return true;
}

QString extrasReleasesUrl() {
	const QString env = qEnvironmentVariable("ZACOS9_EXTRAS_URL");
	return env.isEmpty() ? QStringLiteral("https://api.github.com/repos/chromasystemprofessional/ZacOS-Extras/releases")
		: env;
}

ExtraRelease parseExtraReleases(const QByteArray &json, const QString &extra, const QString &package,
		const QString &arch, bool allowFile) {
	static const QRegularExpression versionRe("^[0-9][A-Za-z0-9.+~]*$");
	const QString prefix = extra + "-v";
	for (const QJsonValue &v : QJsonDocument::fromJson(json).array()) {
		const QJsonObject o = v.toObject();
		const QString tag = o.value("tag_name").toString();
		const QString version = tag.mid(prefix.size());
		if (!tag.startsWith(prefix) || !versionRe.match(version).hasMatch() ||
				o.value("draft").toBool() || o.value("prerelease").toBool()) {
			continue;
		}
		const QString wanted = package + "_" + version + "_" + arch + ".deb";
		for (const QJsonValue &a : o.value("assets").toArray()) {
			const QJsonObject asset = a.toObject();
			const QString url = asset.value("browser_download_url").toString();
			if (asset.value("name").toString() != wanted ||
					!(url.startsWith("https://") || (allowFile && url.startsWith("file:")))) {
				continue;
			}
			ExtraRelease r;
			r.version = version;
			r.debUrl = url;
			const QString digest = asset.value("digest").toString();
			if (digest.startsWith("sha256:")) {
				r.sha256 = digest.mid(7).toLower();
			}
			r.valid = true;
			return r;
		}
	}
	return {};
}

/* One GET, run to the end while the windows stay drawn. */
static QByteArray fetch(QNetworkAccessManager *net, const QString &url, QString *err) {
	QNetworkRequest req{ QUrl(url) };
	req.setRawHeader("Accept", "application/vnd.github+json");
	req.setTransferTimeout(60000);
	QNetworkReply *reply = net->get(req);
	bool done = false;
	QObject::connect(reply, &QNetworkReply::finished, [&done] { done = true; });
	while (!done) {
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
	}
	QByteArray data;
	if (reply->error() == QNetworkReply::NoError) {
		data = reply->readAll();
	} else {
		*err = "ZacOS Extras couldn’t be reached: " + reply->errorString();
	}
	reply->deleteLater();
	return data;
}

bool installExtra(const StoreItem &item, QString *err) {
	if (item.source != "extra" || item.packages.size() != 1) {
		*err = "That isn’t a ZacOS extra.";
		return false;
	}
	const QString package = item.packages.first();
	const bool testing = !qEnvironmentVariableIsEmpty("ZACOS9_EXTRAS_URL");
	QNetworkAccessManager net;
	const QByteArray list = fetch(&net, extrasReleasesUrl(), err);
	if (list.isEmpty()) {
		return false;
	}
	bool ok = false;
	const QString arch = runWatched([](QProcess *p) { p->start("dpkg", { "--print-architecture" }); }, &ok, err)
		.trimmed();
	if (!ok) {
		return false;
	}
	const ExtraRelease release = parseExtraReleases(list, item.extra, package, arch, testing);
	if (!release.valid) {
		*err = item.name + " hasn’t been released for this computer yet.";
		return false;
	}
	const QByteArray deb = fetch(&net, release.debUrl, err);
	if (deb.isEmpty()) {
		return false;
	}
	if (!release.sha256.isEmpty() &&
			QCryptographicHash::hash(deb, QCryptographicHash::Sha256).toHex() != release.sha256.toLatin1()) {
		*err = "The download of " + item.name + " was damaged. Try again.";
		return false;
	}
	QTemporaryDir dir;
	const QString path = dir.filePath(package + ".deb");
	QFile file(path);
	if (!dir.isValid() || !file.open(QIODevice::WriteOnly) || file.write(deb) != deb.size()) {
		*err = "The download of " + item.name + " couldn’t be saved.";
		return false;
	}
	file.close();
	/* The catalog says which package this is; the download must agree. */
	const QString fields = runWatched([&](QProcess *p) { p->start("dpkg-deb", { "-f", path, "Package" }); },
		&ok, err);
	if (!ok || fields.trimmed() != package) {
		*err = "The download of " + item.name + " isn’t the package it should be.";
		return false;
	}
	runAppstoreHelper({ "install-deb", path }, &ok, err);
	return ok;
}
