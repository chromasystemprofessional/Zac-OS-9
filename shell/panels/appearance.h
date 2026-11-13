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
	/* Mac OS 9's order: 0 Themes, 1 Appearance, 2 Fonts, 3 Desktop,
	 * 4 Sound, 5 Options. */
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
	void paintThemesTab(pl_canvas *c);
	void paintAppearanceTab(pl_canvas *c);
	void paintFontsTab(pl_canvas *c);
	void paintDesktopTab(pl_canvas *c);
	void paintSoundTab(pl_canvas *c);
	void loadPatterns();
	void loadWallpapers();
	void loadAlertSounds();
	QString recordAlertSound();
	void loadThemes();
	void paintOptionsTab(pl_canvas *c);
	void openThemeEditor(int index);
	/* Options: Mac OS 9's "Double-click title bar to collapse windows"
	 * (title-collapse, read by zacos9-wm) and Smart Scrolling
	 * (smart-scrolling: lib/widgets' scroll bars and the Qt style read it;
	 * GTK 3 programs from a block in ~/.config/gtk-3.0/gtk.css). */
	void applyProgramFonts();
	PanelCheckbox m_titleCollapse, m_smartScrolling;
	int m_scrollGroupTop = 0;
	int m_fontGroupTop = 0;
	PanelPopup m_fontSize;   /* Fonts: programs' text size */
	PanelPopup m_fontFamily; /* Fonts: programs' typeface */
	QStringList m_fontFamilyIds;
	PanelCheckbox m_smoothFonts;
	void loadFontFamilies();

	int m_tab = 0;
	std::vector<std::unique_ptr<Text>> m_tabLabels;
	PanelList m_accents, m_highlights, m_patterns, m_wallpapers, m_sounds, m_themes, m_soundThemes;
	PanelButton m_previewTheme, m_saveTheme, m_addSound, m_newTheme, m_editTheme;
	PanelEdit m_themeName;
	PanelSlider m_interfaceVolume;
	PanelHost m_host{ this };
	QString m_themeError;
	PanelPopup m_placement;
	PanelList *m_focus = nullptr;
	QStringList m_soundIds;
	std::vector<uint32_t> m_highlightColors;
	/* Themes gallery: first visible card and horizontal scroll bar. */
	int m_themeFirst = 0;
	pl_scrollbar m_themeBar{};
	QRect m_galleryRect;
	QRect m_galleryBarRect;
};
