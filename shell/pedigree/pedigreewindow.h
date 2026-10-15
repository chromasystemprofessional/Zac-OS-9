#pragma once

/*
 * Pedigree's window (Apple System Profiler's): tabs for System Profile
 * and Devices and Volumes, each an outline whose sections open and close
 * by their triangles. File > Save Report writes both out as text.
 */

#include <QMainWindow>
#include <QSet>

#include "profile.h"

class PedigreeWindow : public QMainWindow {
public:
	PedigreeWindow();

	/* For tests: the shown rows ("  " per level, then "label: value"),
	 * switching tabs, opening and closing a row as its triangle does, and
	 * writing the report. */
	QStringList visibleRows() const;
	void setTab(int tab);
	void toggle(int row);
	bool saveReport(const QString &path);

private:
	class View;
	friend class View;
	View *m_view;

	struct Row {
		const ProfileItem *item;
		QString key; /* the labels from the top, for remembering what's open */
		int depth;
	};
	void buildMenus();
	void reload();
	void layoutRows();
	QRect outline() const;
	int visibleCount() const;
	void scrollTo(int top);
	void ensureVisible(int row);
	void saveReportAs();
	void copySelected();

	QList<ProfileItem> m_tabs[2];
	int m_tab = 0;
	QSet<QString> m_open[2];
	QList<Row> m_rows;
	int m_selected = -1, m_top = 0;
	bool m_dragThumb = false;
	int m_dragOffset = 0;
};
