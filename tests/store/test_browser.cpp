#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <cassert>
#include <functional>
#include <memory>
#include <vector>

#include "panelkit.h"
#include "storeclient.h"
#define private public
#include "store.h"
#undef private

static void writeFile(const QString &path, const QByteArray &data, bool executable = false) {
	QFile file(path);
	assert(file.open(QIODevice::WriteOnly));
	assert(file.write(data) == data.size());
	file.close();
	if (executable) {
		assert(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
	}
}

static bool waitFor(const std::function<bool()> &condition) {
	QElapsedTimer timer;
	timer.start();
	while (!condition() && timer.elapsed() < 5000) {
		QApplication::processEvents();
		QThread::msleep(5);
	}
	return condition();
}

static void confirm() {
	QTimer::singleShot(0, [] {
		QWidget *dialog = QApplication::activeModalWidget();
		assert(dialog);
		QKeyEvent key(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
		QApplication::sendEvent(dialog, &key);
	});
}

int main(int argc, char **argv) {
	QTemporaryDir root;
	assert(root.isValid());
	qputenv("HOME", root.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (root.path() + "/config").toUtf8());
	qputenv("XDG_DATA_HOME", (root.path() + "/data").toUtf8());
	qputenv("TEST_STATE", root.path().toUtf8());
	qputenv("PATH", (root.path() + ":/usr/bin:/bin").toUtf8());
	QApplication app(argc, argv);
	const QString catalog = root.path() + "/catalog.json";
	writeFile(catalog, R"JSON({"categories":["Featured","Internet"],
		"items":[{"id":"curated","name":"Curated Browser","category":"Internet",
		"blurb":"Web browser","packages":["browser"],"featured":true}]})JSON");
	qputenv("ZACOS9_STORE_CATALOG", catalog.toUtf8());
	const QString apt = root.path() + "/apt-helper";
	writeFile(apt, "#!/bin/sh\nprintf '%s\\tno\\n' \"$2\"\n", true);
	qputenv("ZACOS9_APPSTORE_HELPER", apt.toUtf8());
	const QString discovery = root.path() + "/discovery";
	writeFile(discovery, R"PY(#!/usr/bin/python3
import json, os, sys
if os.getenv("TEST_BAD") == "1":
    print("broken")
elif sys.argv[1] == "debian":
    print(json.dumps({"items":[{"id":"org.example.Editor","name":"Debian Editor",
        "blurb":"Edit photos","packages":["editor"],"source":"debian"}]}))
else:
    print(json.dumps({"items":[{"id":"org.example.Paint","name":"Flatpak Paint",
        "blurb":"Draw pictures","packages":["org.example.Paint"],"source":"flathub"}]}))
)PY", true);
	qputenv("ZACOS9_SOFTWARE_CATALOG_HELPER", discovery.toUtf8());
	writeFile(root.path() + "/flatpak", R"PY(#!/usr/bin/python3
import os, pathlib, sys
root = pathlib.Path(os.environ["TEST_STATE"])
args = sys.argv[1:]
with (root / "calls").open("a") as log:
    log.write(" ".join(args) + "\n")
assert args[0] == "--user"
command = args[1]
enabled = root / "enabled"
installed = root / "installed"
if command == "remote-list":
    assert "--show-disabled" in args
    if enabled.exists():
        mode = os.getenv("TEST_REMOTE_MODE", "")
        url = "https://wrong.example/repo/" if mode == "untrusted" else "https://dl.flathub.org/repo/"
        options = "disabled" if mode == "disabled" else ""
        print(f"flathub\t{url}\t{options}")
elif command == "remote-add":
    enabled.touch()
elif command == "remote-modify":
    if "--disable" in args:
        enabled.unlink()
    else:
        enabled.touch()
elif command == "list":
    if installed.exists():
        print("org.example.Paint")
elif command == "install":
    if os.getenv("TEST_INSTALL_FAIL") == "1":
        print("Mock installation failed", file=sys.stderr)
        sys.exit(1)
    installed.touch()
elif command == "uninstall":
    installed.unlink()
else:
    sys.exit(2)
)PY", true);

	std::vector<StoreItem> parsed;
	QString error;
	assert(!parseSoftwareCatalog("[]", "debian", &parsed, &error));
	assert(!parseSoftwareCatalog(R"JSON({"items":[{"id":"bad","name":"Bad",
		"source":"debian","packages":["--remove"]}]})JSON", "debian", &parsed, &error));
	assert(!parseSoftwareCatalog(R"JSON({"items":[{"id":"bad","name":"Bad",
		"source":"flathub","packages":["org.example.Paint;bad"]}]})JSON", "flathub", &parsed, &error));

	StoreWindow window;
	window.show();
	assert(window.currentItem()->name == "Curated Browser");
	auto select = [&](const QString &name) {
		window.m_categoryList.select(window.m_categories.indexOf(name), true);
	};
	select("All Applications");
	assert(waitFor([&] { return !window.m_busy; }));
	assert(window.m_debianLoaded && window.m_shown.size() == 2);
	const QPoint field = window.m_search.rect.center();
	QMouseEvent press(QEvent::MouseButtonPress, field, window.mapToGlobal(field),
		Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(&window, &press);
	assert(window.m_host.focus == &window.m_search);
	for (QChar letter : QString("photos")) {
		QKeyEvent key(QEvent::KeyPress, letter.toUpper().unicode(), Qt::NoModifier, QString(letter));
		QApplication::sendEvent(&window, &key);
	}
	assert(window.m_shown.size() == 1 && window.currentItem()->name == "Debian Editor");
	window.m_search.setText("");
	window.m_search.edited();
	select("Featured");
	assert(window.m_shown.size() == 1 && window.currentItem()->name == "Curated Browser");
	select("Flathub");
	assert(window.m_shown.empty() && window.m_status.contains("Enable Flathub"));
	select("Additional Sources");
	assert(!window.m_flathubEnabled && !QFile::exists(root.path() + "/enabled"));
	QTimer::singleShot(0, [] {
		QWidget *dialog = QApplication::activeModalWidget();
		assert(dialog);
		QKeyEvent key(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
		QApplication::sendEvent(dialog, &key);
	});
	window.configureSource();
	assert(!QFile::exists(root.path() + "/enabled"));
	confirm();
	window.configureSource();
	assert(window.m_flathubEnabled && QFile::exists(root.path() + "/enabled"));
	bool remoteOk;
	qputenv("TEST_REMOTE_MODE", "disabled");
	assert(!flathubEnabled(&remoteOk, &error) && remoteOk);
	qputenv("TEST_REMOTE_MODE", "untrusted");
	assert(!flathubEnabled(&remoteOk, &error) && !remoteOk && error.contains("official"));
	qunsetenv("TEST_REMOTE_MODE");
	select("Flathub");
	assert(waitFor([&] { return !window.m_busy; }));
	assert(window.m_shown.size() == 1 && window.currentItem()->source == "flathub");
	qputenv("TEST_INSTALL_FAIL", "1");
	window.act();
	assert(window.m_status == "Mock installation failed" && !window.m_installed);
	qunsetenv("TEST_INSTALL_FAIL");
	window.act();
	assert(window.m_installed && QFile::exists(root.path() + "/installed"));
	confirm();
	window.act();
	assert(!window.m_installed && !QFile::exists(root.path() + "/installed"));
	select("Additional Sources");
	confirm();
	window.configureSource();
	assert(!window.m_flathubEnabled && !QFile::exists(root.path() + "/enabled"));
	select("Flathub");
	assert(window.m_shown.empty());
	select("All Applications");
	qputenv("TEST_BAD", "1");
	window.loadSource("debian");
	assert(waitFor([&] { return !window.m_busy; }));
	assert(window.m_status.contains("Invalid software catalog"));
	assert(window.m_shown.size() == 2);
	qunsetenv("TEST_BAD");
	qputenv("ZACOS9_SOFTWARE_CATALOG_HELPER", "/nonexistent/software-catalog");
	window.loadSource("debian");
	assert(waitFor([&] { return !window.m_busy; }));
	assert(!window.m_status.isEmpty() && window.m_shown.size() == 2);
	QFile calls(root.path() + "/calls");
	assert(calls.open(QIODevice::ReadOnly));
	const QByteArray log = calls.readAll();
	assert(log.contains("--user install --noninteractive flathub org.example.Paint"));
	assert(log.contains("--user uninstall --noninteractive -- org.example.Paint"));
	assert(log.contains("https://dl.flathub.org/repo/flathub.flatpakrepo"));
	return 0;
}
