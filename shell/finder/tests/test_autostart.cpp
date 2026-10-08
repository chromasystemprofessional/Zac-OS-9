/* FreeDesktop autostart rules on private directories. */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
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
	write(sys, "minimal.desktop", head("minimal") + "Name=Restored\n");
	write(user, "minimal.desktop", "[Desktop Entry]\nHidden=true\n");
	write(sys, "gnome.desktop", head("gnome") + "Name=Gnome Off\nX-GNOME-Autostart-enabled=false\n");
	write(sys, "only.desktop", head("only") + "Name=Only KDE\nOnlyShowIn=KDE;\n");
	write(sys, "notshow.desktop", head("notshow") + "Name=Not ZacOS\nNotShowIn=ZacOS9;\n");
	write(sys, "zac.desktop", head("zac") + "Name=Only Zac\nOnlyShowIn=GNOME;ZacOS9;\n");
	write(sys, "missing.desktop",
		"[Desktop Entry]\nType=Application\nName=Missing\nExec=/nonexistent/prog\n");
	write(sys, "invalid-tryexec.desktop",
		head("true") + "Name=Try\nTryExec=no-such-program-zz\n");
	write(sys, "tryexec.desktop", head("tryexec") + "Name=Try\nTryExec=no-such-program-zz\n");
	write(sys, "local.desktop", head("local") + "Name=Plain\nName[fr]=Simple\n");
	write(sys, "dup.desktop", head("both") + "Name=Same Command\n");
	write(sys, "env.desktop", "[Desktop Entry]\nType=Application\nName=Env\nExec=env A=1 sh -c y\n");
	write(sys, "quoted.desktop",
		"[Desktop Entry]\nType=Application\nName=Quoted\nExec=/bin/echo \"argument with spaces\"\n");

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
	check(!find(v, "missing.desktop")->enabled &&
		!find(v, "invalid-tryexec.desktop")->enabled,
		"GIO parses Exec and unavailable Exec and TryExec programs are disabled");
	check(find(v, "env.desktop")->enabled, "valid env-wrapped Exec entries are preserved");
	check(find(v, "local.desktop")->name == "Simple", "localized Name is used");
	check(find(v, "dup.desktop")->enabled && find(v, "both.desktop")->enabled,
		"distinct desktop-file IDs with the same command remain enabled");
	check(find(v, "quoted.desktop")->enabled &&
		find(v, "quoted.desktop")->command.contains("\"argument with spaces\""),
		"quoted Exec arguments remain intact and parse as a desktop command");

	c.desktops = {};
	check(!find(autostartEntries(c), "zac.desktop")->enabled, "no desktop name fails OnlyShowIn");
	qputenv("XDG_CONFIG_DIRS", "");
	check(autostartDefaultContext().systemDirs == QStringList{ "/etc/xdg/autostart" },
		"empty XDG_CONFIG_DIRS uses the FreeDesktop default");

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
	check(autostartSetEnabled(c, "minimal.desktop", true, &error) &&
		find(autostartEntries(c), "minimal.desktop")->enabled &&
		find(autostartEntries(c), "minimal.desktop")->path == sys + "/minimal.desktop",
		"re-enabling a Hidden-only override restores the lower-priority entry");
	write(user, "orphan.desktop", "[Desktop Entry]\nHidden=true\n");
	check(!autostartSetEnabled(c, "orphan.desktop", true, &error) &&
		QFileInfo::exists(user + "/orphan.desktop") && !error.isEmpty(),
		"a Hidden-only override without a lower-priority entry is preserved and reports why");
	check(!autostartSetEnabled(c, "../x.desktop", true, &error), "path-like IDs are refused");

	/* Startup Items: drag an application in, drag it to the Trash. */
	/* Qt picks the home Trash only for files on $HOME's disk. */
	qputenv("HOME", tmp.path().toUtf8());
	qputenv("XDG_DATA_HOME", (tmp.path() + "/data").toUtf8());
	QDir().mkpath(tmp.path() + "/data"); /* Qt makes only Trash itself */
	const QString apps = tmp.path() + "/apps";
	write(apps, "editor.desktop", "[Desktop Entry]\nType=Application\nName=Editor\n"
		"Exec=sh -c \"echo two  spaces\" %U\nOnlyShowIn=GNOME;\nIcon=editor\n");
	write(apps, "masked.desktop", head("masked") + "Name=Masked App\n");
	write(apps, "gone.desktop", "[Desktop Entry]\nType=Application\nName=Gone\nExec=/nonexistent/prog\n");
	write(apps, "link.desktop", "[Desktop Entry]\nType=Link\nName=Web\nURL=https://example.com\n");
	check(autostartAddApplication(c, "editor.desktop", apps + "/editor.desktop", &error),
		"an application dropped into Startup Items is added");
	const auto addedEntries = autostartEntries(c);
	const AutostartEntry *added = find(addedEntries, "editor.desktop");
	check(added && added->enabled && added->userEntry && added->path == user + "/editor.desktop",
		"the added application is an enabled user autostart entry, even if its menu entry "
		"was for another desktop");
	QFile addedFile(user + "/editor.desktop");
	addedFile.open(QIODevice::ReadOnly);
	const QByteArray addedText = addedFile.readAll();
	check(addedText.contains("X-ZacOS9-Startup-Item=true") && !addedText.contains("OnlyShowIn") &&
		addedText.contains("Icon=editor") && added && added->command.contains("\"echo two  spaces\""),
		"the entry copies the application's own command and icon unchanged");
	addedFile.close();
	check(autostartAddApplication(c, "editor.desktop", apps + "/editor.desktop", &error),
		"dropping an application that is already a startup item is harmless");
	check(autostartAddApplication(c, "masked.desktop", apps + "/masked.desktop", &error) &&
		find(autostartEntries(c), "masked.desktop")->enabled,
		"an application whose system entry the user disabled is turned back on");
	check(!autostartAddApplication(c, "gone.desktop", apps + "/gone.desktop", &error) &&
		!QFileInfo::exists(user + "/gone.desktop") && error.contains("could not be found"),
		"an application whose program is missing is refused, and nothing is written");
	check(!autostartAddApplication(c, "link.desktop", apps + "/link.desktop", &error) &&
		!QFileInfo::exists(user + "/link.desktop"), "only applications can be startup items");
	check(!autostartAddApplication(c, "../editor.desktop", apps + "/editor.desktop", &error),
		"path-like IDs are refused when adding");

	bool trashed = false;
	check(autostartRemove(c, "editor.desktop", &trashed, &error) && trashed &&
		!QFileInfo::exists(user + "/editor.desktop") &&
		QFileInfo::exists(tmp.path() + "/data/Trash/files/editor.desktop") &&
		!find(autostartEntries(c), "editor.desktop"),
		"the Trash takes a startup item the user added, so it can be dragged back");
	check(autostartAddApplication(c, "editor.desktop",
		tmp.path() + "/data/Trash/files/editor.desktop", &error) &&
		find(autostartEntries(c), "editor.desktop")->enabled,
		"dragging it back out of the Trash adds it again");
	check(autostartRemove(c, "zac.desktop", &trashed, &error) && !trashed &&
		!find(autostartEntries(c), "zac.desktop")->enabled &&
		QFileInfo::exists(sys + "/zac.desktop"),
		"a system startup item is masked with a user override; the system file stays");
	check(!autostartRemove(c, "nothing.desktop", &trashed, &error) && !error.isEmpty(),
		"removing an item that is gone reports why");
	check(!autostartRemove(c, "../zac.desktop", &trashed, &error), "path-like IDs are refused");

	const QString bin = tmp.path() + "/record-start";
	const QString marker = tmp.path() + "/launches";
	write(tmp.path(), "record-start",
		"#!/bin/sh\nprintf '%s\\n' \"$1\" >> '" + marker + "'\n");
	QFile::setPermissions(bin, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	const QString launchSystem = tmp.path() + "/launch-system";
	write(launchSystem, "launch.desktop", "[Desktop Entry]\nType=Application\nName=Launch\nExec=\"" +
		bin + "\" \"argument with spaces\"\n");
	AutostartContext launchContext = c;
	launchContext.systemDirs = { launchSystem };
	launchContext.userDir = tmp.path() + "/launch-user";
	launchContext.desktops = { "ZacOS9" };
	const auto launchEntries = autostartEntries(launchContext);
	check(find(launchEntries, "launch.desktop") && find(launchEntries, "launch.desktop")->enabled,
		"session launch fixture is enabled");
	QDir().mkpath(tmp.path() + "/runtime");
	qputenv("XDG_RUNTIME_DIR", (tmp.path() + "/runtime").toUtf8());
	qputenv("XDG_SESSION_ID", "autostart-test");
	QElapsedTimer timer;
	timer.start();
	check(autostartRunSession(launchContext, &error), "enabled desktop entries launch for the session");
	while (!QFileInfo::exists(marker) && timer.elapsed() < 3000) {
		QThread::msleep(10);
	}
	QFile launched(marker);
	check(launched.open(QIODevice::ReadOnly) &&
		launched.readAll() == "argument with spaces\n", "quoted Exec arguments reach the launched app");
	check(autostartRunSession(launchContext, &error), "a repeated session hook is harmless");
	launched.close();
	check(launched.open(QIODevice::ReadOnly) &&
		launched.readAll() == "argument with spaces\n", "session startup entries are not launched twice");

	return failures ? 1 : 0;
}
