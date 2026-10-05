#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QThread>
#include <cassert>

#include "diskinit.h"
#include "netvolumes.h"

std::vector<NetVolume> netVolumes() {
	return {};
}

static void write(const QString &path, const QByteArray &data, bool executable = false) {
	QFile file(path);
	assert(file.open(QIODevice::WriteOnly));
	assert(file.write(data) == data.size());
	file.close();
	if (executable) {
		assert(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
	}
}

static QByteArray read(const QString &path) {
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static bool waitFor(const std::function<bool()> &ready) {
	QElapsedTimer elapsed;
	elapsed.start();
	while (!ready() && elapsed.elapsed() < 6000) {
		QApplication::processEvents();
		QThread::msleep(10);
	}
	return ready();
}

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QTemporaryDir root;
	assert(root.isValid());
	const QJsonObject disk{{ "device", "/dev/sdz" }, { "name", "Test USB" },
		{ "identity", "original-device" }, { "size", 8000000000.0 },
		{ "eligible", true }, { "unreadable", true },
		{ "children", QJsonArray{ "/dev/sdz1" } }};
	auto catalog = [&](const QJsonObject &item) {
		return QJsonDocument(QJsonObject{{ "version", 1 }, { "disks", QJsonArray{ item } }}).toJson();
	};
	std::vector<InitializationDisk> parsed;
	QString error;
	assert(parseInitializationDisks(catalog(disk), &parsed, &error));
	assert(parsed.size() == 1 && parsed[0].children == QStringList{ "/dev/sdz1" });
	assert(!parseInitializationDisks("{}", &parsed, &error));
	QJsonObject bad = disk;
	bad["device"] = "/dev/sdz;touch";
	assert(!parseInitializationDisks(catalog(bad), &parsed, &error));
	bad = disk;
	bad["identity"] = "";
	assert(!parseInitializationDisks(catalog(bad), &parsed, &error));
	bad["eligible"] = false;
	bad["size"] = 0;
	assert(parseInitializationDisks(catalog(bad), &parsed, &error));
	const QString fixture = root.path() + "/catalog";
	const QString helper = root.path() + "/helper";
	const QString authorizer = root.path() + "/authorize";
	const QString log = root.path() + "/operations";
	qputenv("DISK_TEST_CATALOG", fixture.toUtf8());
	qputenv("DISK_TEST_LOG", log.toUtf8());
	write(helper, "#!/bin/sh\nif [ \"$1\" = list ]; then cat \"$DISK_TEST_CATALOG\"; "
		"else printf '%s\\n' \"$*\" >> \"$DISK_TEST_LOG\"; "
		"printf '{\"version\":1,\"device\":\"/dev/sdz\",\"initialized\":true}\\n'; fi\n", true);
	write(authorizer, "#!/bin/sh\nexec \"$@\"\n", true);
	bool invalidSuccess = false;
	auto exercise = [&](DiskInitialization::Choice choice, bool confirm,
			bool changed, bool eligible, bool unreadable, bool failedMount) {
		QFile::remove(log);
		QJsonObject current = disk;
		current["eligible"] = eligible;
		current["unreadable"] = unreadable;
		write(fixture, catalog(current));
		int prompts = 0, confirmations = 0, errors = 0, initialized = 0;
		bool busy = false;
		DiskInitialization::Interface ui;
		ui.choose = [&](const InitializationDisk &) {
			++prompts;
			return choice;
		};
		ui.confirm = [&](const InitializationDisk &) {
			++confirmations;
			if (changed) {
				QJsonObject replacement = current;
				replacement["identity"] = "replacement-device";
				write(fixture, catalog(replacement));
			}
			return confirm;
		};
		ui.error = [&](const QString &) { ++errors; };
		ui.busy = [&](bool active) { busy = active; };
		ui.initialized = [&](const QString &device) {
			assert(device == "/dev/sdz");
			++initialized;
		};
		DiskInitialization controller(ui, helper, authorizer, helper);
		if (failedMount) {
			controller.mountFailed("/dev/sdz1");
		} else {
			controller.mountFailed("/dev/unrelated");
		}
		const bool expectedPrompt = eligible && (unreadable || failedMount);
		if (!expectedPrompt) {
			QElapsedTimer elapsed;
			elapsed.start();
			while (elapsed.elapsed() < 800) {
				QApplication::processEvents();
				QThread::msleep(10);
			}
			assert(prompts == 0 && read(log).isEmpty());
			return;
		}
		assert(waitFor([&] { return prompts == 1; }));
		if (choice == DiskInitialization::Initialize && confirm) {
			assert(waitFor([&] { return initialized + errors == 1; }));
			assert(!busy);
			if (changed || invalidSuccess) {
				assert(errors == 1 && read(log).isEmpty());
			} else {
				assert(initialized == 1 && read(log) ==
					"initialize /dev/sdz original-device\n");
			}
		} else if (choice == DiskInitialization::Eject) {
			assert(waitFor([&] { return !read(log).isEmpty(); }));
			assert(read(log) == "power-off --block-device /dev/sdz\n");
		} else {
			assert(read(log).isEmpty());
		}
		assert(confirmations == (choice == DiskInitialization::Initialize ? 1 : 0));
	};
	exercise(DiskInitialization::Ignore, false, false, true, true, false);
	exercise(DiskInitialization::Initialize, false, false, true, true, false);
	exercise(DiskInitialization::Initialize, true, false, true, true, false);
	exercise(DiskInitialization::Initialize, true, true, true, true, false);
	exercise(DiskInitialization::Eject, false, false, true, true, false);
	exercise(DiskInitialization::Ignore, false, false, false, true, false);
	exercise(DiskInitialization::Ignore, false, false, true, false, false);
	exercise(DiskInitialization::Initialize, true, false, true, false, true);
	write(authorizer, "#!/bin/sh\nprintf '{}\\n'\n", true);
	invalidSuccess = true;
	exercise(DiskInitialization::Initialize, true, false, true, true, false);
	return 0;
}
