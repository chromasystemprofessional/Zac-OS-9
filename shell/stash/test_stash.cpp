/* Stash: clips are kept in order across sessions; Paste, Cut, Clear and
 * Undo; files dropped from the Sniffer. */
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>
#include <cstdio>

#include "stashwindow.h"

#define CHECK(cond) do { if (!(cond)) { \
	std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); return 1; } } while (0)

int main(int argc, char **argv) {
	QApplication app(argc, argv);
	QTemporaryDir tmp;
	CHECK(tmp.isValid());
	const QString dir = tmp.path() + "/Stash";

	QImage red(30, 20, QImage::Format_RGB32);
	red.fill(Qt::red);
	{
		Stash stash(dir);
		CHECK(stash.count() == 0);
		CHECK(stash.insertText(0, "second"));
		CHECK(stash.insertText(0, "first"));
		CHECK(stash.insertImage(5, red)); /* past the end: last */
		CHECK(!stash.insertText(0, ""));
		CHECK(!stash.insertImage(0, QImage()));
	}
	{
		Stash stash(dir);
		CHECK(stash.count() == 3);
		CHECK(stash.clip(0).text == "first" && stash.clip(1).text == "second");
		CHECK(stash.clip(2).kind == Clip::Picture && stash.clip(2).image.size() == QSize(30, 20));
		CHECK(stash.clip(2).image.pixelColor(3, 3) == QColor(Qt::red));
		const QString gone = dir + "/" + stash.clip(1).file;
		CHECK(stash.remove(1) && !QFile::exists(gone) && !stash.remove(7));
		std::unique_ptr<QMimeData> data(stash.mimeData(0));
		CHECK(data->text() == "first");
	}
	CHECK(Stash(dir).count() == 2);

	/* Files dropped in: a text document and a picture; others ignored. */
	const QString textFile = tmp.path() + "/note.txt";
	const QString pictureFile = tmp.path() + "/dot.png";
	const QString otherFile = tmp.path() + "/data.bin";
	{
		QFile f(textFile);
		CHECK(f.open(QIODevice::WriteOnly) && f.write("from a file") > 0);
		QFile o(otherFile);
		CHECK(o.open(QIODevice::WriteOnly) && o.write(QByteArray(64, '\0')) > 0);
	}
	CHECK(red.save(pictureFile));

	StashWindow window(dir);
	CHECK(window.currentPage() == 0 && window.infoText().startsWith("Text, 1K, 5 characters"));
	window.showPage(1);
	CHECK(window.infoText() == "Picture, 1K, 30 × 20 pixels" ||
		window.infoText().endsWith(", 30 × 20 pixels"));

	QMimeData files;
	files.setUrls({QUrl::fromLocalFile(textFile), QUrl::fromLocalFile(otherFile),
		QUrl::fromLocalFile(pictureFile)});
	CHECK(window.add(&files));
	CHECK(window.stash().count() == 4 && window.currentPage() == 2);
	CHECK(window.stash().clip(2).text == "from a file");
	CHECK(window.stash().clip(3).kind == Clip::Picture);
	window.undo(); /* both files go again */
	CHECK(window.stash().count() == 2 && window.currentPage() == 1);

	QMimeData nothing;
	CHECK(!window.add(&nothing));

	/* Paste goes after the page in view. */
	window.showPage(0);
	QApplication::clipboard()->setText("pasted");
	window.paste();
	CHECK(window.currentPage() == 1 && window.stash().clip(1).text == "pasted");

	/* Cut takes it to the clipboard; Undo brings it back in its place. */
	window.showPage(0);
	window.cut();
	CHECK(window.stash().count() == 2 && window.stash().clip(0).text == "pasted");
	CHECK(QApplication::clipboard()->text() == "first");
	window.undo();
	CHECK(window.stash().count() == 3 && window.stash().clip(0).text == "first");
	CHECK(window.currentPage() == 0);

	window.showPage(2);
	window.clear();
	CHECK(window.stash().count() == 2 && window.currentPage() == 1);
	CHECK(Stash(dir).count() == 2);
	return 0;
}
