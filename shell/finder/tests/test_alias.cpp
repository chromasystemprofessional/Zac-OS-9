/* Persistent aliases: rename, move, missing, replaced, loops, legacy links. */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>

#include "alias.h"

static int failures = 0;

static void check(bool ok, const char *what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

static void touch(const QString &path, const char *text = "data") {
	QFile f(path);
	f.open(QIODevice::WriteOnly);
	f.write(text);
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir tmp;
	const QString r = tmp.path();
	QDir().mkpath(r + "/docs/sub");
	QDir().mkpath(r + "/aliases");
	touch(r + "/docs/report.txt");
	QFile(r + "/docs/report.txt").setPermissions(QFile::ReadOwner | QFile::WriteOwner);
	const QString alias = r + "/aliases/report alias";
	QFile::link(r + "/docs/report.txt", alias);
	check(aliasRecord(alias, r + "/docs/report.txt"), "record identity");
	check(aliasResolve(alias).state == AliasState::Ok, "intact alias resolves");

	/* A physical alias is opened through its resolved target, not as a link
	 * or as the empty folder its broken path would otherwise create. */
	QDir().mkpath(r + "/physical folder");
	const QString folderAlias = r + "/aliases/folder alias";
	QFile::link(r + "/physical folder", folderAlias);
	check(aliasRecord(folderAlias, r + "/physical folder") &&
		QFileInfo(aliasResolve(folderAlias).target).isDir(),
		"physical folder aliases resolve and classify as the target folder");
	touch(r + "/docs/example.desktop", "[Desktop Entry]\nType=Application\n");
	const QString applicationAlias = r + "/aliases/application alias";
	QFile::link(r + "/docs/example.desktop", applicationAlias);
	check(aliasRecord(applicationAlias, r + "/docs/example.desktop") &&
		aliasResolve(applicationAlias).target.endsWith(".desktop"),
		"physical application aliases resolve to the desktop entry for application launch");

	QFile::rename(r + "/docs/report.txt", r + "/docs/renamed.txt");
	AliasResolution res = aliasResolve(alias);
	check(res.state == AliasState::Reconnected && res.target == r + "/docs/renamed.txt",
		"renamed target is found by inode");
	check(QFileInfo(alias).symLinkTarget() == r + "/docs/renamed.txt", "link is repaired");
	check((QFileInfo(r + "/docs/renamed.txt").permissions() & QFile::WriteOwner) &&
		!(QFileInfo(r + "/docs/renamed.txt").permissions() & QFile::WriteOther),
		"target permissions are unchanged");

	QFile::rename(r + "/docs/renamed.txt", r + "/docs/sub/moved.txt");
	res = aliasResolve(alias);
	check(res.state == AliasState::Reconnected && res.target == r + "/docs/sub/moved.txt",
		"moved target is found nearby");

	/* An alias moved with its record keeps reconnecting. */
	QFile::rename(alias, r + "/aliases/other");
	aliasMoveRecord(alias, r + "/aliases/other", false);
	QFile::rename(r + "/docs/sub/moved.txt", r + "/docs/again.txt");
	check(aliasResolve(r + "/aliases/other").state == AliasState::Reconnected,
		"record follows a moved alias");

	/* A second alias remembers the original identity through its first alias. */
	QFile::link(r + "/aliases/other", r + "/aliases/chain");
	check(aliasRecord(r + "/aliases/chain", r + "/aliases/other"),
		"chained alias records its ultimate target identity");
	QFile::rename(r + "/docs/again.txt", r + "/docs/chained-move.txt");
	res = aliasResolve(r + "/aliases/chain");
	check(res.state == AliasState::Reconnected && res.target == r + "/docs/chained-move.txt",
		"alias chains reconnect by the underlying target identity");
	QDir().mkpath(r + "/elsewhere/a/b/c");
	QFile::rename(r + "/docs/chained-move.txt", r + "/elsewhere/first-move.txt");
	res = aliasResolve(r + "/aliases/chain");
	check(res.state == AliasState::Reconnected && res.target == r + "/elsewhere/first-move.txt",
		"reconnection updates the search origin for later moves");
	QFile::rename(r + "/elsewhere/first-move.txt", r + "/elsewhere/a/b/c/deep-move.txt");
	res = aliasResolve(r + "/aliases/chain");
	check(res.state == AliasState::Reconnected &&
		res.target == r + "/elsewhere/a/b/c/deep-move.txt",
		"updated target identity finds subsequent moves within the bounded search");
	QFile::rename(r + "/elsewhere/a/b/c/deep-move.txt", r + "/docs/again.txt");

	/* Deleted: broken, never recreated. */
	QFile::remove(r + "/docs/again.txt");
	check(aliasResolve(r + "/aliases/other").state == AliasState::Missing, "deleted target is Missing");
	check(!QFileInfo::exists(r + "/docs/again.txt"), "missing target is not recreated");

	/* A new file at the old path is a different item. */
	touch(r + "/docs/again.txt", "new");
	res = aliasResolve(r + "/aliases/other");
	check(res.state == AliasState::Changed, "replaced target needs confirmation");
	check(QFileInfo(r + "/aliases/other").symLinkTarget() == r + "/docs/again.txt", "link not rewritten silently");
	check(aliasReconnect(r + "/aliases/other", r + "/docs/again.txt") &&
		aliasResolve(r + "/aliases/other").state == AliasState::Ok, "user confirms the new item");

	/* User-assisted reconnect of a lost alias. */
	QFile::link(r + "/docs/gone", r + "/aliases/lost");
	check(aliasResolve(r + "/aliases/lost").state == AliasState::Missing, "legacy dangling link is Missing");
	QString error;
	check(!aliasReconnect(r + "/aliases/lost", r + "/docs/nothing", &error) && !error.isEmpty(),
		"reconnect to a missing item is refused");
	check(aliasReconnect(r + "/aliases/lost", r + "/docs/sub") &&
		aliasResolve(r + "/aliases/lost").state == AliasState::Ok, "user chooses a new original");

	/* Legacy symlinks keep working and gain a record. */
	QFile::link(r + "/docs/again.txt", r + "/aliases/legacy");
	check(aliasResolve(r + "/aliases/legacy").state == AliasState::Ok &&
		QFileInfo(r + "/aliases/.alias/legacy").isFile(), "plain symlink works and is adopted");
	check(aliasResolve(r + "/docs/again.txt").state == AliasState::NotAlias, "regular file is not an alias");

	/* Loops. */
	QFile::link(r + "/aliases/b", r + "/aliases/a");
	QFile::link(r + "/aliases/a", r + "/aliases/b");
	check(aliasResolve(r + "/aliases/a").state == AliasState::Loop, "two-link loop detected");
	QFile::link(r + "/aliases/self", r + "/aliases/self");
	check(aliasResolve(r + "/aliases/self").state == AliasState::Loop, "self link detected");
	check(!aliasReconnect(r + "/aliases/lost", r + "/aliases/lost"), "alias cannot point to itself");
	check(!aliasReconnect(r + "/aliases/lost", r + "/aliases/a"), "alias cannot point into a loop");
	check(!aliasMoveRecord(r + "/aliases/other", "/proc/zacos9-alias-test", true),
		"record preservation failure is reported to the caller");

	return failures ? 1 : 0;
}
