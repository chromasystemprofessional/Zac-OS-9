/*
 * The Toy Box (Mac OS 9's Launcher): its pages from bulleted folders in Toy
 * Box Items, the loose items on Applications; icons dropped on it become
 * aliases (on a topic button: in that page); button sizes; what it keeps.
 */
#include <QApplication>
#include <QDir>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMimeData>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

#include "items.h"

#define private public
#define protected public
#include "toybox.h"
#undef protected
#undef private

static int fails;

static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

static void settle(int ms = 50) {
	QElapsedTimer timer;
	timer.start();
	while (timer.elapsed() < ms) {
		QApplication::processEvents(QEventLoop::AllEvents, 10);
	}
}

static QStringList names(const std::vector<ToyBox::Page> &pages) {
	QStringList out;
	for (const auto &p : pages) {
		out << p.name;
	}
	return out;
}

static void drop(ToyBox *t, QPoint at, const QStringList &paths) {
	QMimeData mime;
	QList<QUrl> urls;
	for (const QString &p : paths) {
		urls << QUrl::fromLocalFile(p);
	}
	mime.setUrls(urls);
	QDropEvent e(QPointF(at), Qt::CopyAction | Qt::LinkAction, &mime, Qt::LeftButton, Qt::NoModifier);
	t->dropEvent(&e); /* without a drag going on, Qt won't deliver one */
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
	qputenv("XDG_DATA_HOME", (home.path() + "/data").toUtf8());
	qputenv("XDG_RUNTIME_DIR", home.path().toUtf8());
	QApplication app(argc, argv);

	const QString root = toyBoxPath();
	QDir().mkpath(root);
	check(names(ToyBox::pagesIn(root)) == QStringList{ "Applications" },
		"an empty Toy Box has the one page, Applications");
	QDir().mkpath(root + "/•Games");
	QDir().mkpath(root + "/• Work");
	check(names(ToyBox::pagesIn(root)) == QStringList({ "Games", "Work" }),
		"bulleted folders are pages, named without the bullet; no loose items, no Applications");
	const QString letter = home.path() + "/Letter.txt", photos = home.path() + "/Photos";
	QFile(letter).open(QIODevice::WriteOnly);
	QDir().mkpath(photos);
	ToyBox::addAliases({ { "Letter", letter } }, root);
	check(QFileInfo(root + "/Letter").symLinkTarget() == letter, "a button is an alias, named for its original");
	check(names(ToyBox::pagesIn(root)) == QStringList({ "Applications", "Games", "Work" }),
		"loose items make the Applications page, first");
	for (int i = 0; i < 9; i++) {
		QDir().mkpath(root + QString("/•Page %1").arg(i));
	}
	check(ToyBox::pagesIn(root).size() == 8, "eight pages at most, as the Mac's");
	for (int i = 0; i < 9; i++) {
		QDir(root + QString("/•Page %1").arg(i)).removeRecursively();
	}

	ToyBox *t = ToyBox::open();
	t->resize(420, 240);
	settle();
	check(t->m_pages.size() == 3 && t->m_page == 0 && t->m_items.size() == 1 &&
		t->m_items[0]->name == "Letter", "it opens on Applications, its loose items as buttons");

	/* Dropped on the window: an alias on the page shown; on a topic button:
	 * an alias in that page. The originals stay where they are. */
	drop(t, QPoint(200, 150), { photos });
	settle();
	check(QFileInfo(root + "/Photos").symLinkTarget() == photos && QFileInfo(photos).isDir() &&
		t->m_items.size() == 2, "a folder dropped on it becomes a button (an alias)");
	drop(t, t->topicRect(1).center(), { letter });
	settle();
	check(QFileInfo(root + "/•Games/Letter").isSymLink(), "dropped on a topic button: onto that page");
	t->showPage(1);
	check(t->m_items.size() == 1 && t->m_items[0]->name == "Letter", "and that page shows it");
	drop(t, QPoint(200, 150), { root + "/Photos" });
	settle();
	check(!QFileInfo(root + "/•Games/Photos").exists(), "a Toy Box button dropped back on it isn't copied");

	/* Button sizes, and what's kept. */
	t->setButtonSize(ToyBox::ButtonSize::Large);
	check(t->metrics().side == 76 && t->metrics().icon == 64, "Large buttons double the icons");
	check(!t->grab().isNull(), "it draws");
	t->close();
	settle();
	ToyBox *again = ToyBox::open();
	settle();
	check(again->m_size == ToyBox::ButtonSize::Large && again->m_pages[again->m_page].name == "Games",
		"it comes back with its size and page");
	again->close();
	settle();
	QTextStream(stdout) << fails << " failure(s)\n";
	return fails ? 1 : 0;
}
