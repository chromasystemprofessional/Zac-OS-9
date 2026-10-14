/* The Internet panel's settings: default browser and mail program through
 * GIO (mimeapps.list), the downloads folder in user-dirs.dirs. */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "defaultapps.h"

static int fails;

static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static void app(const QString &dir, const char *id, const char *name, const char *types) {
	QFile f(dir + "/" + id);
	f.open(QIODevice::WriteOnly);
	f.write(QString("[Desktop Entry]\nType=Application\nName=%1\nExec=true %u\nMimeType=%2\n")
		.arg(name, types).toUtf8());
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	qputenv("XDG_CONFIG_DIRS", (home.path() + "/none").toUtf8());
	const QString apps = home.path() + "/data/applications";
	QDir().mkpath(apps);
	QDir().mkpath(home.path() + "/config");
	app(apps, "zebra-browser.desktop", "Zebra Browser",
		"x-scheme-handler/http;x-scheme-handler/https;text/html;");
	app(apps, "aardvark-web.desktop", "Aardvark Web", "x-scheme-handler/http;x-scheme-handler/https;");
	app(apps, "mailer.desktop", "Mailer", "x-scheme-handler/mailto;");
	QCoreApplication qapp(argc, argv);

	/* (The system's own programs may be listed too.) */
	const QList<DefaultApp> browsers = defaultAppChoices(AppRole::WebBrowser);
	int aardvark = -1, zebra = -1;
	for (int i = 0; i < browsers.size(); i++) {
		aardvark = browsers[i].id == "aardvark-web.desktop" ? i : aardvark;
		zebra = browsers[i].id == "zebra-browser.desktop" ? i : zebra;
	}
	check(aardvark >= 0 && zebra > aardvark, "browsers are the programs that open web links, by name");
	bool mailer = false;
	for (const DefaultApp &a : defaultAppChoices(AppRole::Mail)) {
		mailer |= a.id == "mailer.desktop";
	}
	check(mailer, "mail programs are those that open mailto links");
	check(setDefaultApp(AppRole::WebBrowser, "zebra-browser.desktop") &&
		defaultApp(AppRole::WebBrowser) == "zebra-browser.desktop", "choosing a browser makes it the default");
	QFile list(home.path() + "/config/mimeapps.list");
	list.open(QIODevice::ReadOnly);
	const QString mimeapps = QString::fromUtf8(list.readAll());
	check(mimeapps.contains("x-scheme-handler/https=zebra-browser.desktop") &&
		mimeapps.contains("text/html=zebra-browser.desktop"),
		"for https and web pages too, in mimeapps.list");
	check(setDefaultApp(AppRole::Mail, "mailer.desktop") && defaultApp(AppRole::Mail) == "mailer.desktop",
		"and a mail program likewise");

	check(downloadFolder() == home.path() + "/Downloads", "downloads go to ~/Downloads by default");
	QFile dirs(home.path() + "/config/user-dirs.dirs");
	dirs.open(QIODevice::WriteOnly);
	dirs.write("# written by xdg-user-dirs-update\nXDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
		"XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n");
	dirs.close();
	check(setDownloadFolder(home.path() + "/Stuff/Incoming") &&
		downloadFolder() == home.path() + "/Stuff/Incoming" && QDir(home.path() + "/Stuff/Incoming").exists(),
		"choosing another folder makes it (and it is read back)");
	dirs.open(QIODevice::ReadOnly);
	const QString written = QString::fromUtf8(dirs.readAll());
	check(written.contains("XDG_DOWNLOAD_DIR=\"$HOME/Stuff/Incoming\"") &&
		written.contains("XDG_DESKTOP_DIR=\"$HOME/Desktop\"") && written.startsWith("# written"),
		"user-dirs.dirs keeps its other lines; Home is written $HOME");
	return fails ? 1 : 0;
}
