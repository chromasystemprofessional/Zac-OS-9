#include "fonts.h"
#include "fileops.h"
#include "finder.h"
#include "vfs.h"
#include "alert.h"

#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <cstring>

#define private public
#define protected public
#include "folderwindow.h"
#undef protected
#undef private

static int failures = 0;

static void check(bool ok, const char *message) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << message << '\n';
	failures += !ok;
}

static bool waitFor(const std::function<bool()> &ready, int timeout = 5000) {
	QElapsedTimer timer;
	timer.start();
	do {
		QApplication::processEvents(QEventLoop::AllEvents, 10);
		if (ready()) {
			return true;
		}
	} while (timer.elapsed() < timeout);
	return false;
}

static void writeFile(const QString &path, const QByteArray &data) {
	QFile file(path);
	check(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "write fixture");
}

static QByteArray readFile(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

static QByteArray collection(const QByteArray &first, const QByteArray &second) {
	QByteArray result;
	QDataStream header(&result, QIODevice::WriteOnly);
	header << quint32(0x74746366) << quint32(0x00010000) << quint32(2) <<
		quint32(20) << quint32(20 + first.size());
	for (QByteArray font : { first, second }) {
		const quint32 offset = result.size();
		QDataStream reader(font);
		reader.skipRawData(4);
		quint16 tables;
		reader >> tables;
		QDataStream writer(&font, QIODevice::ReadWrite);
		for (int i = 0; i < tables; ++i) {
			reader.device()->seek(12 + i * 16 + 8);
			quint32 tableOffset;
			reader >> tableOffset;
			writer.device()->seek(12 + i * 16 + 8);
			writer << quint32(offset + tableOffset);
		}
		result += font;
	}
	return result;
}

static bool sameMask(const plat_text *a, const plat_text *b) {
	return a && b && a->width == b->width && a->height == b->height &&
		a->baseline == b->baseline &&
		std::memcmp(a->mask, b->mask, a->stride * a->height) == 0;
}

static Item *fontItem(FolderWindow *window, const QString &file, int index = 0) {
	for (const auto &item : window->m_items) {
		if (item->fontFile == file && item->fontFaceIndex == index) {
			return item.get();
		}
	}
	return nullptr;
}

static void answerAlerts(QTimer &timer, bool accept, int *count = nullptr) {
	timer.setInterval(1);
	QObject::connect(&timer, &QTimer::timeout, [accept, count] {
		for (QWidget *widget : QApplication::topLevelWidgets()) {
			if (auto *alert = dynamic_cast<Alert *>(widget); alert && alert->isVisible()) {
				if (count) {
					++*count;
				}
				alert->done(accept ? QDialog::Accepted : QDialog::Rejected);
			}
		}
	});
	timer.start();
}

int main(int argc, char **argv) {
	QTemporaryDir temporary;
	const QString home = temporary.path();
	qputenv("HOME", home.toUtf8());
	qputenv("XDG_DATA_HOME", (home + "/data").toUtf8());
	qputenv("XDG_CONFIG_HOME", (home + "/config").toUtf8());
	qputenv("XDG_CACHE_HOME", (home + "/cache").toUtf8());
	qputenv("XDG_RUNTIME_DIR", home.toUtf8());
	qputenv("FONTCONFIG_FILE", (home + "/fonts.conf").toUtf8());
	const QString cacheProgram = QStandardPaths::findExecutable("fc-cache");
	const QByteArray sans = readFile("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
	const QByteArray serif = readFile("/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf");
	check(!sans.isEmpty() && !serif.isEmpty(), "redistributable DejaVu test fonts are installed");
	if (sans.isEmpty() || serif.isEmpty()) {
		return 1;
	}
	for (const char *directory : { "/system", "/input", "/out", "/bin", "/cache", "/config" }) {
		check(QDir().mkpath(home + directory), "create isolated fixture directory");
	}
	writeFile(home + "/system/System.ttf", sans);
	writeFile(home + "/fonts.conf", ("<?xml version=\"1.0\"?><!DOCTYPE fontconfig SYSTEM \"urn:fontconfig:fonts.dtd\">"
		"<fontconfig><dir>" + home + "/system</dir><dir>" + userFontsPath() +
		"</dir><cachedir>" + home + "/cache/fontconfig</cachedir></fontconfig>").toUtf8());
	writeFile(home + "/bin/fc-cache", "#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"$HOME/cache-calls\"\n"
		"if [ -f \"$HOME/cache-fail\" ]; then echo 'cache failure' >&2; exit 1; fi\n");
	QFile::setPermissions(home + "/bin/fc-cache",
		QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	qputenv("PATH", (home + "/bin:" + QString::fromLocal8Bit(qgetenv("PATH"))).toUtf8());
	QApplication application(argc, argv);
	const QString fontsPath = vfsPathFor("system-folder/fonts");
	QDir().mkpath(QFileInfo(vfsRegistryPath()).absolutePath());
	writeFile(vfsRegistryPath(), R"({"version":13,"nodes":[
		{"id":"system-folder","name":"System Folder","kind":"folder"},
		{"id":"system-folder/fonts","name":"My Fonts","kind":"backed","backing":"$XDG_DATA_HOME/fonts"}
	],"overrides":{}})");
	check(vfsIsFonts(fontsPath) && vfsName(fontsPath) == "My Fonts" &&
		vfsOpensAs(fontsPath).isEmpty() && vfsAcceptsDrops(fontsPath),
		"existing Fonts mapping migrates without losing its name or virtual identity");
	FolderWindow *window = FolderWindow::open(fontsPath);
	check(fontItem(window, home + "/system/System.ttf") != nullptr,
		"Fonts lists available system fonts, not just the writable directory");
	check(!fontCanRemove(home + "/system/System.ttf"), "system fonts are protected");
	QString error;
	const QString incoming = home + "/input/unrelated-filename.ttf";
	writeFile(incoming, sans);
	const auto inspected = fontInspect(incoming, &error);
	check(inspected.size() == 1 && inspected.front().name.contains("DejaVu Sans"),
		"font names come from font metadata, not filenames");
	QMimeData incomingMime;
	incomingMime.setUrls({ QUrl::fromLocalFile(incoming) });
	QDragEnterEvent enter(QPoint(200, 120), Qt::MoveAction | Qt::CopyAction,
		&incomingMime, Qt::LeftButton, Qt::NoModifier);
	window->dragEnterEvent(&enter);
	check(enter.isAccepted(), "the virtual Fonts window accepts a file drag");
	QDropEvent install(QPointF(200, 120), Qt::MoveAction | Qt::CopyAction,
		&incomingMime, Qt::LeftButton, Qt::NoModifier);
	window->dropEvent(&install);
	const QString installed = userFontsPath() + "/unrelated-filename.ttf";
	check(install.isAccepted() && install.dropAction() == Qt::MoveAction &&
		QFileInfo::exists(installed) && !QFileInfo::exists(incoming),
		"dropping a font into Fonts installs it by moving the file");
	check(waitFor([&] { return fontItem(window, installed) &&
		!readFile(home + "/cache-calls").isEmpty(); }),
		"installation refreshes both the cache and the open Fonts window");
	Item *item = fontItem(window, installed);
	if (!item) {
		return 1;
	}
	const QString installedId = item->path;
	check(item->visibleName() == inspected.front().name && !vfsCanRename(item->path),
		"installed faces have intrinsic names that cannot be renamed");
	Text own = item->renderName(400);
	Text bitmap(item->visibleName(), 400, PL_FONT_VIEWS);
	check(!sameMask(own.t, bitmap.t), "font labels are not rendered with Finder's bitmap font");
	Text exact;
	exact.t = fontRenderName(item->visibleName(), 400, installed, 0);
	check(sameMask(own.t, exact.t), "the label uses its exact file and face");
	Text narrow = item->renderName(40);
	check(narrow.inkWidth() <= 40, "font labels respect the available label width");
	item->selected = true;
	Pixels painted(500, 100);
	paintIconLabel(&painted.c, *item, 100, 10, false);
	const auto *label = item->labelText().t;
	bool matchingInk = true;
	for (int y = 0; y < label->height; ++y) {
		for (int x = 0; x < label->width; ++x) {
			if (label->mask[y * label->stride + x]) {
				matchingInk &= painted.img.pixel(116 - item->labelText().inkWidth() / 2 +
					x - label->ink_l, 45 + y) == C_WHITE;
			}
		}
	}
	check(matchingInk, "painted icon labels contain the exact font's glyph mask");
	check(iconItemRect(*item, 10, 10).height() >= PL_ICON_LARGE + own.t->height,
		"icon bounds include the actual font label height");
	window->setViewMode(FolderWindow::ViewMode::List);
	check(window->selectionRect(item).height() >= 13, "list selection uses the font label's dimensions");
	const QImage listImage = window->grab().toImage();
	check(!listImage.isNull(), "the Fonts list view renders offscreen");
	window->setViewMode(FolderWindow::ViewMode::Icons);
	auto options = window->viewOptions();
	options.icons.size = IconSize::Small;
	window->setViewOptions(options);
	check(!window->grab().isNull() && window->smallLabelBox(*item).height() >= own.t->height,
		"small icon labels retain the font's full height");
	window->setViewMode(FolderWindow::ViewMode::Buttons);
	bool onButton = true;
	const QRect button = window->buttonRect(item);
	const auto *buttonText = item->shownLabel().t;
	const QPoint labelPoint(button.center().x(), button.bottom() + 3 + buttonText->height);
	check(!window->grab().isNull() && window->buttonAt(labelPoint, &onButton) == item && !onButton,
		"button view font labels use their actual height for painting and hit testing");
	window->setViewMode(FolderWindow::ViewMode::Icons);
	std::unique_ptr<QMimeData> mime(itemDragMime({ item }));
	check(mime->hasFormat(FONT_ITEMS_MIME) && mime->hasUrls() &&
		!mime->hasFormat(ICON_MOVE_MIME) && !mime->hasFormat(ALIAS_ITEMS_MIME),
		"font drags carry installation files instead of virtual aliases");
	QDropEvent exportCopy({}, Qt::MoveAction | Qt::CopyAction, mime.get(),
		Qt::LeftButton, Qt::AltModifier);
	dropItems(&exportCopy, nullptr, home + "/out");
	check(exportCopy.isAccepted() && exportCopy.dropAction() == Qt::CopyAction &&
		QFileInfo::exists(installed) && QFileInfo::exists(home + "/out/unrelated-filename.ttf"),
		"Option-drag exports a copy without uninstalling");
	QTemporaryDir otherDisk("/dev/shm/zacos-font-test-XXXXXX");
	if (otherDisk.isValid() && QStorageInfo(otherDisk.path()).device() != QStorageInfo(installed).device()) {
		QDropEvent crossDisk({}, Qt::MoveAction | Qt::CopyAction, mime.get(),
			Qt::LeftButton, Qt::NoModifier);
		dropItems(&crossDisk, nullptr, otherDisk.path());
		check(crossDisk.isAccepted() && crossDisk.dropAction() == Qt::CopyAction &&
			QFileInfo::exists(installed) &&
			QFileInfo::exists(otherDisk.filePath("unrelated-filename.ttf")),
			"cross-disk dragging exports a copy and leaves the font installed");
	}
	QFile::remove(home + "/out/unrelated-filename.ttf");
	QDropEvent uninstall({}, Qt::MoveAction | Qt::CopyAction, mime.get(),
		Qt::LeftButton, Qt::NoModifier);
	dropItems(&uninstall, nullptr, home + "/out");
	check(uninstall.isAccepted() && !QFileInfo::exists(installed) &&
		QFileInfo::exists(home + "/out/unrelated-filename.ttf"),
		"moving a font out uninstalls it without deleting the exported file");
	check(waitFor([&] { return !fontItem(window, installed); }),
		"the open catalog removes an uninstalled font");
	writeFile(incoming, sans);
	QDropEvent copyInstall({}, Qt::MoveAction | Qt::CopyAction, &incomingMime,
		Qt::LeftButton, Qt::AltModifier);
	dropItems(&copyInstall, nullptr, fontsPath);
	check(copyInstall.isAccepted() && QFileInfo::exists(installed) && QFileInfo::exists(incoming),
		"Option-drag installs a copy and preserves the incoming package");
	const QString copyOnly = home + "/input/CopyOnly.ttf";
	writeFile(copyOnly, serif);
	QMimeData copyOnlyMime;
	copyOnlyMime.setUrls({ QUrl::fromLocalFile(copyOnly) });
	QDropEvent copyOnlyInstall({}, Qt::CopyAction, &copyOnlyMime, Qt::LeftButton, Qt::NoModifier);
	dropItems(&copyOnlyInstall, nullptr, fontsPath);
	check(copyOnlyInstall.isAccepted() && copyOnlyInstall.dropAction() == Qt::CopyAction &&
		QFileInfo::exists(copyOnly) && QFileInfo::exists(userFontsPath() + "/CopyOnly.ttf"),
		"a copy-only external source is never moved or deleted during installation");
	check(waitFor([&] { return fontItem(window, installed) != nullptr; }) &&
		fontItem(window, installed)->path == installedId, "font face identities survive reinstalling");
	const QString collectionFile = userFontsPath() + "/Collection.ttc";
	writeFile(collectionFile, collection(sans, serif));
	vfsFontsChanged();
	check(fontInspect(collectionFile, &error).size() == 2, "a real collection exposes both font faces");
	Item *first = fontItem(window, collectionFile, 0);
	Item *second = fontItem(window, collectionFile, 1);
	check(first && second, "Fonts lists every collection face");
	if (!first || !second) {
		return 1;
	}
	std::unique_ptr<QMimeData> collectionMime(itemDragMime({ first, second }));
	check(collectionMime->urls().size() == 1, "dragging several faces transfers their collection once");
	Text face0, face1;
	face0.t = fontRenderName("Preview", 200, collectionFile, 0);
	face1.t = fontRenderName("Preview", 200, collectionFile, 1);
	check(!sameMask(face0.t, face1.t), "collection labels render the selected face, not just the family");
	{
		QTimer dismiss;
		answerAlerts(dismiss, false);
		trashFontFiles({ collectionFile });
		check(QFileInfo::exists(collectionFile), "canceling collection removal keeps every face installed");
	}
	{
		QTimer dismiss;
		answerAlerts(dismiss, true);
		trashFontFiles({ collectionFile, collectionFile });
		check(!QFileInfo::exists(collectionFile), "confirmed collection removal moves its file only once");
	}
	check(waitFor([&] { return !fontItem(window, collectionFile); }),
		"all collection faces disappear after removal");
	QProcess realCache;
	realCache.start(cacheProgram, { "-f", userFontsPath() });
	check(waitFor([&] { return realCache.state() == QProcess::NotRunning; }) &&
		realCache.exitStatus() == QProcess::NormalExit && realCache.exitCode() == 0,
		"the real fc-cache accepts the refresh command in the isolated font environment");
	{
		QTimer dismiss;
		answerAlerts(dismiss, true);
		Item *system = fontItem(window, home + "/system/System.ttf");
		std::unique_ptr<QMimeData> systemMime(itemDragMime({ system }));
		QDropEvent blocked({}, Qt::MoveAction | Qt::CopyAction, systemMime.get(),
			Qt::LeftButton, Qt::NoModifier);
		dropItems(&blocked, nullptr, home + "/out");
		check(!blocked.isAccepted() && QFileInfo::exists(home + "/system/System.ttf"),
			"moving a system font is refused even when its filesystem permissions allow it");
		QDropEvent copied({}, Qt::MoveAction | Qt::CopyAction, systemMime.get(),
			Qt::LeftButton, Qt::AltModifier);
		dropItems(&copied, nullptr, home + "/out");
		check(copied.isAccepted() && QFileInfo::exists(home + "/out/System.ttf"),
			"protected fonts can still be exported as copies");
		writeFile(home + "/input/Broken.ttf", "not a font");
		check(transferItems({ home + "/input/Broken.ttf" }, userFontsPath(), true).isEmpty() &&
			!QFileInfo::exists(userFontsPath() + "/Broken.ttf"),
			"invalid font packages are rejected before transfer");
		writeFile(incoming, serif);
		check(transferItems({ incoming }, userFontsPath(), true).isEmpty() &&
			readFile(installed) == sans && readFile(incoming) == serif,
			"installation conflicts preserve both the installed font and the source");
		check(QFile::link(home + "/system", userFontsPath() + "/escape"),
			"create a nested directory-link fixture");
		check(!fontUserDirectory(userFontsPath() + "/escape") &&
			!fontCanRemove(userFontsPath() + "/escape/System.ttf"),
			"nested directory links cannot make protected fonts uninstallable");
	}
	for (const auto &entry : window->m_items) {
		entry->selected = entry->fontFile == installed;
	}
	Finder::instance().setFront(window);
	Finder::instance().moveSelectionToTrash();
	check(!QFileInfo::exists(installed), "Move To Trash uninstalls the selected user font");
	check(waitFor([&] { return !fontItem(window, installed); }),
		"menu removal refreshes the catalog");
	FolderWindow *trash = FolderWindow::open(trashFilesPath());
	trash->selectByName("unrelated-filename.ttf");
	Finder::instance().setFront(trash);
	Finder::instance().putAway();
	check(QFileInfo::exists(installed) && waitFor([&] { return fontItem(window, installed) != nullptr; }),
		"Put Away restores a trashed font and refreshes the installed catalog");
	trash->close();
	int notifications = 0;
	const int token = vfsOnChange([&] { ++notifications; });
	const int previous = notifications;
	check(QFile::remove(installed), "remove a user font outside Finder");
	check(waitFor([&] { return notifications > previous && !fontItem(window, installed); }),
		"external font removal refreshes the open catalog");
	check(QDir().mkpath(userFontsPath() + "/Nested"), "create a nested font installation directory");
	const QString nestedFont = userFontsPath() + "/Nested/Nested.ttf";
	writeFile(nestedFont, sans);
	check(waitFor([&] { return fontItem(window, nestedFont) != nullptr; }),
		"external font installation in a new subdirectory refreshes the catalog");
	check(fontCanRemove(nestedFont), "user-owned fonts in nested installation directories can be removed");
	writeFile(home + "/cache-fail", "fail");
	{
		int alerts = 0;
		QTimer dismiss;
		answerAlerts(dismiss, true, &alerts);
		refreshUserFontCache(userFontsPath());
		check(waitFor([&] { return alerts > 0; }), "font-cache failures are reported explicitly");
	}
	QFile::remove(home + "/cache-fail");
	vfsOffChange(token);
	window->close();
	waitFor([] { return QCoreApplication::instance()->findChildren<QProcess *>().isEmpty(); });
	return failures ? 1 : 0;
}
