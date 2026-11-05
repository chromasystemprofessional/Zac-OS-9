#include "appearance.h"
#include "tr.h"
#include "process.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QSaveFile>
#include <QStandardPaths>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QWheelEvent>

#include "patterns.h"
#include "custompatterns.h"
#include "customthemes.h"
#include "settings.h"

/* The window, its tab control, and the layout inside a pane following
 * the HIG's group box measurements (figure 3-29). */
static constexpr int W = 640, H = 300;
static constexpr int MARGIN = 10;
static constexpr int TABS_Y = 10;
static constexpr uint32_t FACE = GRAY(0xD);
static constexpr uint32_t PANE = GRAY(0xE);
static constexpr int PANE_TOP = TABS_Y + PL_TAB_H;     /* the pane's top line */
static constexpr int BOX_L = MARGIN + 3 + PL_GROUP_MARGIN; /* group boxes inside the pane */
static constexpr int BOX_R = W - MARGIN - 3 - PL_GROUP_MARGIN - 2;
static constexpr int BOX_TOP = PANE_TOP + 3 + 16;     /* room for the title above */
static constexpr int ITEM_TOP = BOX_TOP + 1 + PL_GROUP_MARGIN_TOP;
static constexpr int ITEM_INSET = 1 + PL_GROUP_MARGIN;
static constexpr int MID = (BOX_L + BOX_R) / 2;

static QString setting(const char *key) {
	char v[128];
	return pl_setting(key, v, sizeof(v)) ? QString::fromUtf8(v) : QString();
}

static void setSetting(const char *key, const QString &value) {
	if (!pl_setting_set(key, value.isEmpty() ? nullptr : value.toUtf8().constData())) {
		qWarning() << "Could not save appearance preference:" << key;
		return;
	}
	if (!pl_setting_set("appearance-theme", nullptr)) {
		qWarning() << "Could not clear the changed appearance preset selection.";
	}
}

/* GTK 3 programs read the theme once, so Smart Scrolling reaches them as a
 * marked block in the user's own gtk.css (new programs see it); anything
 * else there stays as it is. */
static const char *const GTK_BEGIN = "/* ZacOS 9 Smart Scrolling (Appearance > Options) */";
static const char *const GTK_END = "/* end of ZacOS 9 Smart Scrolling */";

void setGtkSmartScrolling(bool on) {
	const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/gtk-3.0/gtk.css";
	QFile old(path);
	QString css = old.open(QIODevice::ReadOnly) ? QString::fromUtf8(old.readAll()) : QString();
	old.close();
	const int begin = css.indexOf(GTK_BEGIN);
	const int end = begin >= 0 ? css.indexOf(GTK_END, begin) : -1;
	if (begin >= 0 && end >= 0) {
		int after = end + static_cast<int>(strlen(GTK_END));
		if (after < css.size() && css[after] == '\n') {
			after++;
		}
		css.remove(begin, after - begin);
	}
	if (on) {
		if (!css.isEmpty() && !css.endsWith('\n')) {
			css += '\n';
		}
		css += QString(GTK_BEGIN) + "\nscrollbar {\n"
			"\t-GtkScrollbar-has-backward-stepper: false;\n"
			"\t-GtkScrollbar-has-secondary-backward-stepper: true;\n"
			"\t-GtkScrollbar-fixed-slider-length: false;\n"
			"}\n" + GTK_END + "\n";
	}
	if (css.isEmpty() && !QFile::exists(path)) {
		return;
	}
	QDir().mkpath(QFileInfo(path).path());
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(css.toUtf8()) < 0 || !file.commit()) {
		qWarning() << "Could not update" << path;
	}
}

/* ---- the panel ---------------------------------------------------------------- */

AppearancePanel::AppearancePanel() {
	setWindowTitle(_("Appearance"));
	setFixedSize(W, H);
	for (const char *label : { "Themes", "Appearance", "Fonts", "Desktop", "Sound",
			"Options" }) {
		m_tabLabels.push_back(std::make_unique<Text>(label, 200, PL_FONT_SYSTEM));
	}
	/* Options. Not part of a theme, so it leaves the chosen one alone. */
	m_titleCollapse = PanelCheckbox("Double-click title bar to collapse windows",
		QPoint(BOX_L + ITEM_INSET, ITEM_TOP));
	m_titleCollapse.hint = "Collapses a window to its title bar when you double-click the title bar.";
	m_titleCollapse.on = setting("title-collapse") != "false";
	m_titleCollapse.toggled = [this](bool on) {
		if (!pl_setting_set("title-collapse", on ? nullptr : "false")) {
			qWarning() << "Could not save appearance preference: title-collapse";
		}
		update();
	};
	/* Smart Scrolling: below, in a group of its own (paintOptionsTab). */
	m_scrollGroupTop = m_titleCollapse.hitRect().bottom() + PL_GROUP_MARGIN + 1 + 12 + 16;
	m_smartScrolling = PanelCheckbox("Smart Scrolling",
		QPoint(BOX_L + ITEM_INSET, m_scrollGroupTop + 1 + PL_GROUP_MARGIN_TOP));
	m_smartScrolling.hint = "Puts both scroll arrows together, and sizes the scroll box to show how much of the window you can see.";
	m_smartScrolling.on = pl_smart_scrolling();
	m_smartScrolling.toggled = [this](bool on) {
		if (!pl_setting_set("smart-scrolling", on ? "true" : nullptr)) {
			qWarning() << "Could not save appearance preference: smart-scrolling";
		}
		setGtkSmartScrolling(on);
		update();
	};
	/* Fonts, for programs (ZacOS's own bitmap fonts stay as they are): GTK
	 * through gsettings, Qt through the Platinum style when it starts.
	 * Mac OS 9's Fonts tab: the typeface, its size and smoothing. */
	m_fontGroupTop = BOX_TOP;
	static const int sizes[4] = { 9, 10, 12, 14 };
	m_fontFamily.rect = QRect(BOX_L + ITEM_INSET + 170, ITEM_TOP + 8, 200, PL_POPUP_H);
	m_fontSize.rect = QRect(BOX_L + ITEM_INSET + 170, ITEM_TOP + 38, 200, PL_POPUP_H);
	loadFontFamilies();
	m_fontFamily.hint = "Sets the typeface programs use. Programs already open change when opened again.";
	m_fontFamily.chosen = [this](int row) {
		m_fontFamily.selected = row;
		pl_setting_set("font-family", row == 0 ? nullptr : m_fontFamilyIds[row].toUtf8().constData());
		applyProgramFonts();
		update();
	};
	m_fontSize.items = { PopupItem("Small (9 pt)"), PopupItem("Medium (10 pt)"), PopupItem("Large (12 pt)"),
		PopupItem("Larger (14 pt)") };
	const int size = setting("app-font-size").toInt();
	m_fontSize.selected = size >= 14 ? 3 : size >= 12 ? 2 : size == 9 ? 0 : 1;
	m_fontSize.hint = "Sets how big text is in programs. Programs already open change when opened again.";
	m_fontSize.chosen = [this](int row) {
		m_fontSize.selected = row;
		pl_setting_set("app-font-size", row == 1 ? nullptr : QByteArray::number(sizes[row]).constData());
		applyProgramFonts();
		update();
	};
	m_smoothFonts = PanelCheckbox("Smooth text in programs",
		QPoint(BOX_L + ITEM_INSET, m_fontSize.rect.bottom() + 10));
	m_smoothFonts.on = setting("font-smoothing") != "false";
	m_smoothFonts.hint = "Softens the edges of text in programs. Off: crisp, jagged text like the 1990s'.";
	m_smoothFonts.toggled = [this](bool on) {
		pl_setting_set("font-smoothing", on ? nullptr : "false");
		applyProgramFonts();
		update();
	};

	/* Accent colour. */
	m_accents.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(MID - 6 - ITEM_INSET, ITEM_TOP + 6 * PL_LIST_ROW_H + 1));
	QStringList accents;
	for (int i = 0; i < pl_accent_count(); i++) {
		accents << pl_accent_name(i);
	}
	m_accents.setItems(accents);
	m_accents.select(pl_accent_find(setting("accent").toUtf8().constData()), false);
	m_accents.picked = [this](int i) {
		setSetting("accent", i == 0 ? QString() : QString(pl_accent_id(i)));
		update();
	};

	/* Highlight colour: the accent's own light shade, or a fixed colour. */
	m_highlights.frame = QRect(QPoint(MID + 6 + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_R - ITEM_INSET, ITEM_TOP + 6 * PL_LIST_ROW_H + 1));
	const QList<QPair<QString, uint32_t>> highlights = {
		{ "Accent Color", 0 }, { "Yellow", RGB(0xFF, 0xFF, 0x00) },
		{ "Gold", RGB(0xFF, 0xCC, 0x66) }, { "Orange", RGB(0xFF, 0xCC, 0x99) },
		{ "Green", RGB(0xCC, 0xFF, 0xCC) }, { "Blue", RGB(0xCC, 0xEE, 0xFF) },
		{ "Pink", RGB(0xFF, 0xCC, 0xEE) }, { "Gray", RGB(0xCC, 0xCC, 0xCC) },
	};
	QStringList hlNames;
	const QString current = setting("highlight").toUpper();
	int hlSelected = 0;
	for (int i = 0; i < highlights.size(); i++) {
		hlNames << highlights[i].first;
		m_highlightColors.push_back(highlights[i].second);
		if (highlights[i].second &&
				QString::asprintf("%06X", highlights[i].second & 0xFFFFFF) == current) {
			hlSelected = i;
		}
	}
	m_highlights.setItems(hlNames);
	m_highlights.select(hlSelected, false);
	m_highlights.picked = [this](int i) {
		uint32_t v = m_highlightColors[i];
		setSetting("highlight", v ? QString::asprintf("%06X", v & 0xFFFFFF) : QString());
		update();
	};

	/* Desktop: patterns on the left, pictures on the right (Mac OS 9's
	 * Desktop tab shows both). */
	m_patterns.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_L + ITEM_INSET + 150, ITEM_TOP + 8 * PL_LIST_ROW_H + 1));
	loadPatterns();
	watchDesktopPatterns(this, [this] { loadPatterns(); update(); });
	m_patterns.picked = [this](int i) {
		setSetting("pattern", i == 0 ? QString() : desktopPatternId(i));
		setSetting("background", "pattern");
		update();
	};

	m_wallpapers.frame = QRect(QPoint(MID + 20, ITEM_TOP),
		QPoint(MID + 20 + 150, ITEM_TOP + 6 * PL_LIST_ROW_H + 1));
	loadWallpapers();
	watchDesktopWallpaper(this, [this] { loadWallpapers(); update(); });
	m_wallpapers.picked = [this](int i) {
		if (i < desktopWallpaperCount()) {
			setSetting("wallpaper", desktopWallpaperId(i));
			setSetting("background", "wallpaper");
			update();
		}
	};
	m_placement.rect = QRect(MID + 20, m_wallpapers.frame.bottom() + 12, 150, PL_POPUP_H);
	m_placement.label = QString();
	m_placement.items = { "Fit", "Fill", "Stretch", "Center" };
	const QStringList modes{ "fit", "fill", "stretch", "center", "tile" };
	m_placement.selected = std::max<int>(0, modes.indexOf(setting("wallpaper-mode")));
	m_placement.items = { "Fit", "Fill", "Stretch", "Center", "Tile" };
	m_placement.chosen = [this, modes](int i) {
		m_placement.selected = i;
		setSetting("wallpaper-mode", modes[i]);
		update();
	};

	/* Sound: sound sets on the left, alert sounds on the right. */
	m_soundThemes.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_L + ITEM_INSET + 150, ITEM_TOP + 6 * PL_LIST_ROW_H + 1));
	m_sounds.frame = QRect(QPoint(MID + 20, ITEM_TOP),
		QPoint(MID + 20 + 150, ITEM_TOP + 8 * PL_LIST_ROW_H + 1));
	loadAlertSounds();
	const QString chosen = setting("alert-sound");
	m_sounds.select(std::max<int>(0, m_soundIds.indexOf(chosen.isEmpty() ? "platinum" : chosen)),
		false);
	m_sounds.picked = [this](int i) {
		if (i < 0 || i >= m_soundIds.size()) {
			return;
		}
		const QString id = m_soundIds[i];
		setSetting("alert-sound", id == "platinum" ? QString() : id);
		pl_sound_play(id.toUtf8().constData());
		update();
	};
	m_addSound = PanelButton("Add Recording...", QRect(MID + 20,
		m_sounds.frame.bottom() + 12, 150, 22));
	m_addSound.hint = "Records a new alert sound with the microphone.";
	m_addSound.clicked = [this] {
		/* Mac OS 9's Add…: record one with the microphone, or use a file. */
		QMessageBox ask(QMessageBox::Question, "Add Alert Sound",
			"Record a new alert sound with the microphone, or use a sound file?",
			QMessageBox::NoButton, this);
		QPushButton *record = ask.addButton("Record…", QMessageBox::AcceptRole);
		QPushButton *file = ask.addButton("Use File…", QMessageBox::ActionRole);
		ask.addButton(QMessageBox::Cancel);
		ask.setDefaultButton(record);
		ask.exec();
		QString source;
		if (ask.clickedButton() == record) {
			source = recordAlertSound();
		} else if (ask.clickedButton() == file) {
			source = QFileDialog::getOpenFileName(this, "Add Alert Sound Recording",
				QDir::homePath(), "WAVE recordings (*.wav)");
		}
		if (source.isEmpty()) {
			return;
		}
		QString id, error;
		m_themeError.clear();
		if (!importAlertSound(source, &id, &error)) {
			m_themeError = error;
			update();
			return;
		}
		loadAlertSounds();
		const int index = m_soundIds.indexOf(id);
		m_sounds.select(index, false);
		setSetting("alert-sound", id);
		pl_sound_play(id.toUtf8().constData());
		update();
	};

	m_themes.frame = QRect(QPoint(BOX_L, ITEM_TOP + 6), QPoint(BOX_R, ITEM_TOP + 166));
	m_galleryRect = m_themes.frame;
	m_galleryBarRect = QRect(BOX_L + 1, m_galleryRect.bottom() - SB_WIDTH, m_galleryRect.width() - 2, SB_WIDTH);
	m_themeBar.vertical = false;
	m_themeBar.length = m_galleryBarRect.width();
	m_themeBar.enabled = true;
	m_themeBar.thumb = 0;
	m_themeBar.together = false;
	m_themeBar.thumb_len = 0;
	m_previewTheme = PanelButton("Preview", QRect(BOX_L + ITEM_INSET, m_soundThemes.frame.bottom() + 12, 90, 20));
	m_previewTheme.hint = "Plays the sounds of the selected sound set.";
	m_previewTheme.clicked = [this] { previewSoundTheme(m_soundThemes.state.selected); };
	m_interfaceVolume.pos = QPoint(BOX_L + ITEM_INSET, m_soundThemes.frame.bottom() + 44);
	m_interfaceVolume.width = 150;
	m_interfaceVolume.steps = 8;
	const QString volume = setting("interface-volume");
	m_interfaceVolume.value = volume.isEmpty() ? 5 : std::clamp(volume.toInt(), 0, 7);
	m_interfaceVolume.setLabels("Off", "Loud");
	m_interfaceVolume.changed = [this](int value) {
		if (!pl_setting_set("interface-volume", QByteArray::number(value).constData())) {
			m_themeError = "Could not save interface sound volume.";
		}
		update();
	};
	m_themeName.rect = QRect(BOX_R - ITEM_INSET - 170, H - 70, 170, PL_EDIT_H);
	m_themeName.hint = "The name for a theme you save.";
	m_themeName.setText("My Theme");
	m_saveTheme = PanelButton("Save Theme...", QRect(BOX_R - 110, H - 42, 110, 22));
	m_saveTheme.hint = "Saves the current settings as a theme with a name you choose.";
	m_saveTheme.clicked = [this] {
		m_themeError.clear();
		bool ok = false;
		const QString name = QInputDialog::getText(this, "Save Theme",
			"Name of the new theme:", QLineEdit::Normal, m_themeName.text, &ok);
		if (!ok) {
			update();
			return;
		}
		m_themeName.setText(name.trimmed().isEmpty() ? m_themeName.text : name.trimmed());
		saveAppearanceTheme(m_themeName.text, &m_themeError);
		update();
	};
	m_themes.picked = [this](int i) {
		m_themeError.clear();
		if (applyAppearanceTheme(i, &m_themeError)) {
			m_accents.select(pl_accent_find(setting("accent").toUtf8().constData()), false);
			const QString highlight = setting("highlight").toUpper();
			int selected = 0;
			for (size_t n = 1; n < m_highlightColors.size(); ++n) {
				if (QString::asprintf("%06X", m_highlightColors[n] & 0xFFFFFF) == highlight) {
					selected = static_cast<int>(n);
				}
			}
			m_highlights.select(selected, false);
			const QString alert = setting("alert-sound");
			m_sounds.select(std::max<int>(0, m_soundIds.indexOf(alert.isEmpty() ? "platinum" : alert)), false);
			const QStringList modes{ "fit", "fill", "stretch", "center", "tile" };
			m_placement.selected = std::max<int>(0, modes.indexOf(setting("wallpaper-mode")));
			loadPatterns();
			loadWallpapers();
			loadThemes();
			loadFontFamilies();
		}
		update();
	};
	m_soundThemes.picked = [this](int i) {
		m_themeError.clear();
		applySoundTheme(i, &m_themeError);
		update();
	};
	loadThemes();
	watchCustomThemes(this, [this] {
		loadThemes();
		loadFontFamilies();
		loadAlertSounds();
		update();
	});
	m_focus = &m_accents;
}

void AppearancePanel::loadFontFamilies() {
	m_fontFamilyIds = { QString() };
	QStringList names = { "Default" };
	for (const QString &family : themeFontFamilies()) {
		if (!m_fontFamilyIds.contains(family)) {
			m_fontFamilyIds << family;
			names << family;
		}
	}
	m_fontFamily.items.clear();
	for (const QString &name : names) {
		m_fontFamily.items.push_back(PopupItem(name));
	}
	m_fontFamily.selected = std::max<int>(0, m_fontFamilyIds.indexOf(setting("font-family")));
}

void AppearancePanel::loadAlertSounds() {
	m_soundIds = alertSoundIds();
	QStringList names;
	for (const QString &id : m_soundIds) {
		names << alertSoundDisplayName(id);
	}
	m_sounds.setItems(names);
}

void AppearancePanel::loadThemes() {
	QStringList names;
	int selected = 0;
	for (int i = 0; i < soundThemeCount(); ++i) {
		names << soundThemeName(i);
		if (soundThemeId(i) == setting("sound-theme")) {
			selected = i;
		}
	}
	m_soundThemes.setItems(names);
	m_soundThemes.select(selected, false);
	names.clear();
	selected = -1;
	for (int i = 0; i < appearanceThemeCount(); ++i) {
		names << appearanceThemeName(i);
		if (appearanceThemeId(i) == setting("appearance-theme")) {
			selected = i;
		}
	}
	m_themes.setItems(names);
	m_themes.select(selected, false);
	setToolTip("Appearance Themes: " + appearanceThemesFolder() +
		"\nSound Themes: " + soundThemesFolder() + "\n" + customThemeErrors().join('\n'));
}

void AppearancePanel::loadPatterns() {
	QStringList names;
	for (int i = 0; i < desktopPatternCount(); ++i) {
		names << desktopPatternName(i);
	}
	m_patterns.setItems(names);
	m_patterns.select(desktopPatternFind(setting("pattern")), false);
	setToolTip("Patterns: " + desktopPatternsFolder() + "\n" +
		desktopPatternErrors().join('\n') + "\nWallpaper: " + desktopWallpaperFolder() +
		"\n" + desktopWallpaperErrors().join('\n'));
}

void AppearancePanel::loadWallpapers() {
	QStringList names;
	for (int i = 0; i < desktopWallpaperCount(); ++i) {
		names << desktopWallpaperName(i);
	}
	if (names.isEmpty()) {
		names << "No wallpapers";
	}
	m_wallpapers.setItems(names);
	m_wallpapers.select(std::max(0, desktopWallpaperFind(setting("wallpaper"))), false);
	setToolTip("Patterns: " + desktopPatternsFolder() + "\n" +
		desktopPatternErrors().join('\n') + "\nWallpaper: " + desktopWallpaperFolder() +
		"\n" + desktopWallpaperErrors().join('\n'));
}

void AppearancePanel::showTab(int tab) {
	m_tab = std::clamp(tab, 0, 5);
	m_host.checks = m_tab == 2 ? std::vector<PanelCheckbox *>{ &m_smoothFonts }
		: m_tab == 5 ? std::vector<PanelCheckbox *>{ &m_titleCollapse, &m_smartScrolling }
		: std::vector<PanelCheckbox *>{};
	m_host.buttons = m_tab == 0 ? std::vector<PanelButton *>{ &m_saveTheme }
		: m_tab == 4 ? std::vector<PanelButton *>{ &m_previewTheme, &m_addSound }
		: std::vector<PanelButton *>{};
	m_host.edits = {};
	/* Hint Bubbles for this tab's lists, slider and pop-up. */
	m_host.hintAreas.clear();
	switch (m_tab) {
	case 0:
		m_host.hintAreas.push_back({ &m_galleryRect, "Saved themes: colours, pattern, picture and sounds together. Click one to use it." });
		break;
	case 1:
		m_host.hintAreas.push_back({ &m_accents.frame, "Chooses the colour of scroll bars, sliders and progress bars." });
		m_host.hintAreas.push_back({ &m_highlights.frame, "Chooses the colour of selected text." });
		break;
	case 2:
		m_host.hintAreas.push_back({ &m_fontFamily.rect, "Sets the typeface programs use. Programs already open change when opened again." });
		m_host.hintAreas.push_back({ &m_fontSize.rect, "Sets how big text is in programs. Programs already open change when opened again." });
		break;
	case 3:
		m_host.hintAreas.push_back({ &m_patterns.frame, "Chooses the pattern that covers the desktop." });
		m_host.hintAreas.push_back({ &m_wallpapers.frame, "Chooses a picture for the desktop, over its pattern." });
		m_host.hintAreas.push_back({ &m_placement.rect, "Chooses how the picture fills the screen." });
		break;
	case 4:
		m_host.hintAreas.push_back({ &m_soundThemes.frame, "Chooses the sounds menus, windows and controls make." });
		m_host.hintAreas.push_back({ &m_sounds.frame, "Chooses the sound alerts play." });
		break;
	}
	if (m_tab == 0) {
		loadThemes();
		/* Keep the selection on screen when returning to Themes. */
		const int selected = m_themes.state.selected;
		if (selected >= 0) {
			if (selected < m_themeFirst) {
				m_themeFirst = selected;
			} else if (selected >= m_themeFirst + 2) {
				m_themeFirst = selected - 1;
			}
		}
	}
	if (m_tab == 4) {
		loadThemes();
	}
	if (m_tab == 2) {
		loadFontFamilies();
	}
	const auto lists = visibleLists();
	m_focus = lists.empty() ? nullptr : lists.front();
	update();
}

std::vector<PanelList *> AppearancePanel::visibleLists() {
	switch (m_tab) {
	case 0: return { &m_themes };
	case 1: return { &m_accents, &m_highlights };
	case 2: return {};
	case 3: return { &m_patterns, &m_wallpapers };
	case 4: return { &m_sounds, &m_soundThemes };
	default: return {};
	}
}

static void paintList(pl_canvas *c, PanelList &l, bool focused) {
	l.paint(c, focused);
}

/* A group box around `inner` (its items), with the HIG's margins. */
static void groupAround(pl_canvas *c, const QRect &inner, const char *title, int right = -1) {
	Text t(title, 300, PL_FONT_SYSTEM);
	const int x1 = right >= 0 ? right : inner.right() + PL_GROUP_MARGIN + 1;
	pl_group_box_paint(c, inner.left() - ITEM_INSET, BOX_TOP, x1,
		inner.bottom() + PL_GROUP_MARGIN + 1, t.t, PANE);
}

void AppearancePanel::paintAppearanceTab(pl_canvas *c) {
	groupAround(c, m_accents.frame, "Accent Color");
	groupAround(c, m_highlights.frame, "Highlight Color");
	paintList(c, m_accents, m_focus == &m_accents);
	paintList(c, m_highlights, m_focus == &m_highlights);

	/* A sample in the chosen colours: a scroll bar and a progress bar. */
	const int top = m_accents.frame.bottom() + PL_GROUP_MARGIN + 2 + 12 + 16;
	Text sample("Sample", 200, PL_FONT_SYSTEM);
	pl_group_box_paint(c, BOX_L, top, BOX_R, top + 1 + PL_GROUP_MARGIN_TOP + 16 + PL_GROUP_MARGIN,
		sample.t, PANE);
	pl_scrollbar sb{};
	sb.length = 150;
	sb.enabled = true;
	sb_smart(&sb, 1, 3); /* a third of a page shows: Smart Scrolling's thumb */
	sb.thumb = sb_thumb_span(&sb) / 3;
	pl_scrollbar_paint(c, BOX_L + ITEM_INSET, top + 1 + PL_GROUP_MARGIN_TOP, &sb,
		pl_accent_current());
	pl_progress_paint(c, MID + 6 + ITEM_INSET, top + 1 + PL_GROUP_MARGIN_TOP + 2, 140, 0.6,
		pl_accent_current());
}

void AppearancePanel::paintFontsTab(pl_canvas *c) {
	Text title("Fonts", 300, PL_FONT_SYSTEM);
	pl_group_box_paint(c, BOX_L, BOX_TOP, BOX_R,
		m_smoothFonts.hitRect().bottom() + PL_GROUP_MARGIN + 1, title.t, PANE);
	panelText(c, "Program font:", BOX_L + ITEM_INSET, m_fontFamily.rect.top() + 15);
	m_fontFamily.paint(c);
	panelText(c, "Size of text in programs:", BOX_L + ITEM_INSET, m_fontSize.rect.top() + 15);
	m_fontSize.paint(c);
	m_host.paintControls(c, PANE);
}

void AppearancePanel::paintDesktopTab(pl_canvas *c) {
	groupAround(c, m_patterns.frame, "Patterns", MID - 6);
	groupAround(c, m_wallpapers.frame, "Pictures", BOX_R);
	paintList(c, m_patterns, m_focus == &m_patterns);
	paintList(c, m_wallpapers, m_focus == &m_wallpapers);

	/* 112x112 previews, as Mac OS 9's Desktop tab shows beside each list. */
	const int size = 112;
	const int px0 = BOX_L + ITEM_INSET + 158, py0 = ITEM_TOP + 1;
	if (px0 + size <= MID - 6) {
		pl_hline(c, px0 - 1, px0 + size, py0 - 1, GRAY(0x8));
		pl_vline(c, px0 - 1, py0 - 1, py0 + size, GRAY(0x8));
		pl_hline(c, px0, px0 + size, py0 + size, C_WHITE);
		pl_vline(c, px0 + size, py0, py0 + size, C_WHITE);
		desktopPatternFill(c, std::max(0, m_patterns.state.selected), px0, py0, px0 + size - 1,
			py0 + size - 1);
	}
	const int wx0 = MID + 20 + 158, wy0 = ITEM_TOP + 1;
	const QStringList modes{ "fit", "fill", "stretch", "center", "tile" };
	if (wx0 + size <= BOX_R) {
		pl_hline(c, wx0 - 1, wx0 + size, wy0 - 1, GRAY(8));
		pl_vline(c, wx0 - 1, wy0 - 1, wy0 + size, GRAY(8));
		pl_hline(c, wx0, wx0 + size, wy0 + size, C_WHITE);
		pl_vline(c, wx0 + size, wy0, wy0 + size, C_WHITE);
		desktopWallpaperFill(c, m_wallpapers.state.selected, modes[m_placement.selected],
			wx0, wy0, wx0 + size - 1, wy0 + size - 1);
	} else {
		desktopWallpaperFill(c, m_wallpapers.state.selected, modes[m_placement.selected],
			wx0, wy0, wx0 + size - 1, wy0 + size - 1);
	}
	m_placement.paint(c);
	panelText(c, "Placement:", MID + 20, m_placement.rect.bottom() + 14, PL_FONT_VIEWS);
	const QStringList werrors = desktopWallpaperErrors();
	const bool active = setting("background") == "wallpaper";
	Text hint2(!werrors.isEmpty() ? "Import error: " + werrors.first() :
		desktopWallpaperCount() == 0 ? "Add photos to Appearance > Wallpaper." :
		active && desktopWallpaperFind(setting("wallpaper")) < 0
			? "Selected wallpaper is missing; select a photo." :
		"Select a photo to use wallpaper.",
		BOX_R - BOX_L - 8, PL_FONT_VIEWS);
	pl_text(c, hint2.t, BOX_L + 4, H - 24, C_BLACK);
}

void AppearancePanel::paintSoundTab(pl_canvas *c) {
	groupAround(c, m_soundThemes.frame, "Sound Set", MID - 6);
	groupAround(c, m_sounds.frame, "Alert Sound", BOX_R);
	paintList(c, m_soundThemes, m_focus == &m_soundThemes);
	paintList(c, m_sounds, m_focus == &m_sounds);
	panelText(c, "Interface Volume:", BOX_L + ITEM_INSET, m_interfaceVolume.pos.y() - 4, PL_FONT_VIEWS);
	m_interfaceVolume.paint(c);
	m_host.paintControls(c, PANE);
	const QStringList errors = customThemeErrors();
	panelText(c, !m_themeError.isEmpty() ? m_themeError : !errors.isEmpty() ? errors.first()
		: "Add sets to Appearance > Sound Themes", BOX_L + 4, H - 24, PL_FONT_VIEWS, C_BLACK, BOX_R - BOX_L);
}

/* ---- Themes gallery (Mac OS 9's Themes tab) -------------------------- */

static uint32_t galleryHighlight(const QMap<QString, QString> &values, struct pl_accent accent) {
	const QString s = values.value("highlight").toUpper();
	bool ok = s.size() == 6;
	uint32_t v = 0;
	for (int i = 0; ok && i < 6; ++i) {
		const char ch = s[i].toLatin1();
		int d = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
		if (d < 0) {
			ok = false;
			break;
		}
		v = (v << 4) | (uint32_t)d;
	}
	if (!ok) {
		return accent.light;
	}
	return 0xFF000000u | ((v & 0xFF0000u)) | (v & 0x00FF00u) | (v & 0x0000FFu);
}

static struct pl_accent galleryAccent(const QMap<QString, QString> &values) {
	return pl_accent_at(pl_accent_find(values.value("accent").toUtf8().constData()));
}

static QString galleryBundleDir(const QString &bundle) {
	if (bundle.isEmpty()) {
		return QString();
	}
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes/" + bundle;
}

static void blitImage(pl_canvas *c, const QImage &img, int x, int y) {
	const QImage argb = img.convertToFormat(QImage::Format_ARGB32);
	for (int j = 0; j < argb.height(); ++j) {
		for (int i = 0; i < argb.width(); ++i) {
			const QRgb p = argb.pixel(i, j);
			if (qAlpha(p) == 0) {
				continue;
			}
			pl_put(c, x + i, y + j, p);
		}
	}
}

/* The theme's own chrome: theme.conf for the bar/button keys, and the
 * .argb tiles it names (u32le w,h, then RGBA bytes). Cached, as the
 * gallery repaints on every scroll step. */
static QMap<QString, QString> galleryThemeConf(const QString &bundleDir) {
	QMap<QString, QString> conf;
	if (bundleDir.isEmpty()) {
		return conf;
	}
	QFile f(bundleDir + "/theme.conf");
	if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return conf;
	}
	QTextStream in(&f);
	QString line;
	while (in.readLineInto(&line)) {
		const int eq = line.indexOf('=');
		if (eq <= 0) {
			continue;
		}
		conf.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
	}
	return conf;
}

static QImage galleryTile(const QString &bundleDir, const QString &rel) {
	static QMap<QString, QImage> cache;
	if (rel.isEmpty() || bundleDir.isEmpty()) {
		return QImage();
	}
	const QString path = bundleDir + "/" + rel;
	if (cache.contains(path)) {
		return cache.value(path);
	}
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		cache.insert(path, QImage());
		return QImage();
	}
	unsigned char head[8];
	if (f.read((char *)head, 8) != 8) {
		cache.insert(path, QImage());
		return QImage();
	}
	const uint32_t w = (uint32_t)head[0] | (uint32_t)head[1] << 8 |
		(uint32_t)head[2] << 16 | (uint32_t)head[3] << 24;
	const uint32_t h = (uint32_t)head[4] | (uint32_t)head[5] << 8 |
		(uint32_t)head[6] << 16 | (uint32_t)head[7] << 24;
	if (w == 0 || h == 0 || w > 512 || h > 512 || (uint64_t)w * h > 256 * 256) {
		cache.insert(path, QImage());
		return QImage();
	}
	QImage img((int)w, (int)h, QImage::Format_ARGB32);
	for (uint32_t i = 0; i < w * h; ++i) {
		unsigned char b[4];
		if (f.read((char *)b, 4) != 4) {
			cache.insert(path, QImage());
			return QImage();
		}
		img.setPixel((int)(i % w), (int)(i / w), qRgba(b[0], b[1], b[2], b[3]));
	}
	cache.insert(path, img);
	return img;
}

/* Nearest-neighbour tile blit with transparency, like decor.c's
 * chrome_blit: source (sx,sy,sw,sh) scaled to (dx,dy,dw,dh). */
static void blitTile(pl_canvas *c, const QImage &tile,
		int sx, int sy, int sw, int sh, int dx, int dy, int dw, int dh) {
	if (tile.isNull() || sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
		return;
	}
	const QImage argb = tile.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < dh; ++y) {
		const int ty = sy + y * sh / dh;
		if (ty < 0 || ty >= argb.height()) {
			continue;
		}
		const QRgb *row = reinterpret_cast<const QRgb *>(argb.constScanLine(ty));
		for (int x = 0; x < dw; ++x) {
			const int tx = sx + x * sw / dw;
			if (tx < 0 || tx >= argb.width()) {
				continue;
			}
			const QRgb p = row[tx];
			if (qAlpha(p) == 0) {
				continue;
			}
			pl_put(c, dx + x, dy + y, p);
		}
	}
}

static uint32_t galleryHex(const QString &s, uint32_t fallback) {
	QString t = s.trimmed();
	if (t.startsWith('#')) {
		t = t.mid(1);
	}
	if (t.size() != 6) {
		return fallback;
	}
	bool ok = false;
	const uint32_t v = t.toUInt(&ok, 16);
	return ok ? (0xFF000000u | v) : fallback;
}

/* The mini window's title bar in the theme's own chrome. Returns the
 * title height (22, or 38 with Aqua's blue span). */
static int paintMiniTitleBar(pl_canvas *c, int wx, int wy, int ww,
		const QMap<QString, QString> &conf, const QString &bundleDir, Text &aa) {
	const QImage barLeft = galleryTile(bundleDir, conf.value("bar-left"));
	const QImage barMiddle = galleryTile(bundleDir, conf.value("bar-middle"));
	const QImage barRight = galleryTile(bundleDir, conf.value("bar-right"));
	const QImage barSpan = galleryTile(bundleDir, conf.value("bar-span"));
	const QImage btnClose = galleryTile(bundleDir, conf.value("button-close"));
	const QImage btnZoom = galleryTile(bundleDir, conf.value("button-zoom"));
	const QImage btnGrow = galleryTile(bundleDir, conf.value("button-grow"));
	const bool hasBar = !barMiddle.isNull();
	const bool leftCluster = conf.value("title-cluster") == "left";
	const bool hasSpan = leftCluster && !barSpan.isNull();
	const int titleH = hasSpan ? 38 : 22;
	const int aaW = aa.inkWidth() > 0 ? aa.inkWidth() : 20;
	if (hasBar) {
		if (leftCluster && hasSpan) {
			const int lw = barLeft.width(), rw = barRight.width();
			const int closeX = lw >= 16 ? 12 : 4;
			const int titleX = closeX + 12 + 4;
			const int zoomX = titleX + aaW + 4;
			const int growX = zoomX + 12 + 4;
			int patchW = growX + 12 + 4 + (rw >= 12 ? 3 : rw);
			if (patchW < lw + rw) {
				patchW = lw + rw;
			}
			if (patchW > ww) {
				patchW = ww;
			}
			if (lw > 0 && !barLeft.isNull()) {
				blitTile(c, barLeft, 0, 0, lw, std::min(22, barLeft.height()), wx, wy, lw, 22);
			}
			for (int x = lw; x < patchW - rw; x += barMiddle.width()) {
				int tilew = patchW - rw - x < barMiddle.width() ? patchW - rw - x : barMiddle.width();
				blitTile(c, barMiddle, 0, 0, tilew, std::min(22, barMiddle.height()), wx + x, wy, tilew, 22);
			}
			if (rw > 0 && !barRight.isNull()) {
				blitTile(c, barRight, 0, 0, rw, std::min(22, barRight.height()), wx + patchW - rw, wy, rw, 22);
			}
			if (patchW < ww) {
				pl_hline(c, wx + patchW, wx + ww - 2, wy + 22 - 7, 0xFF525252u);
				pl_hline(c, wx + patchW, wx + ww - 3, wy + 22 - 6, 0xFFF6F6F6u);
				pl_fill(c, wx + patchW, wy + 22 - 5, wx + ww - 2, wy + 22 - 1, 0xFFDADADAu);
				pl_vline(c, wx + ww - 1, wy + 22 - 7, wy + 22 - 1, 0xFF525252u);
			} else {
				pl_hline(c, wx, wx + ww - 1, wy + 22 - 1, 0xFF525252u);
			}
			if (!btnClose.isNull()) {
				blitImage(c, btnClose, wx + closeX + (12 + 1 - btnClose.width()) / 2,
					wy + 4 + (12 + 1 - btnClose.height()) / 2);
			}
			pl_text(c, aa.t, wx + titleX, wy + 15, C_BLACK);
			if (!btnZoom.isNull()) {
				blitImage(c, btnZoom, wx + zoomX + (12 + 1 - btnZoom.width()) / 2,
					wy + 4 + (12 + 1 - btnZoom.height()) / 2);
			}
			if (!btnGrow.isNull()) {
				blitImage(c, btnGrow, wx + growX + (12 + 1 - btnGrow.width()) / 2,
					wy + 4 + (12 + 1 - btnGrow.height()) / 2);
			}
			for (int x = wx + 6; x < wx + ww - 6; x += barSpan.width()) {
				int spanw = wx + ww - 6 - x < barSpan.width() ? wx + ww - 6 - x : barSpan.width();
				blitTile(c, barSpan, 0, 0, spanw, std::min(16, barSpan.height()), x, wy + 22, spanw, 16);
			}
			return titleH;
		}
		const int lw = barLeft.width(), rw = barRight.width();
		if (lw > 0 && !barLeft.isNull()) {
			blitTile(c, barLeft, 0, 0, lw, std::min(22, barLeft.height()), wx, wy, lw, 22);
		}
		for (int x = lw; x < ww - rw; x += barMiddle.width()) {
			int tilew = ww - rw - x < barMiddle.width() ? ww - rw - x : barMiddle.width();
			blitTile(c, barMiddle, 0, 0, tilew, std::min(22, barMiddle.height()), wx + x, wy, tilew, 22);
		}
		if (rw > 0 && !barRight.isNull()) {
			blitTile(c, barRight, 0, 0, rw, std::min(22, barRight.height()), wx + ww - rw, wy, rw, 22);
		}
		if (!btnClose.isNull()) {
			blitImage(c, btnClose, wx + 4 + (12 + 1 - btnClose.width()) / 2, wy + 4 + (12 + 1 - btnClose.height()) / 2);
		}
		if (!btnZoom.isNull()) {
			blitImage(c, btnZoom, wx + ww - 33 + (12 + 1 - btnZoom.width()) / 2, wy + 4 + (12 + 1 - btnZoom.height()) / 2);
		}
		if (!btnGrow.isNull()) {
			blitImage(c, btnGrow, wx + ww - 17 + (12 + 1 - btnGrow.width()) / 2, wy + 4 + (12 + 1 - btnGrow.height()) / 2);
		}
		pl_text(c, aa.t, wx + (ww - aaW) / 2, wy + 15, C_BLACK);
		return 22;
	}
	/* Platinum fallback: title-base fill, stripes around a centred Aa,
	 * close box left, zoom/grow boxes right. */
	const uint32_t base = galleryHex(conf.value("title-base"), 0xFFEEEEEEu);
	const uint32_t light = galleryHex(conf.value("title-stripe-light"), C_WHITE);
	const uint32_t dark = galleryHex(conf.value("title-stripe-dark"), 0xFF777777u);
	pl_fill(c, wx, wy, wx + ww - 1, wy + 21, base);
	pl_hline(c, wx, wx + ww - 1, wy, C_BLACK);
	pl_hline(c, wx, wx + ww - 1, wy + 21, C_BLACK);
	pl_vline(c, wx, wy, wy + 21, C_BLACK);
	pl_vline(c, wx + ww - 1, wy, wy + 21, C_BLACK);
	const int x0 = wx + 4 + 12 + 1 + 4, x1 = wx + ww - 33 - 4 - 1;
	const int titleL = wx + (ww - aaW) / 2, titleR = titleL + aaW - 1;
	for (int k = 0; k < 6; ++k) {
		const int yy = wy + 4 + 2 * k;
		if (x0 <= titleL - 5) {
			pl_hline(c, x0, titleL - 5 - (k % 2), yy, light);
			pl_hline(c, x0 + 1, titleL - 5 - (k % 2) + 1, yy + 1, dark);
		}
		if (titleR + 5 <= x1) {
			pl_hline(c, titleR + 5, x1 - (k % 2), yy, light);
			pl_hline(c, titleR + 6, x1 - (k % 2) + 1, yy + 1, dark);
		}
	}
	pl_fill(c, wx + 4, wy + 4, wx + 15, wy + 15, C_WHITE);
	pl_hline(c, wx + 4, wx + 15, wy + 4, C_BLACK);
	pl_hline(c, wx + 4, wx + 15, wy + 15, C_BLACK);
	pl_vline(c, wx + 4, wy + 4, wy + 15, C_BLACK);
	pl_vline(c, wx + 15, wy + 4, wy + 15, C_BLACK);
	pl_fill(c, wx + ww - 33, wy + 4, wx + ww - 22, wy + 15, C_WHITE);
	pl_hline(c, wx + ww - 33, wx + ww - 22, wy + 4, C_BLACK);
	pl_hline(c, wx + ww - 33, wx + ww - 22, wy + 15, C_BLACK);
	pl_vline(c, wx + ww - 33, wy + 4, wy + 15, C_BLACK);
	pl_vline(c, wx + ww - 22, wy + 4, wy + 15, C_BLACK);
	pl_fill(c, wx + ww - 17, wy + 4, wx + ww - 6, wy + 15, C_WHITE);
	pl_hline(c, wx + ww - 17, wx + ww - 6, wy + 4, C_BLACK);
	pl_hline(c, wx + ww - 17, wx + ww - 6, wy + 15, C_BLACK);
	pl_vline(c, wx + ww - 17, wy + 4, wy + 15, C_BLACK);
	pl_vline(c, wx + ww - 6, wy + 4, wy + 15, C_BLACK);
	pl_text(c, aa.t, titleL, wy + 15, C_BLACK);
	return 22;
}

static constexpr int CARD_W = 180;
static constexpr int CARD_PREVIEW_H = 100;
static constexpr int CARD_NAME_H = 18;
static constexpr int CARD_GAP = 12;

static void paintThemeCard(pl_canvas *c, int x, int y, int index, bool selected) {
	const QMap<QString, QString> values = appearanceThemeValues(index);
	const QString name = appearanceThemeName(index);
	const struct pl_accent accent = galleryAccent(values);
	const uint32_t highlight = galleryHighlight(values, accent);
	const QString bundle = values.value("theme-bundle");
	const QString bundleDir = galleryBundleDir(bundle);

	/* Card frame and background (the theme's own desktop). */
	pl_hline(c, x, x + CARD_W - 1, y, C_BLACK);
	pl_hline(c, x, x + CARD_W - 1, y + CARD_PREVIEW_H - 1, C_BLACK);
	pl_vline(c, x, y, y + CARD_PREVIEW_H - 1, C_BLACK);
	pl_vline(c, x + CARD_W - 1, y, y + CARD_PREVIEW_H - 1, C_BLACK);
	const int bx0 = x + 1, by0 = y + 1, bx1 = x + CARD_W - 2, by1 = y + CARD_PREVIEW_H - 2;
	const int pat = desktopPatternFind(values.value("pattern"));
	const int wall = desktopWallpaperFind(values.value("wallpaper"));
	const QStringList modes{ "fit", "fill", "stretch", "center", "tile" };
	QString mode = values.value("wallpaper-mode");
	if (!modes.contains(mode)) {
		mode = "fit";
	}
	if (values.value("background") == "wallpaper" && wall >= 0) {
		desktopWallpaperFill(c, wall, mode, bx0, by0, bx1, by1);
	} else if (pat >= 0) {
		desktopPatternFill(c, pat, bx0, by0, bx1, by1);
	} else if (wall >= 0) {
		desktopWallpaperFill(c, wall, mode, bx0, by0, bx1, by1);
	} else {
		pl_fill(c, bx0, by0, bx1, by1, GRAY(0xD));
	}
	if (selected) {
		pl_fill(c, x - 1, y - 1, x + CARD_W, y + CARD_PREVIEW_H, C_BLACK);
		pl_hline(c, x, x + CARD_W - 1, y, C_WHITE);
	}

	/* Mini window in the theme's own chrome, with Aa, radios and checkboxes. */
	const int wx = x + 8, wy = y + 4, ww = 90;
	const QMap<QString, QString> tconf = galleryThemeConf(bundleDir);
	Text aa("Aa", 80, PL_FONT_SYSTEM);
	const int titleH = paintMiniTitleBar(c, wx, wy, ww, tconf, bundleDir, aa);
	const int bodyH = 52, wh = titleH + bodyH;
	pl_fill(c, wx, wy + titleH, wx + ww - 1, wy + wh - 1, C_WHITE);
	pl_hline(c, wx, wx + ww - 1, wy + wh - 1, C_BLACK);
	pl_vline(c, wx, wy, wy + wh - 1, C_BLACK);
	pl_vline(c, wx + ww - 1, wy, wy + wh - 1, C_BLACK);
	pl_hline(c, wx, wx + ww - 1, wy + titleH - 1, C_BLACK);
	const int bodyY = wy + titleH;
	pl_radio_paint(c, wx + 8, bodyY + 2, false, false, true, nullptr);
	pl_radio_paint(c, wx + 8, bodyY + 14, true, false, true, nullptr);
	pl_checkbox_paint(c, wx + 8, bodyY + 26, PL_CHECK_ON, false, true, nullptr);
	pl_checkbox_paint(c, wx + 8, bodyY + 38 - 0, PL_CHECK_OFF, false, true, nullptr);

	/* The theme's own 32x32 folder with highlighted Zz. */
	const int fx = x + 110, fy = y + 12;
	QImage folder;
	if (!bundleDir.isEmpty()) {
		folder.load(bundleDir + "/icons/folder-32.png");
	}
	if (!folder.isNull()) {
		blitImage(c, folder, fx, fy);
	} else {
		pl_icon_paint(c, fx, fy, PL_ICON_FOLDER, PL_ICON_LARGE, false);
	}
	{
		Text zz("Zz", 32, PL_FONT_VIEWS);
		const int zw = std::max(20, zz.inkWidth() + 6);
		const int zx = fx + 16 - zw / 2, zy = fy + 34;
		pl_fill(c, zx, zy, zx + zw - 1, zy + 11, highlight);
		pl_text(c, zz.t, zx + (zw - zz.inkWidth()) / 2, zy + 9, C_BLACK);
	}

	/* Mini progress card in the theme's accent. */
	const int px = x + 100, py = y + 58, pw = 70, ph = 24;
	pl_fill(c, px, py, px + pw - 1, py + ph - 1, C_WHITE);
	pl_hline(c, px, px + pw - 1, py, C_BLACK);
	pl_hline(c, px, px + pw - 1, py + ph - 1, C_BLACK);
	pl_vline(c, px, py, py + ph - 1, C_BLACK);
	pl_vline(c, px + pw - 1, py, py + ph - 1, C_BLACK);
	pl_progress_paint(c, px + 8, py + 8, pw - 16, 0.6, accent);

	/* Name beneath, highlighted when selected. */
	{
		Text label(name, CARD_W, PL_FONT_SYSTEM);
		const int nx = x, ny = y + CARD_PREVIEW_H + 2;
		if (selected) {
			const int nw = std::min(CARD_W, label.inkWidth() + 8);
			pl_fill(c, nx + (CARD_W - nw) / 2, ny - 2, nx + (CARD_W + nw) / 2 - 1, ny + 12,
				pl_highlight_current());
		}
		pl_text(c, label.t, nx + (CARD_W - label.inkWidth()) / 2, ny + 10, C_BLACK);
	}
}

void AppearancePanel::paintThemesTab(pl_canvas *c) {
	/* Recessed gallery well with two side-by-side visual cards. */
	const int count = appearanceThemeCount();
	if (m_themeFirst > std::max(0, count - 2)) {
		m_themeFirst = std::max(0, count - 2);
	}
	if (m_themeFirst < 0) {
		m_themeFirst = 0;
	}
	pl_fill(c, m_galleryRect.left(), m_galleryRect.top(),
		m_galleryRect.right(), m_galleryRect.bottom(), C_WHITE);
	pl_hline(c, m_galleryRect.left(), m_galleryRect.right(), m_galleryRect.top(), GRAY(0x8));
	pl_vline(c, m_galleryRect.left(), m_galleryRect.top(), m_galleryRect.bottom(), GRAY(0x8));
	pl_hline(c, m_galleryRect.left(), m_galleryRect.right(), m_galleryRect.bottom(), C_WHITE);
	pl_vline(c, m_galleryRect.right(), m_galleryRect.top(), m_galleryRect.bottom(), C_WHITE);
	const int cardTop = m_galleryRect.top() + 6;
	for (int slot = 0; slot < 2; ++slot) {
		const int index = m_themeFirst + slot;
		if (index < 0 || index >= count) {
			continue;
		}
		const int cardX = m_galleryRect.left() + 8 + slot * (CARD_W + CARD_GAP);
		if (cardX + CARD_W > m_galleryRect.right()) {
			continue;
		}
		paintThemeCard(c, cardX, cardTop, index, index == m_themes.state.selected);
	}
	/* Horizontal scroll bar along the bottom of the well. */
	m_themeBar.vertical = false;
	m_themeBar.length = m_galleryBarRect.width();
	m_themeBar.enabled = count > 2;
	const int span = sb_thumb_span(&m_themeBar);
	m_themeBar.thumb = count > 2 ? m_themeFirst * span / std::max(1, count - 2) : 0;
	pl_scrollbar_paint(c, m_galleryBarRect.left(), m_galleryBarRect.top(), &m_themeBar,
		pl_accent_current());

	const QString current = m_themes.state.selected >= 0 && m_themes.state.selected < count
		? appearanceThemeName(m_themes.state.selected) : QString("None");
	{
		Text cur("Current Theme: " + current, BOX_R - BOX_L, PL_FONT_SYSTEM);
		pl_text(c, cur.t, BOX_L + 4, m_galleryRect.bottom() + 16, C_BLACK);
	}
	panelText(c, "To create a new theme, modify the settings in the following sections.",
		BOX_L + 4, m_galleryRect.bottom() + 30, PL_FONT_VIEWS);
	m_host.paintControls(c, PANE);
	const QStringList errors = customThemeErrors();
	if (!m_themeError.isEmpty() || !errors.isEmpty()) {
		panelText(c, !m_themeError.isEmpty() ? m_themeError : errors.first(),
			BOX_L + 4, H - 24, PL_FONT_VIEWS, C_BLACK, BOX_R - BOX_L);
	} else {
		panelText(c, "Tip: drop a Mac OS 9 theme onto the System Folder and it files itself into Theme Files.",
			BOX_L + 4, H - 24, PL_FONT_VIEWS, C_BLACK, BOX_R - BOX_L);
	}
}

/* GTK programs read their font from gsettings (the session runs no
 * XSETTINGS); Qt ones from the Platinum style, at start. */
void AppearancePanel::applyProgramFonts() {
	static const int sizes[4] = { 9, 10, 12, 14 };
	const int size = sizes[std::clamp(m_fontSize.selected, 0, 3)];
	const int familyRow = std::clamp(m_fontFamily.selected, 0,
		std::max(0, (int)m_fontFamilyIds.size() - 1));
	const QString family = m_fontFamilyIds.value(familyRow);
	QProcess::startDetached("gsettings", { "set", "org.gnome.desktop.interface", "font-name",
		family.isEmpty() ? QString("Sans %1").arg(size) : QString("%1 %2").arg(family).arg(size) });
	QProcess::startDetached("gsettings", { "set", "org.gnome.desktop.interface", "font-antialiasing",
		m_smoothFonts.on ? "grayscale" : "none" });
}

void AppearancePanel::paintOptionsTab(pl_canvas *c) {
	groupAround(c, m_titleCollapse.hitRect(), "Windows", BOX_R);
	Text scrollBars("Scroll Bars", 300, PL_FONT_SYSTEM);
	const int bottom = m_smartScrolling.hitRect().bottom() + 6 + 2 * 13 + PL_GROUP_MARGIN + 1;
	pl_group_box_paint(c, BOX_L, m_scrollGroupTop, BOX_R, bottom, scrollBars.t, PANE);
	const int x = m_smartScrolling.hitRect().left() + 20;
	panelText(c, "Scroll arrows together at the end, and a scroll box that",
		x, m_smartScrolling.hitRect().bottom() + 14, PL_FONT_VIEWS);
	panelText(c, "shows how much of the contents the window holds.",
		x, m_smartScrolling.hitRect().bottom() + 27, PL_FONT_VIEWS);
	m_host.paintControls(c, PANE);
}

void AppearancePanel::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);
	std::vector<const plat_text *> labels;
	for (auto &t : m_tabLabels) {
		labels.push_back(t->t);
	}
	pl_tabs_paint(c, MARGIN, TABS_Y, W - MARGIN - 1, H - MARGIN - 1, labels.data(),
		static_cast<int>(labels.size()), m_tab);
	switch (m_tab) {
	case 0: paintThemesTab(c); break;
	case 1: paintAppearanceTab(c); break;
	case 2: paintFontsTab(c); break;
	case 3: paintDesktopTab(c); break;
	case 4: paintSoundTab(c); break;
	default: paintOptionsTab(c); break;
	}
	QPainter p(this);
	px.blit(p);
}

void AppearancePanel::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	std::vector<const plat_text *> labels;
	for (auto &t : m_tabLabels) {
		labels.push_back(t->t);
	}
	const int tab = pl_tabs_hit(MARGIN, TABS_Y, labels.data(), static_cast<int>(labels.size()),
		pos.x(), pos.y());
	if (tab >= 0 && tab != m_tab) {
		showTab(tab);
		return;
	}
	if (m_tab == 0) {
		/* Gallery scrollbar first. */
		if (m_galleryBarRect.contains(pos)) {
			const int along = pos.x() - m_galleryBarRect.left();
			switch (sb_hit(&m_themeBar, along)) {
			case SB_DEC_ARROW:
				if (m_themeFirst > 0) {
					m_themeFirst--;
					update();
				}
				return;
			case SB_INC_ARROW: {
				const int maxFirst = std::max(0, appearanceThemeCount() - 2);
				if (m_themeFirst < maxFirst) {
					m_themeFirst++;
					update();
				}
				return;
			}
			case SB_DEC_PAGE:
				m_themeFirst = std::max(0, m_themeFirst - 2);
				update();
				return;
			case SB_INC_PAGE: {
				const int maxFirst = std::max(0, appearanceThemeCount() - 2);
				m_themeFirst = std::min(maxFirst, m_themeFirst + 2);
				update();
				return;
			}
			default:
				break;
			}
		}
		/* Cards side by side. */
		const int cardTop = m_galleryRect.top() + 6;
		for (int slot = 0; slot < 2; ++slot) {
			const int index = m_themeFirst + slot;
			if (index < 0 || index >= appearanceThemeCount()) {
				continue;
			}
			const QRect card(m_galleryRect.left() + 8 + slot * (CARD_W + CARD_GAP), cardTop,
				CARD_W, CARD_PREVIEW_H + CARD_NAME_H);
			if (card.contains(pos)) {
				m_themes.select(index, true);
				const int selected = m_themes.state.selected;
				if (selected >= 0) {
					if (selected < m_themeFirst) {
						m_themeFirst = selected;
					} else if (selected >= m_themeFirst + 2) {
						m_themeFirst = selected - 1;
					}
				}
				update();
				return;
			}
		}
		if (m_host.hostPress(e)) {
			return;
		}
		return;
	}
	if (m_tab == 2 && (m_fontFamily.press(this, pos) || m_fontSize.press(this, pos))) {
		update();
		return;
	}
	if (m_tab == 3 && m_placement.press(this, pos)) {
		return;
	}
	if (m_tab == 4 && m_interfaceVolume.press(pos)) {
		update();
		return;
	}
	if (m_host.hostPress(e)) {
		return;
	}
	for (PanelList *l : visibleLists()) {
		if (l->press(pos)) {
			m_focus = l;
			update();
			return;
		}
	}
}

void AppearancePanel::mouseMoveEvent(QMouseEvent *e) {
	if (m_tab == 4 && m_interfaceVolume.move(e->position().toPoint())) {
		update();
	}
	m_host.hostMove(e);
	for (PanelList *l : visibleLists()) {
		if (m_tab == 0) {
			continue;
		}
		if (l->move(e->position().toPoint())) {
			update();
		}
	}
}

void AppearancePanel::mouseReleaseEvent(QMouseEvent *e) {
	if (m_tab == 4) {
		m_interfaceVolume.release(e->position().toPoint());
	}
	m_host.hostRelease(e);
	for (PanelList *l : visibleLists()) {
		l->release();
	}
}

void AppearancePanel::wheelEvent(QWheelEvent *e) {
	if (m_tab == 0) {
		const int maxFirst = std::max(0, appearanceThemeCount() - 2);
		if (e->angleDelta().y() < 0 && m_themeFirst < maxFirst) {
			m_themeFirst++;
			update();
		} else if (e->angleDelta().y() > 0 && m_themeFirst > 0) {
			m_themeFirst--;
			update();
		}
		return;
	}
	for (PanelList *l : visibleLists()) {
		if (l->wheel(e->position().toPoint(), e->angleDelta().y())) {
			update();
		}
	}
}

void AppearancePanel::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (m_tab == 0) {
		if (m_host.hostKey(e)) {
			update();
			return;
		}
		const int count = appearanceThemeCount();
		const int maxFirst = std::max(0, count - 2);
		if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Right) {
			int selected = m_themes.state.selected;
			if (e->key() == Qt::Key_Left && selected > 0) {
				m_themes.select(selected - 1, true);
			} else if (e->key() == Qt::Key_Right && selected + 1 < count) {
				m_themes.select(selected + 1, true);
			}
			selected = m_themes.state.selected;
			if (selected < m_themeFirst) {
				m_themeFirst = selected;
			} else if (selected >= m_themeFirst + 2) {
				m_themeFirst = std::min(maxFirst, selected - 1);
			}
			update();
			return;
		}
		if (m_focus && m_focus->key(e->key(), e->text())) {
			const int selected = m_themes.state.selected;
			if (selected < m_themeFirst) {
				m_themeFirst = selected;
			} else if (selected >= m_themeFirst + 2) {
				m_themeFirst = std::min(maxFirst, selected - 1);
			}
			update();
			return;
		}
		return;
	}
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	if (!m_focus) {
		return;
	}
	if (e->key() == Qt::Key_Tab) {
		/* Tab moves the focus between the lists on this pane. */
		auto lists = visibleLists();
		auto it = std::find(lists.begin(), lists.end(), m_focus);
		m_focus = (it == lists.end() || it + 1 == lists.end()) ? lists.front() : *(it + 1);
	} else if (!m_focus->key(e->key(), e->text())) {
		return;
	}
	update();
}

/* Records up to ten seconds from the sound input (pw-record, as Bark
 * Recorder does) while an alert offers Stop; then asks for its name. The
 * WAV file it made, named so, or empty. Never waitForFinished (CLAUDE.md). */
QString AppearancePanel::recordAlertSound() {
	static QTemporaryDir dir;
	const QString raw = dir.filePath("recording.wav");
	QFile::remove(raw);
	QProcess recorder;
	recorder.start("pw-record", { "--rate", "22050", "--channels", "1", raw });
	if (!recorder.waitForStarted(3000)) {
		m_themeError = "Recording needs PipeWire's pw-record, which couldn't be started.";
		update();
		return {};
	}
	QMessageBox stop(QMessageBox::Information, "Recording", "Recording… Make the sound now, then click Stop.",
		QMessageBox::NoButton, this);
	stop.addButton("Stop", QMessageBox::AcceptRole);
	QTimer::singleShot(10000, &stop, [&stop] { stop.accept(); }); /* ten seconds at most */
	stop.exec();
	recorder.terminate(); /* pw-record finishes the file on SIGTERM */
	waitForProcess(&recorder, 3000); /* killed if it hasn't finished by then */
	if (QFileInfo(raw).size() <= 44) { /* a WAV header and nothing else */
		m_themeError = "Nothing was recorded. Check the sound input in the Sound control panel.";
		update();
		return {};
	}
	bool ok = false;
	QString name = QInputDialog::getText(this, "Add Alert Sound", "Name of the new alert sound:",
		QLineEdit::Normal, "Recording", &ok).trimmed();
	name.remove(QRegularExpression("[/\\\\:\\x00-\\x1f]"));
	if (!ok || name.isEmpty()) {
		return {};
	}
	const QString named = dir.filePath(name + ".wav");
	QFile::remove(named);
	return QFile::rename(raw, named) ? named : QString();
}
