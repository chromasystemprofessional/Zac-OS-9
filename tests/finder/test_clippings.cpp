/* Clippings (shell/files/clippings.cpp): text, a picture and a link dropped
 * on a folder window from another program become clipping files where they
 * were dropped; dragged out again they carry what they hold; opened they
 * show it; Edit > Show Clipboard follows the clipboard. */
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUrl>

#include "clippings.h"
#include "fileops.h"
#include "items.h"

#define private public
#define protected public
#include "folderwindow.h"
#undef protected
#undef private

static int fails;
static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static void settle(int ms = 150) {
	QElapsedTimer t;
	t.start();
	while (t.elapsed() < ms) {
		QCoreApplication::processEvents();
		QThread::msleep(5);
	}
}

static QByteArray read(const QString &path) {
	QFile f(path);
	return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

/* Drops `mime` at `at` (window coordinates), as from another program. */
static void drop(FolderWindow *w, QMimeData *mime, QPoint at) {
	QDropEvent e(QPointF(at), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
	w->dropEvent(&e);
	settle();
}

static Item *itemNamed(FolderWindow *w, const QString &name) {
	for (auto &item : w->m_items) {
		if (item->name == name) {
			return item.get();
		}
	}
	return nullptr;
}

static QWidget *window(const QString &title) {
	for (QWidget *w : QApplication::topLevelWidgets()) {
		if (w->isVisible() && w->windowTitle() == title) {
			return w;
		}
	}
	return nullptr;
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	qputenv("XDG_RUNTIME_DIR", home.path().toUtf8());
	QApplication app(argc, argv);

	// Names and kinds.
	check(clippingBaseName("  Hello there, how are you today?\nSecond line") == "Hello there, how are you",
		"a text clipping is named after its first words");
	check(clippingBaseName("a/b:c") == "abc" && clippingBaseName(" \n") == "text",
		"without slashes or colons, and \"text\" when there are no words");
	check(clippingKind("/x/Hello clipping.txt") == ClippingKind::Text &&
		clippingKind("/x/Hello clipping 3.txt") == ClippingKind::Text &&
		clippingKind("/x/picture clipping.png") == ClippingKind::Picture &&
		clippingKind("/x/Example.url") == ClippingKind::Location &&
		clippingKind("/x/notes.txt") == ClippingKind::None && clippingKind("/x/photo.png") == ClippingKind::None,
		"clippings are known by their names");
	QMimeData files;
	files.setUrls({ QUrl::fromLocalFile("/etc/hostname") });
	files.setText("/etc/hostname");
	check(!canMakeClipping(&files), "files dragged in are files, not clippings");

	const QString folder = home.path() + "/Folder";
	QDir().mkpath(folder);
	FolderWindow *w = FolderWindow::open(folder);
	w->resize(520, 360);
	w->setViewMode(FolderWindow::ViewMode::Icons);
	w->relayout();
	settle();

	// Text.
	auto *text = new QMimeData;
	text->setText("Hello there\nmore text");
	drop(w, text, QPoint(200, 120));
	const QString textPath = folder + "/Hello there clipping.txt";
	check(read(textPath) == "Hello there\nmore text", "dropped text becomes “Hello there clipping.txt”");
	Item *textItem = itemNamed(w, "Hello there clipping.txt");
	check(textItem && textItem->kind == PL_ICON_CLIPPING && textItem->kindName() == "text clipping",
		"with the clipping icon, its kind “text clipping”");
	const QPoint content = w->toContent(QPoint(200, 120));
	check(textItem && (textItem->pos - (content - QPoint(16, 16))).manhattanLength() <= 2,
		"where it was dropped");
	auto *again = new QMimeData;
	again->setText("Hello there");
	drop(w, again, QPoint(300, 120));
	check(QFile::exists(folder + "/Hello there clipping 2.txt"), "the same words again: “Hello there clipping 2.txt”");

	// A picture.
	QImage red(8, 6, QImage::Format_RGB32);
	red.fill(Qt::red);
	auto *picture = new QMimeData;
	picture->setImageData(red);
	drop(w, picture, QPoint(200, 220));
	const QString picturePath = folder + "/picture clipping.png";
	const QImage saved(picturePath);
	check(saved.size() == QSize(8, 6) && saved.pixelColor(3, 3) == QColor(Qt::red),
		"a dropped picture becomes “picture clipping.png”");
	Item *pictureItem = itemNamed(w, "picture clipping.png");
	check(pictureItem && pictureItem->kind == PL_ICON_CLIPPING && pictureItem->kindName() == "picture clipping",
		"also with the clipping icon");

	// A link, with the browser's title for it.
	auto *link = new QMimeData;
	link->setUrls({ QUrl("https://example.com/page") });
	const QString moz = "https://example.com/page\nExample Page";
	link->setData("text/x-moz-url", QByteArray(reinterpret_cast<const char *>(moz.utf16()), moz.size() * 2));
	drop(w, link, QPoint(300, 220));
	const QString linkPath = folder + "/Example Page.url";
	check(internetLocationUrl(linkPath) == "https://example.com/page" &&
		read(linkPath).startsWith("[InternetShortcut]"),
		"a dropped link becomes the Internet location “Example Page.url”");
	Item *linkItem = itemNamed(w, "Example Page.url");
	check(linkItem && linkItem->kind == PL_ICON_INTERNET_LOCATION && linkItem->kindName() == "Internet location",
		"with the Internet location icon");

	// Dragged out again, each carries what it holds as well as the file. (The
	// folder has been read again since: find them afresh.)
	textItem = itemNamed(w, "Hello there clipping.txt");
	pictureItem = itemNamed(w, "picture clipping.png");
	linkItem = itemNamed(w, "Example Page.url");
	auto carries = [](Item *item) { return std::unique_ptr<QMimeData>(itemDragMime({ item })); };
	if (textItem && pictureItem && linkItem) {
		const auto t = carries(textItem), p = carries(pictureItem), l = carries(linkItem);

		check(t->text() == "Hello there\nmore text" && t->urls() == QList<QUrl>{ QUrl::fromLocalFile(textPath) },
			"a text clipping dragged out carries its text (and is still the file to the Sniffer)");
		check(p->hasImage() && qvariant_cast<QImage>(p->imageData()).size() == QSize(8, 6),
			"a picture clipping carries its picture");
		check(l->text() == "https://example.com/page", "an Internet location carries its address");
	}

	// A link already where a clipping would go, even one to nothing, is
	// stepped over, never written through: nothing is made outside.
	const QString drop2 = home.path() + "/Drop";
	QDir().mkpath(drop2);
	QFile::link(home.path() + "/outside.txt", drop2 + "/hello clipping.txt");
	QFile::link(home.path() + "/outside.png", drop2 + "/picture clipping.png");
	QFile::link(home.path() + "/outside.url", drop2 + "/Example Page.url");
	QString err;
	QMimeData hello;
	hello.setText("hello");
	const QStringList madeText = makeClippings(&hello, drop2, &err);
	QMimeData pic;
	pic.setImageData(red);
	const QStringList madePic = makeClippings(&pic, drop2, &err);
	QMimeData url;
	url.setUrls({ QUrl("https://example.com/page") });
	url.setData("text/x-moz-url", QByteArray(reinterpret_cast<const char *>(moz.utf16()), moz.size() * 2));
	const QStringList madeUrl = makeClippings(&url, drop2, &err);
	check(madeText == QStringList{ drop2 + "/hello clipping 2.txt" } && read(madeText.value(0)) == "hello",
		"a dangling link named like the text clipping: “hello clipping 2.txt” instead");
	check(madePic == QStringList{ drop2 + "/picture clipping 2.png" } &&
		QImage(madePic.value(0)).size() == QSize(8, 6), "and for a picture");
	check(madeUrl == QStringList{ drop2 + "/Example Page 2.url" }, "and for an Internet location");
	check(!QFile::exists(home.path() + "/outside.txt") && !QFile::exists(home.path() + "/outside.png") &&
		!QFile::exists(home.path() + "/outside.url") && QFileInfo(drop2 + "/hello clipping.txt").isSymLink(),
		"nothing is created through the links, which stay as they were");

	// Opening a clipping shows it.
	openClippingWindow(textPath);
	settle();
	QWidget *shown = window("Hello there clipping");
	auto *edit = shown ? shown->findChild<QPlainTextEdit *>() : nullptr;
	check(edit && edit->isVisible() && edit->toPlainText() == "Hello there\nmore text" && edit->isReadOnly(),
		"opening a text clipping shows its text in a window named after it");

	// Show Clipboard follows the clipboard.
	QGuiApplication::clipboard()->setText("copied words");
	showClipboardWindow();
	settle();
	QWidget *board = window("Clipboard");
	auto status = [&] {
		for (QLabel *label : board ? board->findChildren<QLabel *>() : QList<QLabel *>()) {
			if (label->text().startsWith("Clipboard contents")) {
				return label->text();
			}
		}
		return QString();
	};
	auto *boardText = board ? board->findChild<QPlainTextEdit *>() : nullptr;
	check(board && boardText && boardText->toPlainText() == "copied words" &&
		status() == "Clipboard contents: text", "Show Clipboard shows copied text");
	QGuiApplication::clipboard()->setImage(red);
	settle();
	check(status() == "Clipboard contents: picture", "and follows the clipboard to a picture");
	QGuiApplication::clipboard()->clear();
	settle();
	check(status() == "Clipboard contents: none", "and to nothing");
	return fails ? 1 : 0;
}
