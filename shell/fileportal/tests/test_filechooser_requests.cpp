#include <gio/gio.h>

#include "filechooserportal.h"
#include "alias.h"
#include "settings.h"

#include <QLabel>
#include <QApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDialog>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QDropEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <cstdio>
#include <functional>

static int failures = 0;
static int serial = 0;
static const QString service = "org.freedesktop.impl.portal.desktop.zacos9";

static void check(bool condition, const char *message) {
	if (!condition) {
		std::fprintf(stderr, "FAIL: %s\n", message);
		++failures;
	}
}

static QDBusMessage choose(const QDBusConnection &client, const QString &method,
		const QVariantMap &options, const std::function<void(QDialog *, const QString &)> &action,
		const QString &location = {}) {
	const QString handle = QStringLiteral("/org/freedesktop/portal/desktop/request/test/r%1")
		.arg(++serial);
	QDBusMessage call = QDBusMessage::createMethodCall(service,
		"/org/freedesktop/portal/desktop", "org.freedesktop.impl.portal.FileChooser", method);
	call.setArguments({ QVariant::fromValue(QDBusObjectPath(handle)),
		"com.visualstudio.code", "", "Choose Test", options });
	QDBusPendingCallWatcher watcher(client.asyncCall(call, 25000));
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	QTimer interact;
	interact.setInterval(10);
	bool acted = false;
	bool choseDrive = false;
	QObject::connect(&interact, &QTimer::timeout, [&]() {
		if (!acted) {
			if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
				if ((options.contains("current_folder") || !location.isEmpty()) && !choseDrive &&
						!qobject_cast<QFileDialog *>(dialog)) {
					auto *list = dialog->findChild<QListWidget *>();
					check(list,
						"app-suggested Home does not bypass the Desktop drives screen");
					if (list && !location.isEmpty()) {
						const auto items = list->findItems(location, Qt::MatchExactly);
						check(items.size() == 1, "the requested desktop folder alias is listed");
						if (!items.isEmpty()) {
							list->setCurrentItem(items.front());
						}
					}
					choseDrive = true;
					dialog->accept();
					return;
				}
				acted = true;
				action(dialog, handle);
			}
		}
	});
	QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
	QObject::connect(&timeout, &QTimer::timeout, [&]() {
		check(false, "chooser request completes within timeout");
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (auto *dialog = qobject_cast<QDialog *>(widget)) {
				dialog->reject();
			}
		}
		loop.quit();
	});
	interact.start();
	timeout.start(30000);
	loop.exec();
	check(acted, "a real D-Bus request displays a chooser");
	return watcher.isFinished() ? watcher.reply() : QDBusMessage();
}

static QVariantMap successfulResults(const QDBusMessage &reply) {
	check(reply.type() == QDBusMessage::ReplyMessage && reply.signature() == "ua{sv}" &&
		reply.arguments().value(0).toUInt() == 0, "selection returns the portal success signature");
	return qdbus_cast<QVariantMap>(reply.arguments().value(1));
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	const QString base = QFileInfo(home.path()).canonicalFilePath();
	qputenv("HOME", QFile::encodeName(base));
	qputenv("XDG_DATA_HOME", QFile::encodeName(base + "/data"));
	qputenv("XDG_CONFIG_HOME", QFile::encodeName(base + "/config"));
	qputenv("XDG_DATA_DIRS", QFile::encodeName(base + "/share"));
	qputenv("XDG_RUNTIME_DIR", QFile::encodeName(base + "/runtime"));
	QDir().mkpath(base + "/runtime");
	QFile::setPermissions(base + "/runtime", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	QApplication app(argc, argv);
	app.setQuitOnLastWindowClosed(false);
	if (!registerFileChooserPortal()) {
		return 1;
	}
	const QDBusConnection client = QDBusConnection::connectToBus(
		QDBusConnection::SessionBus, "chooser-test-client");
	check(home.isValid(), "create chooser fixture");
	fileChooserWarmUp();
	/* Zacintosh HD is the Finder's Macintosh view; Home holds the files. */
	const QString disk = base + "/runtime/zacos9-file-chooser/Zacintosh HD";
	const QString drive = base;
	const QString suggested = home.filePath("Suggested");
	check(QDir().mkpath(suggested), "create app-suggested directory");
	const QString file = QDir(drive).filePath("test.txt");
	QFile fixture(file);
	check(fixture.open(QIODevice::WriteOnly), "create selectable file");
	fixture.write("test");
	fixture.close();
	check(fileChooserPathWithinDrive(file, drive), "existing file inside the drive is allowed");
	check(fileChooserPathWithinDrive(QDir(drive).filePath("new.txt"), drive),
		"new save file inside the drive is allowed");
	check(!fileChooserPathWithinDrive(QFileInfo(home.path()).absolutePath(), drive) &&
		!fileChooserPathWithinDrive("/", drive) &&
		!fileChooserPathWithinDrive(QDir(drive).filePath("../escape.txt"), drive),
		"Home, Unix root, and traversal outside the drive are rejected");
	check(QFile::link(QFileInfo(home.path()).absolutePath(), QDir(drive).filePath("outside")),
		"create escaping directory alias");
	check(!fileChooserPathWithinDrive(QDir(drive).filePath("outside"), drive) &&
		!fileChooserPathWithinDrive(QDir(drive).filePath("outside/new.txt"), drive),
		"symlinks cannot navigate or save outside the selected drive");
	QByteArray folder = QFile::encodeName(suggested);
	folder.append('\0');
	const QVariantMap options{ { "current_folder", folder } };

	auto results = successfulResults(choose(client, "OpenFile", options,
		[&](QDialog *dialog, const QString &) {
			auto *picker = qobject_cast<QFileDialog *>(dialog);
			check(picker && picker->fileMode() == QFileDialog::ExistingFile,
				"OpenFile decodes the wire options and displays a file picker");
			if (picker) {
				check(picker->directory().absolutePath() == disk,
					"browser starts at the startup disk, not the suggested Home folder");
				check(QFileInfo(disk + "/System Folder").isDir() &&
					QFileInfo(disk + "/Applications").isDir() && QFileInfo(disk + "/Home").isDir(),
					"the startup disk shows System Folder, Applications and Home");
				QWidget *sidebar = picker->findChild<QWidget *>("sidebar");
				check(sidebar && sidebar->isHidden(),
					"the places sidebar remains hidden in the displayed chooser");
				QWidget *lookIn = picker->findChild<QWidget *>("lookInCombo");
				check(lookIn && lookIn->isHidden(),
					"Unix ancestors are not exposed in a location dropdown");
				auto *name = picker->findChild<QLineEdit *>("fileNameEdit");
				check(name, "file chooser offers an editable file name");
				if (name) {
					name->setText("/");
					QMetaObject::invokeMethod(picker, "accept");
					check(picker->isVisible() && picker->directory().absolutePath() == disk,
						"typing Unix root cannot accept or navigate outside the selected drive");
					check(!picker->selectedFiles().contains("/"),
						"a rejected outside item does not stay selected and block later choices");
					name->setText("test.txt");
				}
				picker->setDirectory(disk + "/Home");
				check(picker->directory().absolutePath() == disk + "/Home",
					"opening Home keeps the startup disk's path, so Up returns to it");
				auto *select = new QTimer(picker);
				QObject::connect(select, &QTimer::timeout, picker, [picker, name]() {
					if (name) {
						name->setText("test.txt");
					}
					QMetaObject::invokeMethod(picker, "accept");
				});
				select->start(20);
			} else {
				dialog->reject();
			}
		}));
	check(results.value("uris").toStringList() ==
		QStringList{ QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded) },
		"OpenFile returns the selected file URI");

	QVariantMap directories(options);
	directories.insert("directory", true);
	results = successfulResults(choose(client, "OpenFile", directories,
		[&](QDialog *dialog, const QString &) {
			auto *picker = qobject_cast<QFileDialog *>(dialog);
			check(picker && picker->fileMode() == QFileDialog::Directory,
				"VS Code folder selection retains the directory option");
			if (picker) {
				QMetaObject::invokeMethod(picker, "accept");
				check(picker->isVisible(),
					"the startup disk itself is not handed to applications as a folder");
				picker->setDirectory(disk + "/Home");
				QMetaObject::invokeMethod(picker, "accept");
			} else {
				dialog->reject();
			}
		}));
	const QStringList folderUris = results.value("uris").toStringList();
	if (folderUris.size() != 1 ||
			QDir::cleanPath(QUrl(folderUris.value(0)).toLocalFile()) != drive) {
		std::fprintf(stderr, "Folder URI: %s; expected path: %s\n",
			qPrintable(folderUris.join(", ")), qPrintable(drive));
	}
	check(folderUris.size() == 1 &&
		QDir::cleanPath(QUrl(folderUris.value(0)).toLocalFile()) == drive,
		"folder selection returns the real directory URI, not the chooser's link");

	QVariantMap save(options);
	save.insert("current_name", "new.txt");
	results = successfulResults(choose(client, "SaveFile", save,
		[&](QDialog *dialog, const QString &) {
			if (auto *picker = qobject_cast<QFileDialog *>(dialog)) {
				QMetaObject::invokeMethod(picker, "accept");
				check(picker->isVisible(), "nothing is saved into the startup disk's own folders");
				picker->setDirectory(disk + "/Home");
				/* Qt may clear a typed name while reloading the folder; retry like a user would. */
				auto *save = new QTimer(picker);
				QObject::connect(save, &QTimer::timeout, picker, [picker]() {
					if (auto *name = picker->findChild<QLineEdit *>("fileNameEdit")) {
						name->setText("new.txt");
					}
					QMetaObject::invokeMethod(picker, "accept");
				});
				save->start(20);
			} else {
				dialog->reject();
			}
		}));
	check(results.value("uris").toStringList() ==
		QStringList{ QUrl::fromLocalFile(QDir(drive).filePath("new.txt")).toString(QUrl::FullyEncoded) },
		"SaveFile returns the suggested file name without writing the file");
	check(!QFile::exists(QDir(drive).filePath("new.txt")), "the backend does not create saved files");

	QDBusArgument names;
	names.beginArray(QMetaType::fromType<QByteArray>());
	names << QByteArray("test.txt\0", 9) << QByteArray("test.txt\0", 9);
	names.endArray();
	QVariantMap saveFiles(options);
	saveFiles.insert("files", QVariant::fromValue(names));
	results = successfulResults(choose(client, "SaveFiles", saveFiles,
		[&](QDialog *dialog, const QString &) {
			if (auto *picker = qobject_cast<QFileDialog *>(dialog)) {
				QMetaObject::invokeMethod(picker, "accept");
				check(picker->isVisible(), "nothing is saved into the startup disk's own folders");
				picker->setDirectory(disk + "/Home");
				if (picker->acceptMode() == QFileDialog::AcceptSave) {
					picker->selectFile("new.txt");
				}
			}
			QMetaObject::invokeMethod(dialog, "accept");
		}));
	check(results.value("uris").toStringList() == QStringList{
			QUrl::fromLocalFile(QDir(drive).filePath("test (1).txt")).toString(QUrl::FullyEncoded),
			QUrl::fromLocalFile(QDir(drive).filePath("test (2).txt")).toString(QUrl::FullyEncoded) },
		"SaveFiles decodes aay names, asks for a folder, and avoids collisions");

	QTemporaryDir server;
	const QString serverRoot = QFileInfo(server.path()).canonicalFilePath();
	const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
	check(QDir().mkpath(desktop), "create the Desktop for alias requests");
	for (const QString &name : { QStringLiteral("AFP Projects"), QStringLiteral("Windows Shared") }) {
		const QString target = serverRoot + "/" + name;
		check(QDir().mkpath(target) && QFile::link(target, desktop + "/" + name),
			"create desktop aliases to folders outside Home");
		QFile document(target + "/document.txt");
		check(document.open(QIODevice::WriteOnly), "create a server document");
		document.write("server");
		document.close();
		for (const QString &method : { QStringLiteral("OpenFile"), QStringLiteral("SaveFile"),
				QStringLiteral("SaveFiles") }) {
			QVariantMap aliasOptions;
			if (method == "SaveFile") {
				aliasOptions.insert("current_name", "saved.txt");
			} else if (method == "SaveFiles") {
				aliasOptions.insert("files", QVariant::fromValue(names));
			}
			results = successfulResults(choose(client, method, aliasOptions,
				[&](QDialog *dialog, const QString &) {
					auto *picker = qobject_cast<QFileDialog *>(dialog);
					check(picker && picker->directory().absolutePath() == target,
						"choosing an alias starts directly in its resolved folder");
					if (!picker) {
						dialog->reject();
						return;
					}
					auto *up = picker->findChild<QToolButton *>("toParentButton");
					check(up && !up->isEnabled(), "an alias target has no Unix-parent navigation");
					if (method == "OpenFile") {
						if (auto *edit = picker->findChild<QLineEdit *>("fileNameEdit")) {
							edit->setText(base + "/test.txt");
							QMetaObject::invokeMethod(picker, "accept");
							check(picker->isVisible() && picker->directory().absolutePath() == target,
								"an alias location still refuses selections outside its target");
							edit->setText("document.txt");
						}
					}
					auto *select = new QTimer(picker);
					QObject::connect(select, &QTimer::timeout, picker, [picker, method]() {
						if (method != "SaveFiles") {
							if (auto *edit = picker->findChild<QLineEdit *>("fileNameEdit")) {
								edit->setText(method == "OpenFile" ? "document.txt" : "saved.txt");
							}
						}
						QMetaObject::invokeMethod(picker, "accept");
					});
					select->start(20);
				}, name));
			QStringList expected;
			const QStringList selected = method == "SaveFiles" ? QStringList{ "test.txt", "test (1).txt" } :
				QStringList{ method == "OpenFile" ? "document.txt" : "saved.txt" };
			for (const QString &fileName : selected) {
				expected << QUrl::fromLocalFile(target + "/" + fileName).toString(QUrl::FullyEncoded);
			}
			check(results.value("uris").toStringList() == expected,
				"open and both save methods return real external-folder URIs");
		}
		QVariantMap folderOptions;
		folderOptions.insert("directory", true);
		results = successfulResults(choose(client, "OpenFile", folderOptions,
			[&](QDialog *dialog, const QString &) { QMetaObject::invokeMethod(dialog, "accept"); }, name));
		check(results.value("uris").toStringList() ==
			QStringList{ QUrl::fromLocalFile(target).toString(QUrl::FullyEncoded) },
			"folder selection can return the alias target itself");
	}
	check(QFile::link(serverRoot + "/Disconnected", desktop + "/Offline Server"),
		"create an unavailable server alias");
	const QDBusMessage unavailable = choose(client, "OpenFile", {},
		[&](QDialog *dialog, const QString &) {
			auto *list = dialog->findChild<QListWidget *>();
			const auto items = list ? list->findItems("Offline Server", Qt::MatchExactly) :
				QList<QListWidgetItem *>{};
			check(items.size() == 1, "an unavailable server remains on the location screen");
			if (!items.isEmpty()) {
				list->setCurrentItem(items.front());
				QMetaObject::invokeMethod(dialog, "accept");
				check(dialog->isVisible(), "an unavailable alias does not fall back to Home");
				bool error = false;
				for (const QLabel *label : dialog->findChildren<QLabel *>()) {
					error |= label->text().contains("Reconnect");
				}
				check(error, "unavailable aliases report a visible reconnection error");
			}
			dialog->reject();
		});
	check(unavailable.arguments().value(0).toUInt() == 1,
		"cancelling an unavailable alias returns cancellation");
	check(QDir().mkpath(serverRoot + "/Replaced") &&
		QFile::link(serverRoot + "/Replaced", desktop + "/Replaced Folder") &&
		aliasRecord(desktop + "/Replaced Folder", serverRoot + "/Replaced") &&
		QDir().mkpath(serverRoot + "/Away/a/b/c/d") &&
		QDir().rename(serverRoot + "/Replaced", serverRoot + "/Away/a/b/c/d/Original") &&
		QDir().mkpath(serverRoot + "/Replaced"),
		"create an alias with a replaced original outside the reconnection search area");
	const QDBusMessage replaced = choose(client, "OpenFile", {},
		[&](QDialog *dialog, const QString &) {
			auto *list = dialog->findChild<QListWidget *>();
			const auto items = list ? list->findItems("Replaced Folder", Qt::MatchExactly) :
				QList<QListWidgetItem *>{};
			check(items.size() == 1, "a replaced folder alias stays listed");
			if (!items.isEmpty()) {
				list->setCurrentItem(items.front());
				QMetaObject::invokeMethod(dialog, "accept");
				check(dialog->isVisible(), "a replaced folder is not silently accepted");
				bool error = false;
				for (const QLabel *label : dialog->findChildren<QLabel *>()) {
					error |= label->text().contains("replaced");
				}
				check(error, "the chooser reports a replaced original explicitly");
			}
			dialog->reject();
		});
	check(replaced.arguments().value(0).toUInt() == 1, "a replaced alias request can be cancelled");

	for (int i = 0; i < 2; ++i) {
		const QDBusMessage cancelled = choose(client, "OpenFile", {},
			[&](QDialog *, const QString &handle) {
				const QDBusMessage close = QDBusMessage::createMethodCall(service, handle,
					"org.freedesktop.impl.portal.Request", "Close");
				client.asyncCall(close);
			});
		check(cancelled.type() == QDBusMessage::ReplyMessage &&
			cancelled.arguments().value(0).toUInt() == 1,
			"Request.Close cancels the disk screen and repeated requests still work");
	}
	const QDBusMessage cancelled = choose(client, "OpenFile", options,
		[&](QDialog *, const QString &handle) {
			const QDBusMessage close = QDBusMessage::createMethodCall(service, handle,
				"org.freedesktop.impl.portal.Request", "Close");
			client.asyncCall(close);
		});
	check(cancelled.type() == QDBusMessage::ReplyMessage &&
		cancelled.arguments().value(0).toUInt() == 1,
		"Request.Close also cancels the file browser without hanging the caller");

	/* General Controls' Documents folder: Open starts there, with no list
	 * of disks first, and Other Locations... goes back to the list. */
	pl_setting_set("documents-folder", "documents");
	const QString documents = base + "/Documents";
	QDir().mkpath(documents);
	QFile letter(documents + "/letter.txt");
	check(letter.open(QIODevice::WriteOnly), "create a document in Documents");
	letter.close();
	results = successfulResults(choose(client, "OpenFile", {},
		[&](QDialog *dialog, const QString &) {
			auto *picker = qobject_cast<QFileDialog *>(dialog);
			check(picker && QFileInfo(picker->directory().absolutePath()).canonicalFilePath() == documents,
				"with Documents folder chosen, Open starts in it");
			check(picker && picker->findChild<QPushButton *>("otherLocationsButton"),
				"and offers the other locations");
			if (!picker) {
				dialog->reject();
				return;
			}
			auto *select = new QTimer(picker);
			QObject::connect(select, &QTimer::timeout, picker, [picker]() {
				if (auto *name = picker->findChild<QLineEdit *>("fileNameEdit")) {
					name->setText("letter.txt");
				}
				QMetaObject::invokeMethod(picker, "accept");
			});
			select->start(20);
		}));
	check(results.value("uris").toStringList() ==
		QStringList{ QUrl::fromLocalFile(documents + "/letter.txt").toString(QUrl::FullyEncoded) },
		"a document opened from Documents is its real file");
	bool listed = false;
	const QDBusMessage other = choose(client, "SaveFile", {},
		[&](QDialog *dialog, const QString &) {
			auto *button = dialog->findChild<QPushButton *>("otherLocationsButton");
			check(button, "Save offers the other locations too");
			auto *wait = new QTimer(&app);
			QObject::connect(wait, &QTimer::timeout, [&listed, wait]() {
				auto *list = qobject_cast<QDialog *>(QApplication::activeModalWidget());
				if (list && !qobject_cast<QFileDialog *>(list) && list->findChild<QListWidget *>()) {
					listed = true;
					wait->deleteLater();
					list->reject();
				}
			});
			wait->start(20);
			button ? button->click() : dialog->reject();
		});
	check(listed, "Other Locations... shows the list of disks");
	check(other.arguments().value(0).toUInt() == 1, "and cancelling it cancels the request");

	/* Recent: documents and folders used lately, newest first; gone ones
	 * aren't offered. Choosing one opens its folder with it selected. */
	const QString letters = base + "/Letters";
	check(QDir().mkpath(letters), "create a folder of recent documents");
	const QString report = letters + "/report.txt";
	QFile reportFile(report);
	check(reportFile.open(QIODevice::WriteOnly), "create a recent document");
	reportFile.close();
	{
		GBookmarkFile *bookmarks = g_bookmark_file_new();
		const QByteArray reportUri = QUrl::fromLocalFile(report).toEncoded();
		const QByteArray goneUri = QUrl::fromLocalFile(base + "/gone.txt").toEncoded();
		const QByteArray lettersUri = QUrl::fromLocalFile(letters).toEncoded();
		const QByteArray xbel = QFile::encodeName(base + "/data/recently-used.xbel");
		g_bookmark_file_set_mime_type(bookmarks, reportUri.constData(), "text/plain");
		g_bookmark_file_set_mime_type(bookmarks, goneUri.constData(), "text/plain");
		g_bookmark_file_set_mime_type(bookmarks, lettersUri.constData(), "inode/directory");
		for (const auto &[uri, when] : { std::pair{ lettersUri, 1000 }, { reportUri, 3000 }, { goneUri, 4000 } }) {
			/* GLib drops bookmarks no program registered. */
			g_bookmark_file_add_application(bookmarks, uri.constData(), "test", "true %u");
			GDateTime *at = g_date_time_new_from_unix_utc(when);
			g_bookmark_file_set_modified_date_time(bookmarks, uri.constData(), at);
			g_date_time_unref(at);
		}
		QDir().mkpath(base + "/data");
		check(g_bookmark_file_to_file(bookmarks, xbel.constData(), nullptr), "write recently-used.xbel");
		g_bookmark_file_free(bookmarks);
	}
	check(fileChooserRecentItems() == QStringList{ report, letters },
		"Recent lists documents and folders used lately, newest first, without gone ones");

	bool reopened = false;
	results = successfulResults(choose(client, "OpenFile", {},
		[&](QDialog *dialog, const QString &) {
			auto *button = dialog->findChild<QPushButton *>("recentButton");
			check(button && button->menu(), "Open offers a Recent pop-up");
			if (!button || !button->menu()) {
				dialog->reject();
				return;
			}
			auto *wait = new QTimer(&app);
			QObject::connect(wait, &QTimer::timeout, [&reopened, wait, letters]() {
				auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
				/* (the new dialog may sit where the old one was: same address) */
				if (picker &&
						QFileInfo(picker->directory().absolutePath()).canonicalFilePath() == letters) {
					reopened = true;
					wait->deleteLater();
					QMetaObject::invokeMethod(picker, "accept");
				}
			});
			wait->start(20);
			emit button->menu()->aboutToShow();
			for (QAction *action : button->menu()->actions()) {
				if (action->text() == "report.txt") {
					action->trigger();
					return;
				}
			}
			check(false, "the Recent pop-up lists the recent document");
			dialog->reject();
		}));
	check(reopened, "choosing a recent document opens its folder");
	check(results.value("uris").toStringList() ==
		QStringList{ QUrl::fromLocalFile(report).toString(QUrl::FullyEncoded) },
		"with the document selected, ready to open");

	/* A folder dragged onto the dialog opens there. */
	bool dropped = false;
	const QDBusMessage drop = choose(client, "SaveFile", {},
		[&](QDialog *dialog, const QString &) {
			auto *wait = new QTimer(&app);
			QObject::connect(wait, &QTimer::timeout, [&dropped, wait, letters]() {
				auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
				/* (the new dialog may sit where the old one was: same address) */
				if (picker &&
						QFileInfo(picker->directory().absolutePath()).canonicalFilePath() == letters) {
					dropped = true;
					wait->deleteLater();
					picker->reject();
				}
			});
			wait->start(20);
			QMimeData mime;
			mime.setUrls({ QUrl::fromLocalFile(letters) });
			QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(dialog, &enter);
			check(enter.isAccepted(), "the dialog takes a dragged folder");
			QDropEvent dropEvent(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
			QApplication::sendEvent(dialog, &dropEvent);
		});
	check(dropped && drop.arguments().value(0).toUInt() == 1,
		"a folder dropped on the dialog opens there");
	pl_setting_set("documents-folder", nullptr);
	return failures ? 1 : 0;
}
