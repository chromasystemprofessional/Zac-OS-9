#pragma once

#include <QHash>
#include <QPoint>
#include <QStringList>
#include <QTimer>
#include <QWidget>
#include <memory>
#include <vector>

#include "findcriteria.h"
#include "items.h"
#include "panelkit.h"
#include "pixels.h"
#include "searchsites.h"

class BloodhoundSearch;
class QContextMenuEvent;
class QDragEnterEvent;
class QDropEvent;

/*
 * Bloodhound (Search): Sherlock 2's channels in one window. The channel bar
 * chooses Files (the scope, criteria and result list), a group of Internet
 * search sites, the accounts on this computer, or ZacOS Help. The engine is
 * the Sniffer's Find (search.h, findcriteria.*, findindex.*), and results
 * open and reveal through zacos9-finder-ctl.
 */
class BloodhoundWindow : public QWidget {
public:
	explicit BloodhoundWindow(const QString &folder = QString());
	~BloodhoundWindow() override;

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void mouseDoubleClickEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;
	void contextMenuEvent(QContextMenuEvent *) override;
	void dragEnterEvent(QDragEnterEvent *) override;
	void dropEvent(QDropEvent *) override;
	void leaveEvent(QEvent *) override;

private:
	/* Sherlock 2's channels, as a row of icons along the top. Files is
	 * the local search; Sites are groups of Internet Search Sites (one
	 * group each, or every site for Internet); People is the account
	 * list; ZacOS searches Help. */
	enum class ChannelKind { Files, Sites, People, Help };
	struct Channel {
		QString id;
		QString label;
		pl_icon_kind icon = PL_ICON_CHANNEL_FILES;
		ChannelKind kind = ChannelKind::Sites;
		bool builtin = true;
	};
	struct ChannelTile {
		QString id;
		QRect rect;
	};

	struct CriterionRow;
	struct Snippet {
		QString text;
		QStringList words;
		bool asked = false;
	};

	void search(bool remember = true);
	void stop();
	void addResults(const QStringList &paths);
	void finished(const QString &problem);
	void findSimilar();
	FindQuery query() const;

	void addCriterion(FindField field = FindField::Modified);
	void removeCriterion();
	void setCriterionField(CriterionRow &row, FindField field);
	void relayout();
	void saveSearch();
	void searchHelp();
	void fillPeople();
	void showChannelMenu(const QPoint &at);
	const Channel *channelById(const QString &id) const;
	const Channel *currentChannel() const;
	QString channelLabel(const QString &id) const;
	bool filesChannel() const;
	bool sitesChannel() const;
	bool peopleChannel() const;
	bool helpChannel() const;
	bool channelTileAt(const QPoint &pos, QString *id) const;
	void showSavedSearches();
	void indexSelection();
	void showIndexMenu();
	void indexRemoval();
	void indexStatusReport();
	void indexRebuildConfirm();
	void applyQuery(const FindQuery &q);
	void rebuildScope(FindScope scope = FindScope::AllDisks);
	void reloadSites();
	void rebuildChannels();
	void chooseChannel(const QString &name);
	void updateSitePopup();
	QList<SearchSite> sitesForChannel(const QString &name) const;
	bool hasContentsRow() const;
	void liveSearchSoon();
	void runLiveSearch();
	void loadSettings();
	void saveSetting(const QString &key, const QString &value);
	void loadRecents();
	void saveRecents();
	void rememberRecent(const QString &words);
	void showRecents();
	void installSiteFile(const QString &path);
	void searchFolder(const QString &path);
	void paintPreview(pl_canvas *c) const;
	int rowHeight() const;
	void requestSnippet(const QString &path);
	void startNextSnippet();
	void paintSnippet(pl_canvas *c, const Snippet &snippet, int x, int baseline, int width) const;

	std::vector<Item *> selectedItems() const;
	void selectOnly(Item *item);
	void moveSelection(int step);
	void openSelection();
	void revealSelection();
	void getInfo();
	void copyPaths();
	void runFinderCtl(const QStringList &args);

	int listTop() const;
	int paneTop() const;
	int listHeight() const;
	pl_scrollbar bar() const;
	void scrollTo(int y);
	Item *itemAt(QPoint pos) const;

	QString m_folder;
	PanelHost m_host{ this };
	PanelEdit m_search;
	PanelPopup m_scope;
	PanelPopup m_site;
	PanelCheckbox m_live;
	PanelButton m_find, m_stop;
	PanelButton m_more, m_fewer, m_saveSearch, m_savedSearches, m_indexSelection;
	PanelButton m_recents, m_preview;
	QTimer m_liveTimer;
	QStringList m_recentsList;
	QString m_status;
	bool m_previewOn = false;
	QString m_channelName = "Files";
	QStringList m_channels;
	std::vector<Channel> m_channelDefs;
	std::vector<ChannelTile> m_channelTiles;
	QRect m_channelMore;
	QString m_hoverChannel;
	bool m_barFocus = false;
	int m_barIndex = 0;
	QList<SearchSite> m_sites;
	std::vector<std::unique_ptr<CriterionRow>> m_rows;
	QPoint m_pressPos;
	Item *m_pressItem = nullptr;
	bool m_listFocus = false;
	bool m_dragging = false;
	int m_scrollY = 0;
	enum sb_part m_sbPart = SB_NONE;
	int m_thumbGrab = 0;
	QString m_problem;
	std::vector<std::unique_ptr<Item>> m_items;
	BloodhoundSearch *m_worker = nullptr;
	bool m_searching = false;
	QStringList m_contentWords;
	QHash<QString, Snippet> m_snippets;
	QStringList m_snippetQueue;
	int m_snippetRequests = 0;
	bool m_snippetRunning = false;
	quint64 m_generation = 0;
	QString m_similarTo;
	int m_resultsTop = 100;
};
