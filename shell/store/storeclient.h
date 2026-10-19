#pragma once

/*
 * What the Software window needs: the shipped catalog, and running
 * zacos9-appstore-helper (as root, through pkexec, for install and
 * remove; directly for installed, which needs no privilege).
 */

#include <QProcess>
#include <QString>
#include <QStringList>
#include <vector>

/* One thing the Software window offers. Its identity is `id` (ours, not
 * a package name: an item may stand for more than one package). */
struct StoreItem {
	QString id, name, category, blurb;
	QStringList packages;
	bool featured = false;
	QString source = "debian";
	QString iconPath;
	/* An optional look-and-feel preset for it (zacos9-appstyle): the
	 * button's label to switch it on (`styleLabel`) and off (`styleReset`),
	 * and what it does. Empty `styleId` means none. */
	QString styleId, styleLabel, styleReset, styleBlurb;
	/* A ZacOS extra (source "extra"): its directory in the extras
	 * repository, which names its releases (EXTRA-vVERSION), and the
	 * program to start once it's installed, so it runs without logging
	 * out (its package also starts it at every login). */
	QString extra, extraStart;
};

/* The shipped catalog (pl_data_dir()/store/catalog.json, or
 * ZACOS9_STORE_CATALOG for tests): its categories in the order the
 * file lists them, "Featured" first, and its items. Empty on any
 * problem reading or parsing it (the window says so rather than crash). */
QStringList storeCategories();
std::vector<StoreItem> storeItems();
/* Items in `category`; "Featured" collects every item marked so, from
 * any category. */
std::vector<const StoreItem *> storeItemsIn(const QStringList &category,
	const std::vector<StoreItem> &items);

/* The helper, as root: through pkexec, or directly if we are root.
 * ZACOS9_APPSTORE_HELPER points at another one, for tests. */
void appstoreHelperCommand(QProcess *p, const QStringList &args);

/* Runs the helper to the end, keeping the windows drawn meanwhile (apt
 * can take a while, and pkexec may be asking for a password). *ok says
 * whether it worked; *err gets its last line of complaint. */
QString runAppstoreHelper(const QStringList &args, bool *ok, QString *err = nullptr);

/* zacos9-appstyle, as the user (no privilege): "apply", "reset" or
 * "status" for a preset. Keeps the windows drawn meanwhile, like
 * runAppstoreHelper; *err gets its last line of complaint. */
QString runStyleHelper(const QStringList &args, bool *ok, QString *err = nullptr);
bool styleIsOn(const QString &styleId);

/* Is every package in `packages` installed? Runs the helper's
 * "installed" command directly: no privilege is needed to ask, so it
 * is never run through pkexec. */
bool packagesInstalled(const QStringList &packages, bool *ok = nullptr, QString *err = nullptr);
QString runFlatpak(const QStringList &args, bool *ok, QString *err = nullptr);
bool flathubEnabled(bool *ok, QString *err);
bool flatpakInstalled(const QString &id, bool *ok, QString *err);
QString softwareCatalogHelper();

/* ZacOS Extras: add-ons released on GitHub
 * (chromasystemprofessional/ZacOS-Extras) as EXTRA-vVERSION, each with
 * a PACKAGE_VERSION_ARCH.deb. The list of releases, from GitHub's API;
 * ZACOS9_EXTRAS_URL replaces it (tests serve a file). */
QString extrasReleasesUrl();
struct ExtraRelease {
	QString version, debUrl, sha256; /* sha256: GitHub's digest, hex; may be empty */
	bool valid = false;
};
/* The newest release of `extra` in a GitHub releases list (which GitHub
 * gives newest first), skipping drafts and pre-releases. Its package must
 * be `package` for `arch`, served over https (or file:, if `allowFile`). */
ExtraRelease parseExtraReleases(const QByteArray &json, const QString &extra, const QString &package,
	const QString &arch, bool allowFile = false);
/* Finds the extra's newest release, downloads its .deb, checks its digest
 * and that it is the catalog's package, and installs it through the
 * helper's install-deb. Keeps the windows drawn meanwhile. */
bool installExtra(const StoreItem &item, QString *err);
bool parseSoftwareCatalog(const QByteArray &json, const QString &source,
	std::vector<StoreItem> *items, QString *error);
