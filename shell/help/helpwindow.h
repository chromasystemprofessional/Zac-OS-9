#pragma once

/*
 * The Help Center window: Back, Forward and Help Center buttons and a
 * search field above the page. Pages are the books' HTML; the Help
 * Center's front page and search results are made on the spot
 * (zacos9-help:center, zacos9-help:search?q=…). A page opened with
 * ?for=PATH is shown as that tutorial path sees it (tutorialHtml), and its
 * links keep the path. Links to the web open in the browser.
 */

#include <QMainWindow>
#include <QUrl>

class QTextBrowser;

class HelpWindow : public QMainWindow {
public:
	HelpWindow();

	/* The Help Center, a book's page (a path, or a book's folder name),
	 * or the results for some words. */
	void showCenter();
	bool showPage(const QString &pathOrBook);
	void search(const QString &words);

	/* For tests. */
	QTextBrowser *browser() const { return m_browser; }
	QString searchText() const;
	void setSearchText(const QString &text);
	void back();
	void forward();
	void follow(const QUrl &link); /* as a click on it */

private:
	class Toolbar;
	class Browser;
	Toolbar *m_toolbar;
	QTextBrowser *m_browser;
	void buildMenus();
	void updateTitle();
};
