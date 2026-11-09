/* Bloodhound's criteria rows (shell/bloodhound/bloodhoundwindow.cpp): the
 * search field plus More Choices rows build one FindQuery, and a saved
 * query loads back into them. No compositor: the window is only built and
 * queried, never shown. */
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <cstdio>

#define private public
#define protected public
#include "bloodhoundwindow.h"
#undef protected
#undef private

static int fails;
static void check(bool ok, const QString &what) {
	if (!ok) {
		fprintf(stderr, "FAIL  %s\n", qPrintable(what));
		fails++;
	} else {
		printf("ok    %s\n", qPrintable(what));
	}
}

static bool write(const QString &path, const QByteArray &data) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

int main(int argc, char **argv) {
	qputenv("QT_QPA_PLATFORM", "offscreen");
	QTemporaryDir sites;
	if (!sites.isValid()) {
		fprintf(stderr, "could not make a temporary site folder\n");
		return 1;
	}
	const QString systemSites = sites.filePath("system/Internet Search Sites");
	const QString userSites = sites.filePath("config/zacos9/Internet Search Sites");
	if (!QDir().mkpath(systemSites) || !QDir().mkpath(userSites) ||
			!write(systemSites + "/Test.searchsite",
				"[Search Site]\nName=Test\nURL=https://test.example/?q={query}\n"
				"Channel=Reference\n") ||
			!write(userSites + "/Mine.searchsite",
				"[Search Site]\nName=Mine\nURL=https://mine.example/?q={query}\n")) {
		fprintf(stderr, "could not write the test sites\n");
		return 1;
	}
	qputenv("ZACOS9_DATA", sites.filePath("system").toUtf8());
	qputenv("XDG_CONFIG_HOME", sites.filePath("config").toUtf8());
	QApplication app(argc, argv);
	QTemporaryDir dir;
	if (!dir.isValid()) {
		fprintf(stderr, "could not make a temporary folder\n");
		return 1;
	}
	BloodhoundWindow window(dir.path());
	check(window.m_scope.selected == 2, "the folder is the initial scope");
	if (!write(dir.filePath("report.txt"), "the harbour report\n")) {
		fprintf(stderr, "could not write the search file\n");
		return 1;
	}

	window.m_search.setText("report");
	FindQuery byName = window.query();
	check(byName.criteria.size() == 1 &&
		byName.criteria[0].field == FindField::Name &&
		byName.criteria[0].op == FindOp::Contains &&
		byName.criteria[0].text == "report", "the search field is the name row");

	FindCriterion name;
	name.field = FindField::Name;
	name.op = FindOp::Contains;
	name.text = "budget";
	FindCriterion contents;
	contents.field = FindField::Contents;
	contents.op = FindOp::Includes;
	contents.text = "harbour";
	FindCriterion size;
	size.field = FindField::Size;
	size.op = FindOp::LargerThan;
	size.text = "10";
	FindCriterion kind;
	kind.field = FindField::Kind;
	kind.op = FindOp::Is;
	kind.choice = 2;

	FindQuery saved;
	saved.scope = FindScope::Home;
	saved.criteria << name << contents << size << kind;
	window.applyQuery(saved);
	FindQuery loaded = window.query();
	check(loaded.scope == FindScope::Home, "a saved search restores its scope");
	check(loaded.criteria.size() == 4, "a saved search restores its rows");
	check(loaded.criteria[0].field == FindField::Name &&
		loaded.criteria[0].text == "budget", "its name row is the search field");
	bool hasContents = false, hasSize = false, hasKind = false;
	for (const FindCriterion &c : loaded.criteria) {
		if (c.field == FindField::Contents && c.op == FindOp::Includes &&
				c.text == "harbour") {
			hasContents = true;
		}
		if (c.field == FindField::Size && c.op == FindOp::LargerThan && c.text == "10") {
			hasSize = true;
		}
		if (c.field == FindField::Kind && c.op == FindOp::Is && c.choice == 2) {
			hasKind = true;
		}
	}
	check(hasContents && hasSize && hasKind, "its criteria keep their fields, ops and values");
	check(loaded.hasContents() && loaded.contentWords() == QStringList({ "harbour" }),
		"contents are asked for by word");
	check(window.hasContentsRow(), "the contents row is shown");

	const int top = window.m_resultsTop;
	window.addCriterion(FindField::Modified);
	check(window.m_resultsTop > top, "More Choices makes room for a row");
	check(window.query().criteria.size() == 5, "and the row joins the query");
	window.removeCriterion();
	check(window.m_resultsTop == top, "Fewer Choices puts the room back");
	check(window.query().criteria.size() == 4, "and the row leaves the query");

	window.saveSetting("live-search", "yes");
	window.loadSettings();
	check(window.m_live.on, "the live-search setting is remembered");
	FindQuery liveQuery;
	liveQuery.scope = FindScope::Folder;
	liveQuery.folder = dir.path();
	FindCriterion liveName;
	liveName.field = FindField::Name;
	liveName.op = FindOp::Contains;
	liveName.text = "report";
	liveQuery.criteria << liveName;
	window.applyQuery(liveQuery);
	window.liveSearchSoon();
	QElapsedTimer waited;
	waited.start();
	while ((window.m_searching || window.m_items.empty()) && waited.elapsed() < 5000) {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
		QThread::msleep(5);
	}
	check(!window.m_searching && window.m_items.size() == 1 &&
		window.m_items.front()->name == "report.txt",
		"Live Search searches after the pause");

	window.rememberRecent("alpha");
	window.rememberRecent("beta");
	window.rememberRecent("alpha");
	check(window.m_recentsList == QStringList({ "alpha", "beta" }),
		"recent searches are unique and newest first");
	window.loadRecents();
	check(window.m_recentsList == QStringList({ "alpha", "beta" }),
		"recent searches survive a reload");
	window.m_recentsList.clear();
	window.saveRecents();
	window.loadRecents();
	check(window.m_recentsList.isEmpty(), "and can be cleared");

	window.selectOnly(window.m_items.front().get());
	const int withoutPreview = window.paneTop();
	window.m_previewOn = true;
	check(window.paneTop() < withoutPreview, "the preview pane takes room from the list");
	window.m_previewOn = false;

	if (!QDir().mkpath(dir.filePath("other")) ||
			!write(dir.filePath("other/notes.txt"), "notes\n")) {
		fprintf(stderr, "could not make the dropped folder\n");
		return 1;
	}
	window.m_search.setText(QString());
	window.searchFolder(dir.filePath("other"));
	check(window.m_scope.selected == 2 && window.m_folder == dir.filePath("other"),
		"a dropped folder becomes the search scope");

	if (!QDir().mkpath(sites.filePath("drop")) ||
			!write(sites.filePath("drop/New.searchsite"),
				"[Search Site]\nName=New\nURL=https://new.example/?q={query}\n"
				"Channel=Reference\n")) {
		fprintf(stderr, "could not write the dropped site\n");
		return 1;
	}
	window.installSiteFile(sites.filePath("drop/New.searchsite"));
	check(window.m_channelName == "Web" && window.m_status.contains("New") &&
		window.m_sites.size() == 3, "a dropped site file is installed and offered");
	check(QFileInfo::exists(userSites + "/New.searchsite"),
		"and lands in the user's Internet Search Sites folder");

	check(window.m_channels == QStringList({ "Files", "Web", "Reference", "My Channel" }),
		"the channel bar has Files, Web, named channels and My Channel");
	window.chooseChannel("Web");
	check(!window.filesChannel() && window.m_site.items.size() == 3,
		"the Web channel offers every site");
	window.chooseChannel("Reference");
	check(window.m_site.items.size() == 2, "a named channel groups its sites");
	window.chooseChannel("My Channel");
	check(window.m_site.items.size() == 1 && window.m_site.items[0].text == "Mine",
		"My Channel holds the user's sites without a channel");
	window.chooseChannel("Files");
	check(window.filesChannel() && window.m_site.items.size() == 0,
		"Files comes back with its criteria controls");

	if (fails) {
		return 1;
	}
	printf("ok: Bloodhound's criteria rows build and reload one query\n");
	return 0;
}
