/*
 * Software Update's checks: a GitHub "releases/latest" reply (which .deb
 * it picks, which tags it refuses), apt's upgrade simulation, and Debian
 * version order through dpkg itself. No network; nothing is installed.
 */
#include <QCoreApplication>
#include <QTextStream>

#include "updatecheck.h"

static int fails;

static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static const char RELEASE[] = R"({
  "tag_name": "v0.2.0",
  "name": "ZacOS 9 0.2.0",
  "body": "Global menus.",
  "assets": [
    { "name": "zacos9-dbgsym_0.2.0_amd64.deb",
      "browser_download_url": "https://github.com/o/r/releases/download/v0.2.0/zacos9-dbgsym_0.2.0_amd64.deb" },
    { "name": "zacos9_0.2.0_arm64.deb",
      "browser_download_url": "https://github.com/o/r/releases/download/v0.2.0/zacos9_0.2.0_arm64.deb" },
    { "name": "zacos9_0.2.0_amd64.deb",
      "digest": "sha256:ABCDEF0123",
      "browser_download_url": "https://github.com/o/r/releases/download/v0.2.0/zacos9_0.2.0_amd64.deb" }
  ]
})";

int main(int argc, char *argv[]) {
	QCoreApplication app(argc, argv);

	/* ---- releases ---- */
	Release r = parseRelease(RELEASE, "amd64");
	check(r.valid, "a release with an amd64 package is valid");
	check(r.version == "0.2.0", "the version is the tag without its v");
	check(r.debName == "zacos9_0.2.0_amd64.deb", "it picks zacos9 for amd64, not dbgsym or arm64");
	check(r.sha256 == "abcdef0123", "the digest is kept, lower case, without sha256:");
	check(r.name == "ZacOS 9 0.2.0" && r.notes == "Global menus.", "title and notes are kept");

	check(!parseRelease(RELEASE, "i386").valid, "no package for this architecture: not valid");
	check(!parseRelease("not json", "amd64").valid, "broken JSON: not valid");
	check(!parseRelease(R"({"tag_name":"v1.0; rm -rf /","assets":[]})", "amd64").valid,
		"a tag that isn't a Debian version is refused");
	check(!parseRelease(R"({"tag_name":"v1.0","assets":[{"name":"zacos9_1.0_amd64.deb",
		"browser_download_url":"http://example.com/zacos9_1.0_amd64.deb"}]})", "amd64").valid,
		"a package that isn't served over https is refused");

	/* ---- apt's simulation ---- */
	const QString sim =
		"Reading package lists...\n"
		"Inst libfoo1 [1.2-1] (1.2-2 Debian-Security:13/stable-security [amd64])\n"
		"Inst linux-image-6.12.9-amd64 (6.12.9-1 Debian:13.1/stable [amd64])\n"
		"Conf libfoo1 (1.2-2 Debian-Security:13/stable-security [amd64])\n";
	const auto ups = parseAptSimulation(sim);
	check(ups.size() == 2, "two Inst lines, two updates (Conf lines aren't counted)");
	check(ups.size() == 2 && ups[0].package == "libfoo1" && ups[0].from == "1.2-1" &&
		ups[0].to == "1.2-2", "an upgrade has its old and new versions");
	check(ups.size() == 2 && ups[1].from.isEmpty() && ups[1].to == "6.12.9-1",
		"a new package has no old version");
	check(parseAptSimulation("0 upgraded, 0 newly installed.\n").empty(), "nothing to do: no updates");

	/* ---- Debian version order ---- */
	check(versionNewer("0.2.0", "0.0.1"), "0.2.0 is newer than 0.0.1");
	check(!versionNewer("0.0.1", "0.0.1"), "the same version isn't newer");
	check(versionNewer("0.10.0", "0.9.0"), "0.10.0 is newer than 0.9.0 (not a string comparison)");
	check(!versionNewer("0.2.0~rc1", "0.2.0"), "a ~rc comes before its release");
	check(versionNewer("0.1", ""), "anything is newer than not installed");
	check(!versionNewer("", "0.1"), "nothing is never newer");

	QTextStream(stdout) << (fails ? "FAILED\n" : "all passed\n");
	return fails ? 1 : 0;
}
