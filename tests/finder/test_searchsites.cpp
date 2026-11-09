/* Internet Search Sites (shell/files/searchsites.cpp): the three site
 * formats, the system and user folders, and one user site replacing a
 * system site of the same name. */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>
#include <cstdio>

#include "searchsites.h"

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static bool write(const QString &path, const QByteArray &data) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir root;
	if (!root.isValid()) {
		return 1;
	}
	const QString system = root.filePath("system/Internet Search Sites");
	const QString user = root.filePath("config/zacos9/Internet Search Sites");
	if (!QDir().mkpath(system) || !QDir().mkpath(user)) {
		return 1;
	}
	qputenv("ZACOS9_DATA", root.filePath("system").toUtf8());
	qputenv("XDG_CONFIG_HOME", root.filePath("config").toUtf8());
	check(write(system + "/DuckDuckGo.searchsite",
		"[Search Site]\nName=DuckDuckGo\nURL=https://duckduckgo.com/?q={query}\n"),
		"a .searchsite file is written");
	check(write(system + "/OpenSearch.xml",
		"<?xml version=\"1.0\"?><OpenSearchDescription>"
		"<ShortName>Wiki</ShortName><Description>Wiki search</Description>"
		"<Url type=\"text/html\" template=\"https://wiki.example/?q={searchTerms}\"/>"
		"<Channel>Reference</Channel></OpenSearchDescription>"),
		"an OpenSearch file is written");
	check(write(system + "/Classic.src",
		"<name>Classic</name><search method=\"get\" "
		"action=\"http://classic.example/find?q=%s\"/>"),
		"a classic .src file is written");
	check(write(system + "/Bad.searchsite",
		"[Search Site]\nName=Bad\nURL=javascript:alert(1)\n"),
		"a bad site is written");
	check(write(user + "/DuckDuckGo.searchsite",
		"[Search Site]\nName=DuckDuckGo\nURL=https://user.example/?q={query}\n"),
		"the user's DuckDuckGo site is written");

	QString error;
	const QList<SearchSite> sites = searchSites(&error);
	QSet<QString> names;
	for (const SearchSite &site : sites) {
		names << site.name;
	}
	check(names == QSet<QString>({ "Classic", "DuckDuckGo", "Wiki" }),
		"the system and user sites are read, and the bad one is skipped");
	check(!error.isEmpty(), "a bad site is named");
	const SearchSite *duck = searchSiteNamed(sites, "duckduckgo");
	check(duck && duck->user && duck->url.startsWith("https://user.example/"),
		"the user's site replaces the system's same-named site");
	const SearchSite *wiki = searchSiteNamed(sites, "Wiki");
	check(wiki && wiki->channel == "Reference" &&
		wiki->url == "https://wiki.example/?q={query}",
		"OpenSearch's ShortName, template and channel are read");
	const SearchSite *classic = searchSiteNamed(sites, "Classic");
	check(classic && classic->url == "http://classic.example/find?q={query}",
		"a classic Sherlock action is read and its %s becomes {query}");
	check(searchSiteUrl(*duck, "two words") == "https://user.example/?q=two%20words",
		"the words are URL-encoded into the site's template");

	SearchSite bad;
	error.clear();
	check(!readSearchSiteFile(system + "/Bad.searchsite", &bad, &error) &&
		error.contains("http"), "a non-http search URL is refused");

	const QStringList channels = webSearchChannels();
	check(channels.contains("Wiki") && channels.contains("DuckDuckGo"),
		"the menu list comes from the same reader");
	if (fails) {
		return 1;
	}
	printf("ok: Internet Search Sites are read from both folders\n");
	return 0;
}
