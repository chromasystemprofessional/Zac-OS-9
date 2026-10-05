#include "appearance.h"

#include <QDir>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>

#include "patterns.h"
#include "custompatterns.h"
#include "customthemes.h"
#include "settings.h"

/* The window, its tab control, and the layout inside a pane following
 * the HIG's group box measurements (figure 3-29). */
static constexpr int W = 580, H = 300;
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

/* ---- the panel ---------------------------------------------------------------- */

AppearancePanel::AppearancePanel() {
	setWindowTitle("Appearance");
	setFixedSize(W, H);
	for (const char *label : { "Color", "Desktop", "Wallpaper", "Sound", "Themes", "Sound Sets" }) {
		m_tabLabels.push_back(std::make_unique<Text>(label, 200, PL_FONT_SYSTEM));
	}

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
	const QStringList modes{"fit", "fill", "stretch", "center"};
	m_placement.selected = std::max<int>(0, modes.indexOf(setting("wallpaper-mode")));
	m_placement.chosen = [this, modes](int i) {
		m_placement.selected = i;
		setSetting("wallpaper-mode", modes[i]);
		update();
	};

	/* Alert sound: what's in sounds/, plus None. */
	m_sounds.frame = QRect(QPoint(BOX_L + ITEM_INSET, ITEM_TOP),
		QPoint(BOX_L + ITEM_INSET + 170, ITEM_TOP + 8 * PL_LIST_ROW_H + 1));
	QDir dir(QString::fromUtf8(pl_data_dir()) + "/sounds");
	m_soundIds = { "platinum" };
	for (const QString &f : dir.entryList({ "*.wav" }, QDir::Files, QDir::Name)) {
		const QString id = f.chopped(4);
		if (id != "platinum") {
			m_soundIds << id;
		}
	}
	m_soundIds << "none";
	QStringList soundNames;
	for (const QString &id : m_soundIds) {
		/* The default alert sound's file is still platinum.wav (sound
		 * assets, like the fonts and icons, keep their internal names);
		 * shown as "Chime" rather than capitalizing that into the
		 * product's old name. */
		QString name = id == "platinum" ? "Chime" : id;
		name[0] = name[0].toUpper();
		soundNames << name;
	}
	m_sounds.setItems(soundNames);
	const QString chosen = setting("alert-sound");
	m_sounds.select(std::max<int>(0, m_soundIds.indexOf(chosen.isEmpty() ? "platinum" : chosen)),
		false);
	m_sounds.picked = [this](int i) {
		const QString id = m_soundIds[i];
		setSetting("alert-sound", id == "platinum" ? QString() : id);
		pl_sound_play(id.toUtf8().constData());
		update();
	};

	m_themes.frame = m_soundThemes.frame = m_sounds.frame;
	m_previewTheme = PanelButton("Preview", QRect(m_soundThemes.frame.right() + 20, ITEM_TOP + 20, 90, 20));
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
	m_themeName.setText("My Theme");
	m_saveTheme = PanelButton("Save Current", QRect(m_themes.frame.right() + 20, ITEM_TOP + 82, 130, 20));
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
			const QStringList modes{ "fit", "fill", "stretch", "center" };
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
	m_host.buttons = m_tab == 4 ? std::vector<PanelButton *>{ &m_saveTheme }
		: m_tab == 5 ? std::vector<PanelButton *>{ &m_previewTheme } : std::vector<PanelButton *>{};
	m_host.edits = m_tab == 4 ? std::vector<PanelEdit *>{ &m_themeName } : std::vector<PanelEdit *>{};
	if (m_tab != 4) {
		m_host.setFocus(nullptr);
	}
	if (m_tab == 4 || m_tab == 5) {
		loadThemes();
	}
	m_focus = visibleLists().front();
	update();
}

std::vector<PanelList *> AppearancePanel::visibleLists() {
	switch (m_tab) {
	case 0: return { &m_accents, &m_highlights };
	case 1: return { &m_patterns };
	case 2: return { &m_wallpapers };
	case 3: return { &m_sounds };
	case 4: return { &m_themes };
	default: return { &m_soundThemes };
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
	pl_scrollbar sb = { false, 150, true, 40 };
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
	const QStringList modes{"fit", "fill", "stretch", "center"};
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
			"alert appears." }) {
		Text t(line, BOX_R - x - 4, PL_FONT_VIEWS);
		pl_text(c, t.t, x, y, C_BLACK);
		y += 13;
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
	default: paintSoundThemesTab(c); break;
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
	if (m_tab == 2 && m_placement.press(this, pos)) {
		return;
	}
	if (m_tab == 5 && m_interfaceVolume.press(pos)) {
		update();
		return;
	}
	if ((m_tab == 4 || m_tab == 5) && m_host.hostPress(e)) {
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
	if (m_tab == 4 || m_tab == 5) {
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
	if (m_tab == 4 || m_tab == 5) {
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
	if ((m_tab == 4 || m_tab == 5) && m_host.hostKey(e)) {
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
