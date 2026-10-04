#pragma once

/*
 * What Software Update needs to know, kept apart from its window so it
 * can be tested: ZacOS 9's newest release on GitHub, the Debian updates
 * apt would install, and Debian version order.
 */

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <vector>

/* The GitHub repository releases come from ("owner/name");
 * ZACOS9_UPDATE_REPO overrides it, for tests and forks. */
QString updateRepo();
/* https://api.github.com/repos/<repo>/releases/latest; ZACOS9_UPDATE_URL
 * overrides the whole URL (tests serve a file). */
QString updateReleaseUrl();

struct Release {
	QString version;   /* the tag without a leading "v": "0.1.0" */
	QString name;      /* the release's title, or the tag */
	QString notes;     /* its description (Markdown, as written) */
	QString debUrl;    /* the zacos9 .deb for this machine's architecture */
	QString debName;
	QString sha256;    /* the asset's digest from GitHub, hex; empty if none given */
	bool valid = false;
};

/* A "releases/latest" reply. `arch` is dpkg's ("amd64"). Not valid if
 * the JSON is wrong, or there is no zacos9_*_<arch>.deb among its assets. */
Release parseRelease(const QByteArray &json, const QString &arch);

struct DebianUpdate {
	QString package, from, to;
};

/* `apt-get -s upgrade` output: its "Inst" lines (new packages it would
 * add have no "from"). */
std::vector<DebianUpdate> parseAptSimulation(const QString &out);

/* Debian version order, by dpkg itself: true if a is newer than b. */
bool versionNewer(const QString &a, const QString &b);
/* The installed version of a package, or empty. */
QString installedVersion(const QString &package);
/* dpkg's architecture name ("amd64"). */
QString debArchitecture();
