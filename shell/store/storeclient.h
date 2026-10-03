#pragma once

/*
 * What the Software window needs: the shipped catalog, and running
 * platinum-appstore-helper (as root, through pkexec, for install and
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
};

/* The shipped catalog (pl_data_dir()/store/catalog.json, or
 * PLATINUM_STORE_CATALOG for tests): its categories in the order the
 * file lists them, "Featured" first, and its items. Empty on any
 * problem reading or parsing it (the window says so rather than crash). */
QStringList storeCategories();
std::vector<StoreItem> storeItems();
/* Items in `category`; "Featured" collects every item marked so, from
 * any category. */
std::vector<const StoreItem *> storeItemsIn(const QStringList &category,
	const std::vector<StoreItem> &items);

/* The helper, as root: through pkexec, or directly if we are root.
 * PLATINUM_APPSTORE_HELPER points at another one, for tests. */
void appstoreHelperCommand(QProcess *p, const QStringList &args);

/* Runs the helper to the end, keeping the windows drawn meanwhile (apt
 * can take a while, and pkexec may be asking for a password). *ok says
 * whether it worked; *err gets its last line of complaint. */
QString runAppstoreHelper(const QStringList &args, bool *ok, QString *err = nullptr);

/* Is every package in `packages` installed? Runs the helper's
 * "installed" command directly: no privilege is needed to ask, so it
 * is never run through pkexec. */
bool packagesInstalled(const QStringList &packages);
