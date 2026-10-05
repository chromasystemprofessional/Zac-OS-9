#pragma once

#include <QRect>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

#include "panelkit.h"

/*
 * The Appearance control panel, after Mac OS 8's (HIG figure 6-1): a tab
 * control with Color (accent and highlight colours), Desktop (patterns),
 * Wallpaper (photos and placement) and Sound (alert sound). Choices apply
 * at once and are kept in
 * ~/.config/zacos9/desktop.conf, which the shell watches.
 * TODO: the layout inside the panes is ours; the HIG shows only "Options".
 */
class AppearancePanel : public QWidget {
public:
	AppearancePanel();
	/* 0 Color, 1 Desktop patterns, 2 Wallpaper, 3 Sound. */
	void showTab(int tab);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	std::vector<PanelList *> visibleLists();
	void paintColorTab(pl_canvas *c);
	void paintDesktopTab(pl_canvas *c);
	void paintSoundTab(pl_canvas *c);
	void loadPatterns();
	void loadWallpapers();
	void paintWallpaperTab(pl_canvas *c);

	int m_tab = 0;
	std::vector<std::unique_ptr<Text>> m_tabLabels;
	PanelList m_accents, m_highlights, m_patterns, m_wallpapers, m_sounds;
	PanelPopup m_placement;
	PanelList *m_focus = nullptr;
	QStringList m_soundIds;
	std::vector<uint32_t> m_highlightColors;
};
