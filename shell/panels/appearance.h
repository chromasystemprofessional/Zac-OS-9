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
 * Wallpaper (photos and placement), Sound (alert sound), and Options.
 * Choices apply at once and are kept in
 * ~/.config/zacos9/desktop.conf, which the shell watches.
 * TODO: the layout inside the panes is ours; the HIG shows only "Options".
 */
/* Smart Scrolling for GTK 3 programs: adds or removes its marked block in
 * ~/.config/gtk-3.0/gtk.css, leaving the rest of that file as it is. */
void setGtkSmartScrolling(bool on);

class AppearancePanel : public QWidget {
public:
	AppearancePanel();
	/* 0 Color, 1 Desktop patterns, 2 Wallpaper, 3 Alert Sound,
	 * 4 Appearance Themes, 5 Sound Sets, 6 Options. */
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
	void loadAlertSounds();
	QString recordAlertSound();
	void paintWallpaperTab(pl_canvas *c);
	void loadThemes();
	void paintThemesTab(pl_canvas *c);
	void paintSoundThemesTab(pl_canvas *c);
	/* Options: Mac OS 9's "Double-click title bar to collapse windows"
	 * (title-collapse, read by zacos9-wm) and Smart Scrolling
	 * (smart-scrolling: lib/widgets' scroll bars and the Qt style read it;
	 * GTK 3 programs from a block in ~/.config/gtk-3.0/gtk.css). */
	void paintOptionsTab(pl_canvas *c);
	void applyProgramFonts();
	PanelCheckbox m_titleCollapse, m_smartScrolling;
	int m_scrollGroupTop = 0;
	int m_fontGroupTop = 0;
	PanelPopup m_fontSize;   /* Options: programs' text size */
	PanelCheckbox m_smoothFonts;

	int m_tab = 0;
	std::vector<std::unique_ptr<Text>> m_tabLabels;
	PanelList m_accents, m_highlights, m_patterns, m_wallpapers, m_sounds, m_themes, m_soundThemes;
	PanelButton m_previewTheme, m_saveTheme, m_addSound;
	PanelEdit m_themeName;
	PanelSlider m_interfaceVolume;
	PanelHost m_host{ this };
	QString m_themeError;
	PanelPopup m_placement;
	PanelList *m_focus = nullptr;
	QStringList m_soundIds;
	std::vector<uint32_t> m_highlightColors;
};
