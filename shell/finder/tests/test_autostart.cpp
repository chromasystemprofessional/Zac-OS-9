/* FreeDesktop autostart rules on private directories. */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "autostart.h"

static int failures = 0;

static void check(bool ok, const char *what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

static QString head(const char *id) {
	return QString("[Desktop Entry]\nType=Application\nExec=sh -c ") + id + "\n";
}

static void write(const QString &dir, const QString &name, const QString &text) {
	QDir().mkpath(dir);
	QFile f(dir + "/" + name);
	f.open(QIODevice::WriteOnly);
	f.write(text.toUtf8());
}

static const AutostartEntry *find(const std::vector<AutostartEntry> &v, const QString &id) {
	for (const AutostartEntry &e : v) {
		if (e.id == id) {
			return &e;
		}
	}
	return nullptr;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir tmp;
	const QString user = tmp.path() + "/user", sys = tmp.path() + "/sys";
		write(sys, "both.desktop", head("both") + "Name=System Both\n");
	write(user, "both.desktop", head("both") + "Name=User Both\n");
	write(sys, "masked.desktop", head("masked") + "Name=Masked\n");
	write(user, "masked.desktop", head("masked") + "Name=Masked\nHidden=true\n");
	write(sys, "gnome.desktop", head("gnome") + "Name=Gnome Off\nX-GNOME-Autostart-enabled=false\n");
	write(sys, "only.desktop", head("only") + "Name=Only KDE\nOnlyShowIn=KDE;\n");
	write(sys, "notshow.desktop", head("notshow") + "Name=Not ZacOS\nNotShowIn=ZacOS9;\n");
	write(sys, "zac.desktop", head("zac") + "Name=Only Zac\nOnlyShowIn=GNOME;ZacOS9;\n");
	write(sys, "missing.desktop", "[Desktop Entry]\nName=Missing\nExec=/nonexistent/prog\n");
	write(sys, "tryexec.desktop", head("tryexec") + "Name=Try\nTryExec=no-such-program-zz\n");
	write(sys, "local.desktop", head("local") + "Name=Plain\nName[fr]=Simple\n");
	write(sys, "dup.desktop", head("both") + "Name=Same Command\n");
	write(sys, "env.desktop", "[Desktop Entry]\nName=Env\nExec=env A=1 sh -c y\n");

	AutostartContext c;
	c.userDir = user;
	c.systemDirs = { sys };
	c.desktops = { "ZacOS9" };
	c.locale = "fr";
	auto v = autostartEntries(c);

	const AutostartEntry *both = find(v, "both.desktop");
	check(both && both->name == "User Both" && both->userEntry && both->overridesSystem && both->enabled,
		"user entry overrides the system entry of the same ID");
	const AutostartEntry *masked = find(v, "masked.desktop");
	check(masked && !masked->enabled, "user Hidden=true masks a system entry");
	check(find(v, "gnome.desktop") && !find(v, "gnome.desktop")->enabled,
		"X-GNOME-Autostart-enabled=false disables");
	check(!find(v, "only.desktop")->enabled, "OnlyShowIn without this desktop is excluded");
	check(!find(v, "notshow.desktop")->enabled, "NotShowIn this desktop is excluded");
	check(find(v, "zac.desktop")->enabled, "OnlyShowIn listing this desktop is kept");
	check(!find(v, "missing.desktop")->enabled && !find(v, "tryexec.desktop")->enabled,
		"missing executables are excluded");
	check(find(v, "env.desktop")->enabled, "env wrapper looks through to its program");
	check(find(v, "local.desktop")->name == "Simple", "localized Name is used");
	int enabledSame = 0;
	for (const AutostartEntry &e : v) {
		enabledSame += e.enabled && e.command.simplified() == "sh -c both";
	}
	check(enabledSame == 1, "enabled entries running one command appear once");

	c.desktops = {};
	check(!find(autostartEntries(c), "zac.desktop")->enabled, "no desktop name fails OnlyShowIn");

	/* Disabling a system-only entry writes a user override, not the system file. */
	c.desktops = { "ZacOS9" };
	QString error;
	check(autostartSetEnabled(c, "zac.desktop", false, &error), "disable system entry");
	check(!find(autostartEntries(c), "zac.desktop")->enabled &&
		find(autostartEntries(c), "zac.desktop")->userEntry, "override disables it");
	QFile sysFile(sys + "/zac.desktop");
	sysFile.open(QIODevice::ReadOnly);
	check(!sysFile.readAll().contains("Hidden"), "system file is untouched");
	check(autostartSetEnabled(c, "zac.desktop", true, &error) &&
		find(autostartEntries(c), "zac.desktop")->enabled, "re-enable");
	check(!autostartSetEnabled(c, "../x.desktop", true, &error), "path-like IDs are refused");

	return failures ? 1 : 0;
}
