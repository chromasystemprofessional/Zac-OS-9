/*
 * The Software window's catalog: reading it, filtering by category and
 * Featured, and the helper's own package-name validation (the one
 * thing standing between a catalog entry and a shell command run as
 * root, so it gets exercised directly, not just trusted).
 */
#include <QCoreApplication>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>

#include "storeclient.h"

static int fails;

static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static void writeCatalog(const QString &path, const QByteArray &json) {
	QFile f(path);
	f.open(QIODevice::WriteOnly | QIODevice::Truncate);
	f.write(json);
}

/* The real helper script, run directly (never through pkexec): its
 * "installed" command needs no privilege, and neither does rejecting a
 * malformed package name before "install"/"remove" even look at who is
 * calling. */
static QString helperScript() {
	const QString env = qEnvironmentVariable("ZACOS9_APPSTORE_HELPER");
	if (!env.isEmpty()) {
		return env;
	}
	return QCoreApplication::applicationDirPath() + "/../../appstore/zacos9-appstore-helper";
}

static int runHelper(const QStringList &args, QString *out = nullptr) {
	QProcess p;
	p.start(helperScript(), args);
	p.waitForFinished(10000);
	if (out) {
		*out = QString::fromUtf8(p.readAllStandardOutput());
	}
	return p.exitCode();
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);

	QTemporaryFile catalog;
	catalog.open();
	const QString path = catalog.fileName();
	catalog.close();
	qputenv("ZACOS9_STORE_CATALOG", path.toUtf8());

	writeCatalog(path, R"JSON({
		"version": 1,
		"categories": ["Featured", "Internet", "Games"],
		"items": [
			{ "id": "a", "name": "App A", "category": "Internet", "featured": true,
			  "blurb": "Does a thing.", "packages": ["pkg-a"] },
			{ "id": "b", "name": "App B", "category": "Internet",
			  "blurb": "Does another thing.", "packages": ["pkg-b"] },
			{ "id": "c", "name": "App C", "category": "Games", "featured": true,
			  "blurb": "A game.", "packages": ["pkg-c1", "pkg-c2"] },
			{ "id": "skip", "name": "", "category": "Games", "packages": ["x"] },
			{ "id": "skip2", "name": "No Packages", "category": "Games", "packages": [] }
		]
	})JSON");

	const QStringList categories = storeCategories();
	check(categories == QStringList({ "Featured", "Internet", "Games" }),
		"categories are read in the file's own order");

	const std::vector<StoreItem> items = storeItems();
	check(items.size() == 3, "an item with no name or no packages is left out, not shown broken");

	const auto internet = storeItemsIn({ "Internet" }, items);
	check(internet.size() == 2, "a category lists its own items");
	check(internet[0]->name == "App A" && internet[1]->name == "App B",
		"in the catalog's own order");

	const auto games = storeItemsIn({ "Games" }, items);
	check(games.size() == 1 && games[0]->name == "App C",
		"the broken entries don't leak into a real category either");
	check(games[0]->packages.size() == 2, "an item can name more than one package");

	const auto featured = storeItemsIn({ "Featured" }, items);
	check(featured.size() == 2, "Featured collects every item marked so");
	bool hasA = false, hasC = false;
	for (const StoreItem *item : featured) {
		hasA = hasA || item->name == "App A";
		hasC = hasC || item->name == "App C";
	}
	check(hasA && hasC, "from any category, not just the first");

	check(storeItemsIn({ "Nonexistent" }, items).empty(),
		"an unknown category is simply empty, not an error");

	/* A missing or unreadable catalog: the window says so, it doesn't crash. */
	qputenv("ZACOS9_STORE_CATALOG", "/nonexistent/path/catalog.json");
	check(storeCategories().isEmpty(), "a missing catalog file reads as empty categories");
	check(storeItems().empty(), "and empty items");
	qputenv("ZACOS9_STORE_CATALOG", path.toUtf8());

	/* ---- the helper's own validation, run directly, never through pkexec --- */
	QString out;
	check(runHelper({ "installed", "bash" }, &out) == 0 && out.contains("bash\tyes"),
		"\"installed\" finds a package that really is (bash, always present)");
	check(runHelper({ "installed", "no-such-package-xyz" }, &out) == 0 &&
		out.contains("no-such-package-xyz\tno"),
		"and says no for one that isn't, rather than failing");

	check(runHelper({ "install" }) != 0, "install with no package named is refused");
	check(runHelper({ "install", "--reinstall" }) != 0,
		"a package name starting with '-' is refused (it would be read as an apt option)");
	check(runHelper({ "install", "pkg; rm -rf /" }) != 0,
		"a package name with shell metacharacters is refused");
	check(runHelper({ "install", "pkg a" }) != 0, "a package name with a space is refused");
	check(runHelper({ "remove", "-y" }) != 0, "the same validation guards remove, not just install");
	/* A validly-named package that doesn't exist: validation lets it
	 * through, and it fails for apt's own reason (no such package, or,
	 * running as root, no privilege check to even get that far) rather
	 * than ours. Picked never to collide with a real package, so this
	 * can't accidentally install or remove anything real even if this
	 * test is ever run as root. */
	check(runHelper({ "install", "zzz-not-a-real-debian-package-zzz" }) != 0,
		"a validly-named package reaches apt, which is where it then fails");

	QTextStream(stdout) << (fails ? QString("%1 failed\n").arg(fails) : QStringLiteral("all passed\n"));
	return fails ? 1 : 0;
}
