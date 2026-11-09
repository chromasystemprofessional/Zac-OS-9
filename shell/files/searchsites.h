#pragma once

/*
 * Internet Search Sites: the search sites Bloodhound's Web channels and the
 * Sniffer's File > Search Internet both use. A site is a file, never code.
 *
 * Read, in this order:
 *   - OpenSearch XML (.xml): ShortName and a text/html Url template.
 *   - a ZacOS search site (.searchsite), key=value:
 *       [Search Site]
 *       Name=Wikipedia
 *       URL=https://en.wikipedia.org/w/index.php?search={query}
 *       Channel=Reference
 *       Icon=system-search
 *   - a classic Sherlock .src (best effort): its search action.
 *
 * The system's sites are read-only in the System Folder's Internet Search
 * Sites; the user's own are in ~/.config/zacos9/Internet Search Sites.
 * A user's site with the same name replaces the system's one.
 */

#include <QList>
#include <QString>

struct SearchSite {
	QString name;
	QString url; /* has {query} where the words go */
	QString channel; /* optional: groups sites into a channel */
	QString icon; /* optional icon name */
	QString path; /* the site file it came from */
	bool user = false;
};

/* The folders, for callers and tests. */
QString systemSearchSitesFolder();
QString userSearchSitesFolder();

/* Every site, the system's then the user's; a user's same-named site
 * replaces the system's. Bad files are skipped and named in `error`. */
QList<SearchSite> searchSites(QString *error = nullptr);
/* The site names in the order the menus show them. */
QStringList webSearchChannels();
/* The first site with that name (case-insensitive), or nullptr. */
const SearchSite *searchSiteNamed(const QList<SearchSite> &sites, const QString &name);

/* One site file (for tests). */
bool readSearchSiteFile(const QString &path, SearchSite *site, QString *error = nullptr);
/* The site's URL for `words`, with the query URL-encoded. */
QString searchSiteUrl(const SearchSite &site, const QString &words);
/* Search the web with the named site, in the default browser. */
bool openWebSearch(const QString &site, const QString &words, QString *error = nullptr);
bool openSearchSite(const SearchSite &site, const QString &words, QString *error = nullptr);
