#pragma once

#include <QRect>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

#include "pixels.h"
#include "widgets.h"

/* A Platinum list box inside a panel: its rows, selection and scrolling. */
struct PanelList {
	QRect frame; /* the black frame */
	QStringList items;
	std::vector<std::unique_ptr<Text>> texts;
	pl_list state{};
	/* Called with the new row when the user picks one. */
	std::function<void(int)> picked;

	void setItems(const QStringList &items);
	void select(int row, bool notify);
	void scrollTo(int top);
	void ensureVisible(int row);
	pl_list view() const;
};

/*
 * The Appearance control panel, after Mac OS 8's (HIG figure 6-1): a tab
 * control with Color (accent and highlight colours), Desktop (patterns)
 * and Sound (alert sound). Choices apply at once and are kept in
 * ~/.config/platinum/desktop.conf, which the shell watches.
 * TODO: the layout inside the panes is ours; the HIG shows only "Options".
 */
class AppearancePanel : public QWidget {
public:
	AppearancePanel();
	/* 0 Color, 1 Desktop, 2 Sound. */
	void showTab(int tab);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	std::vector<PanelList *> visibleLists();
	void paintColorTab(pl_canvas *c);
	void paintDesktopTab(pl_canvas *c);
	void paintSoundTab(pl_canvas *c);

	int m_tab = 0;
	std::vector<std::unique_ptr<Text>> m_tabLabels;
	PanelList m_accents, m_highlights, m_patterns, m_sounds;
	PanelList *m_focus = nullptr;
	QStringList m_soundIds;
	std::vector<uint32_t> m_highlightColors;
};
