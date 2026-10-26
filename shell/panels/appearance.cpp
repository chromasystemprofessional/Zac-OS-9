#include "appearance.h"

#include <QApplication>
#include <QDir>
#include <QFile>
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
	setWindowTitle("Appearance");
	setFixedSize(W, H);
	for (const char *label : { "Color", "Desktop", "Wallpaper", "Sound", "Themes", "Sound Sets",
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
	 * through gsettings, Qt through the Platinum style when it starts. */
	m_fontGroupTop = m_smartScrolling.hitRect().bottom() + 6 + 2 * 13 + PL_GROUP_MARGIN + 1 + 28;
	static const int sizes[4] = { 9, 10, 12, 14 };
	m_fontSize.rect = QRect(BOX_L + ITEM_INSET + 196, m_fontGroupTop + 1 + PL_GROUP_MARGIN_TOP, 170, PL_POPUP_H);
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

	/* Desktop pattern. */
	m_patterns.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_L + ITEM_INSET + 170, ITEM_TOP + 8 * PL_LIST_ROW_H + 1));
	loadPatterns();
	watchDesktopPatterns(this, [this] { loadPatterns(); update(); });
	m_patterns.picked = [this](int i) {
		setSetting("pattern", i == 0 ? QString() : desktopPatternId(i));
		setSetting("background", "pattern");
		update();
	};

	m_wallpapers.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_L + ITEM_INSET + 170, ITEM_TOP + 6 * PL_LIST_ROW_H + 1));
	loadWallpapers();
	watchDesktopWallpaper(this, [this] { loadWallpapers(); update(); });
	m_wallpapers.picked = [this](int i) {
		if (i < desktopWallpaperCount()) {
			setSetting("wallpaper", desktopWallpaperId(i));
			setSetting("background", "wallpaper");
			update();
		}
	};
	m_placement.rect = QRect(BOX_L + 80, m_wallpapers.frame.bottom() + 16, 100, PL_POPUP_H);
	m_placement.label = "Placement:";
	m_placement.items = { "Fit", "Fill", "Stretch", "Center" };
	const QStringList modes{ "fit", "fill", "stretch", "center", "tile" };
	m_placement.selected = std::max<int>(0, modes.indexOf(setting("wallpaper-mode")));
	m_placement.items = { "Fit", "Fill", "Stretch", "Center", "Tile" };
	m_placement.chosen = [this, modes](int i) {
		m_placement.selected = i;
		setSetting("wallpaper-mode", modes[i]);
		update();
	};

	/* Alert sounds include the installed set and personal recordings. */
	m_sounds.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_L + ITEM_INSET + 170, ITEM_TOP + 8 * PL_LIST_ROW_H + 1));
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
	m_addSound = PanelButton("Add Recording...", QRect(m_sounds.frame.right() + 20,
		ITEM_TOP + 58, 128, 22));
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

	m_themes.frame = m_soundThemes.frame = m_sounds.frame;
	m_previewTheme = PanelButton("Preview", QRect(m_soundThemes.frame.right() + 20, ITEM_TOP + 20, 90, 20));
	m_previewTheme.hint = "Plays the sounds of the selected sound set.";
	m_previewTheme.clicked = [this] { previewSoundTheme(m_soundThemes.state.selected); };
	m_interfaceVolume.pos = QPoint(m_soundThemes.frame.right() + 20, ITEM_TOP + 78);
	m_interfaceVolume.width = 170;
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
	m_themeName.rect = QRect(m_themes.frame.right() + 20, ITEM_TOP + 48, 170, PL_EDIT_H);
	m_themeName.hint = "The name for a theme you save.";
	m_themeName.setText("My Theme");
	m_saveTheme = PanelButton("Save Current", QRect(m_themes.frame.right() + 20, ITEM_TOP + 82, 130, 20));
	m_saveTheme.hint = "Saves the current colours, pattern, picture and sounds as a theme, with the name you typed.";
	m_saveTheme.clicked = [this] {
		m_themeError.clear();
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
		}
		update();
	};
	m_soundThemes.picked = [this](int i) {
		m_themeError.clear();
		applySoundTheme(i, &m_themeError);
		update();
	};
	loadThemes();
	watchCustomThemes(this, [this] { loadThemes(); update(); });
	m_focus = &m_accents;
}

void AppearancePanel::loadAlertSounds() {
	m_soundIds = { "platinum" };
	for (const QString &folder : { QString::fromUtf8(pl_data_dir()) + "/sounds",
			userAlertSoundsFolder() }) {
		const QDir dir(folder);
		for (const QString &file : dir.entryList({ "*.wav" }, QDir::Files, QDir::Name)) {
			const QFileInfo info(dir.filePath(file));
			const QString id = info.completeBaseName();
			if (!info.isSymLink() && info.isFile() && !m_soundIds.contains(id) &&
					QRegularExpression("\\A[A-Za-z0-9_-]{1,48}\\z")
					.match(id).hasMatch()) {
				m_soundIds << id;
			}
		}
	}
	m_soundIds << "none";
	QStringList names;
	for (const QString &id : m_soundIds) {
		QString name = id == "platinum" ? "Chime" : id;
		name[0] = name[0].toUpper();
		names << name;
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
	m_tab = std::clamp(tab, 0, 6);
	m_host.checks = m_tab == 6 ? std::vector<PanelCheckbox *>{ &m_titleCollapse, &m_smartScrolling, &m_smoothFonts }
		: std::vector<PanelCheckbox *>{};
	m_host.buttons = m_tab == 4 ? std::vector<PanelButton *>{ &m_saveTheme }
		: m_tab == 5 ? std::vector<PanelButton *>{ &m_previewTheme }
		: m_tab == 3 ? std::vector<PanelButton *>{ &m_addSound } : std::vector<PanelButton *>{};
	m_host.edits = m_tab == 4 ? std::vector<PanelEdit *>{ &m_themeName } : std::vector<PanelEdit *>{};
	/* Hint Bubbles for this tab's lists, slider and pop-up. */
	m_host.hintAreas.clear();
	switch (m_tab) {
	case 0:
		m_host.hintAreas.push_back({ &m_accents.frame, "Chooses the colour of scroll bars, sliders and progress bars." });
		m_host.hintAreas.push_back({ &m_highlights.frame, "Chooses the colour of selected text." });
		break;
	case 1:
		m_host.hintAreas.push_back({ &m_patterns.frame, "Chooses the pattern that covers the desktop." });
		break;
	case 2:
		m_host.hintAreas.push_back({ &m_wallpapers.frame, "Chooses a picture for the desktop, over its pattern." });
		m_host.hintAreas.push_back({ &m_placement.rect, "Chooses how the picture fills the screen." });
		break;
	case 3:
		m_host.hintAreas.push_back({ &m_sounds.frame, "Chooses the sound alerts play." });
		break;
	case 4:
		m_host.hintAreas.push_back({ &m_themes.frame, "Saved themes: colours, pattern, picture and sounds together. Click one to use it." });
		break;
	case 5:
		m_host.hintAreas.push_back({ &m_soundThemes.frame, "Chooses the sounds menus, windows and controls make." });
		break;
	}
	if (m_tab != 4) {
		m_host.setFocus(nullptr);
	}
	if (m_tab == 4 || m_tab == 5) {
		loadThemes();
	}
	const auto lists = visibleLists();
	m_focus = lists.empty() ? nullptr : lists.front();
	update();
}

std::vector<PanelList *> AppearancePanel::visibleLists() {
	switch (m_tab) {
	case 0: return { &m_accents, &m_highlights };
	case 1: return { &m_patterns };
	case 2: return { &m_wallpapers };
	case 3: return { &m_sounds };
	case 4: return { &m_themes };
	case 5: return { &m_soundThemes };
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

void AppearancePanel::paintColorTab(pl_canvas *c) {
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

void AppearancePanel::paintDesktopTab(pl_canvas *c) {
	groupAround(c, m_patterns.frame, "Patterns", BOX_R);
	paintList(c, m_patterns, m_focus == &m_patterns);

	/* The pattern, tiled in a recessed well beside the list. */
	const int size = 112;
	const int px0 = m_patterns.frame.right() + 24, py0 = ITEM_TOP + 1;
	pl_hline(c, px0 - 1, px0 + size, py0 - 1, GRAY(0x8));
	pl_vline(c, px0 - 1, py0 - 1, py0 + size, GRAY(0x8));
	pl_hline(c, px0, px0 + size, py0 + size, C_WHITE);
	pl_vline(c, px0 + size, py0, py0 + size, C_WHITE);
	desktopPatternFill(c, std::max(0, m_patterns.state.selected), px0, py0, px0 + size - 1,
		py0 + size - 1);
	Text name(m_patterns.items.value(m_patterns.state.selected), size, PL_FONT_VIEWS);
	pl_text(c, name.t, px0 + (size - name.inkWidth()) / 2, py0 + size + 16, C_BLACK);
	const QStringList errors = desktopPatternErrors();
	Text hint(errors.isEmpty() ? (setting("background") == "wallpaper"
		? "Wallpaper active; select a pattern to switch."
		: "Add tiles to Desktop Patterns")
		: QString("Import error: ") + errors.first(), BOX_R - BOX_L - 8, PL_FONT_VIEWS);
	pl_text(c, hint.t, BOX_L + 4, m_patterns.frame.bottom() + 20, C_BLACK);
}

void AppearancePanel::paintWallpaperTab(pl_canvas *c) {
	groupAround(c, m_patterns.frame, "Wallpaper", BOX_R);
	paintList(c, m_wallpapers, m_focus == &m_wallpapers);
	const int size = 112;
	const int x = m_wallpapers.frame.right() + 24, y = ITEM_TOP + 1;
	pl_hline(c, x - 1, x + size, y - 1, GRAY(8));
	pl_vline(c, x - 1, y - 1, y + size, GRAY(8));
	pl_hline(c, x, x + size, y + size, C_WHITE);
	pl_vline(c, x + size, y, y + size, C_WHITE);
	const QStringList modes{ "fit", "fill", "stretch", "center", "tile" };
	desktopWallpaperFill(c, m_wallpapers.state.selected, modes[m_placement.selected],
		x, y, x + size - 1, y + size - 1);
	m_placement.paint(c);
	const QStringList errors = desktopWallpaperErrors();
	const bool active = setting("background") == "wallpaper";
	Text hint(!errors.isEmpty() ? "Import error: " + errors.first() :
		desktopWallpaperCount() == 0 ? "Add photos to Appearance > Wallpaper." :
		active && desktopWallpaperFind(setting("wallpaper")) < 0
			? "Selected wallpaper is missing; select a photo." :
		active ? "Wallpaper active; choose a pattern to switch."
		       : "Select a photo to use wallpaper.",
		BOX_R - BOX_L - 8, PL_FONT_VIEWS);
	pl_text(c, hint.t, BOX_L + 4, m_patterns.frame.bottom() + 20, C_BLACK);
}

void AppearancePanel::paintSoundTab(pl_canvas *c) {
	groupAround(c, m_sounds.frame, "Alert Sound", BOX_R);
	paintList(c, m_sounds, m_focus == &m_sounds);
	const int x = m_sounds.frame.right() + 20;
	int y = ITEM_TOP + 11;
	for (const char *line : { "Click a sound to hear it.", "It plays whenever an",
			"alert appears.", "Record a WAV with Bark Recorder." }) {
		Text t(line, BOX_R - x - 4, PL_FONT_VIEWS);
		pl_text(c, t.t, x, y, C_BLACK);
		y += 13;
	}
	m_host.paintControls(c, PANE);
	if (!m_themeError.isEmpty()) {
		panelText(c, m_themeError, BOX_L + 4, H - 24, PL_FONT_VIEWS, C_BLACK, BOX_R - BOX_L);
	}
}

void AppearancePanel::paintThemesTab(pl_canvas *c) {
	groupAround(c, m_themes.frame, "Appearance Theme", BOX_R);
	paintList(c, m_themes, m_focus == &m_themes);
	const int x = m_themes.frame.right() + 20;
	panelText(c, "Save the current colors,", x, ITEM_TOP + 12, PL_FONT_VIEWS);
	panelText(c, "background and sound set.", x, ITEM_TOP + 26, PL_FONT_VIEWS);
	m_host.paintControls(c, PANE);
	const QStringList errors = customThemeErrors();
	panelText(c, !m_themeError.isEmpty() ? m_themeError : !errors.isEmpty() ? errors.first()
		: "Custom presets: Appearance > Themes", BOX_L + 4, H - 24, PL_FONT_VIEWS, C_BLACK, BOX_R - BOX_L);
}

/* GTK programs read their font from gsettings (the session runs no
 * XSETTINGS); Qt ones from the Platinum style, at start. */
void AppearancePanel::applyProgramFonts() {
	static const int sizes[4] = { 9, 10, 12, 14 };
	const int size = sizes[std::clamp(m_fontSize.selected, 0, 3)];
	QProcess::startDetached("gsettings", { "set", "org.gnome.desktop.interface", "font-name",
		QString("Sans %1").arg(size) });
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
	Text fonts("Fonts", 300, PL_FONT_SYSTEM);
	pl_group_box_paint(c, BOX_L, m_fontGroupTop, BOX_R, m_smoothFonts.hitRect().bottom() + PL_GROUP_MARGIN + 1,
		fonts.t, PANE);
	panelText(c, "Size of text in programs:", BOX_L + ITEM_INSET, m_fontSize.rect.top() + 15);
	m_fontSize.paint(c);
	m_host.paintControls(c, PANE);
}

void AppearancePanel::paintSoundThemesTab(pl_canvas *c) {
	groupAround(c, m_soundThemes.frame, "Sound Set", BOX_R);
	paintList(c, m_soundThemes, m_focus == &m_soundThemes);
	const int x = m_soundThemes.frame.right() + 20;
	panelText(c, "Interface sounds", x, ITEM_TOP + 12, PL_FONT_SYSTEM);
	panelText(c, "Interface Volume:", x, ITEM_TOP + 68, PL_FONT_VIEWS);
	m_interfaceVolume.paint(c);
	m_host.paintControls(c, PANE);
	const QStringList errors = customThemeErrors();
	panelText(c, !m_themeError.isEmpty() ? m_themeError : !errors.isEmpty() ? errors.first()
		: "Add sets to Appearance > Sound Themes", BOX_L + 4, H - 24, PL_FONT_VIEWS, C_BLACK, BOX_R - BOX_L);
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
	case 0: paintColorTab(c); break;
	case 1: paintDesktopTab(c); break;
	case 2: paintWallpaperTab(c); break;
	case 3: paintSoundTab(c); break;
	case 4: paintThemesTab(c); break;
	case 5: paintSoundThemesTab(c); break;
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
	if (m_tab == 6 && m_fontSize.press(this, pos)) {
		update();
		return;
	}
	if (m_tab == 2 && m_placement.press(this, pos)) {
		return;
	}
	if (m_tab == 5 && m_interfaceVolume.press(pos)) {
		update();
		return;
	}
	if ((m_tab >= 3) && m_host.hostPress(e)) {
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
	if (m_tab == 5 && m_interfaceVolume.move(e->position().toPoint())) {
		update();
	}
	if (m_tab >= 3) {
		m_host.hostMove(e);
	}
	for (PanelList *l : visibleLists()) {
		if (l->move(e->position().toPoint())) {
			update();
		}
	}
}

void AppearancePanel::mouseReleaseEvent(QMouseEvent *e) {
	if (m_tab == 5) {
		m_interfaceVolume.release(e->position().toPoint());
	}
	if (m_tab >= 3) {
		m_host.hostRelease(e);
	}
	for (PanelList *l : visibleLists()) {
		l->release();
	}
}

void AppearancePanel::wheelEvent(QWheelEvent *e) {
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
	if ((m_tab == 3 || m_tab == 4 || m_tab == 5 || m_tab == 6) && m_host.hostKey(e)) {
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
	bool done = recorder.state() == QProcess::NotRunning;
	QObject::connect(&recorder, &QProcess::finished, [&done] { done = true; });
	for (int waited = 0; !done && waited < 3000; waited += 50) {
		QApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 50);
	}
	if (!done) {
		recorder.kill();
	}
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
