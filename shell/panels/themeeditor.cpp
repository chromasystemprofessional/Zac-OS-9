#include "themeeditor.h"

#include <QColor>
#include <QColorDialog>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QStandardPaths>
#include <algorithm>
#include <cstring>

#include "custompatterns.h"
#include "settings.h"
#include "theme.h"

/* The editor's window, without the title bar the compositor draws. */
static constexpr int W = 640, H = 540;
static constexpr uint32_t FACE = GRAY(0xD);
static constexpr int LEFT_X = 16, LEFT_R = 320;
static constexpr int RIGHT_X = 336, RIGHT_R = 624;
static constexpr int TITLE_TOP = 52, TITLE_BOTTOM = 178;
static constexpr int FRAME_TOP = 186, FRAME_BOTTOM = 312;
static constexpr int MENU_TOP = 320, MENU_BOTTOM = 444;
static constexpr int DESKTOP_BOTTOM = 212;
static constexpr int SOUND_TOP = 220, SOUND_BOTTOM = 302;
static constexpr int TEXT_TOP = 310, TEXT_BOTTOM = 366;
static constexpr int PREVIEW_TOP = 374, PREVIEW_BOTTOM = 478;
static constexpr int BUTTON_TOP = 506;

static QString setting(const char *key) {
	char value[256];
	return pl_setting(key, value, sizeof(value)) ? QString::fromUtf8(value) : QString();
}

static QString hexColour(uint32_t v) {
	return QString::asprintf("%06X", v & 0xFFFFFFu);
}

static QString uniqueThemeName(const QString &base) {
	QString name = base;
	for (int n = 2;; ++n) {
		bool clash = false;
		for (int i = 0; i < appearanceThemeCount(); ++i) {
			if (appearanceThemeName(i).compare(name, Qt::CaseInsensitive) == 0) {
				clash = true;
				break;
			}
		}
		if (!clash) {
			return name;
		}
		name = base + " " + QString::number(n);
	}
}

static QString configBundleDir(const QString &id) {
	if (id.isEmpty()) {
		return QString();
	}
	const QString user = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/themes/" + id;
	if (QFileInfo(user).isDir()) {
		return user;
	}
	const QString system = QString::fromUtf8(pl_data_dir()) + "/themes/" + id;
	return QFileInfo(system).isDir() ? system : QString();
}

static QImage loadArgbImage(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return QImage();
	}
	const QByteArray head = file.read(8);
	if (head.size() != 8) {
		return QImage();
	}
	auto u32 = [&head](int at) {
		return (quint32)(uchar)head[at] | (quint32)(uchar)head[at + 1] << 8 |
			(quint32)(uchar)head[at + 2] << 16 | (quint32)(uchar)head[at + 3] << 24;
	};
	const quint32 w = u32(0), h = u32(4);
	if (w == 0 || h == 0 || w > 4096 || h > 4096 || (quint64)w * h > 4096ULL * 1024) {
		return QImage();
	}
	const QByteArray body = file.read((qint64)w * h * 4);
	if (body.size() != (qint64)w * h * 4) {
		return QImage();
	}
	QImage image((int)w, (int)h, QImage::Format_RGBA8888);
	for (quint32 y = 0; y < h; ++y) {
		memcpy(image.scanLine((int)y), body.constData() + (size_t)y * w * 4, (size_t)w * 4);
	}
	return image;
}

static QImage cachedArt(const QString &path) {
	static QMap<QString, QImage> cache;
	const QFileInfo info(path);
	const QString key = path + "|" + QString::number(info.size()) + "|" +
		QString::number(info.lastModified().toMSecsSinceEpoch());
	if (cache.contains(key)) {
		return cache.value(key);
	}
	QImage image;
	if (path.endsWith(".argb", Qt::CaseInsensitive)) {
		image = loadArgbImage(path);
	} else {
		image = QImage(path);
	}
	if (cache.size() > 48) {
		cache.clear();
	}
	cache.insert(key, image);
	return image;
}

/* The image a slot resolves to in this draft: a chosen source first, then
 * the theme's own bundle, then the inherited bundle. */
static QImage artImageFor(const ThemeDraft &draft, const ThemeArtSlot &slot) {
	const QString source = draft.artSources.value(slot.key);
	if (!source.isEmpty()) {
		return cachedArt(source);
	}
	const QString value = draft.chrome.value(slot.key);
	if (value == "off" || value.startsWith('/')) {
		return QImage();
	}
	const QString own = configBundleDir(draft.bundleId);
	QString path;
	if (value.isEmpty()) {
		if (slot.defaultPath.isEmpty()) {
			return QImage();
		}
		if (!own.isEmpty() && QFileInfo(own + "/" + slot.defaultPath).isFile()) {
			path = own + "/" + slot.defaultPath;
		} else if (!draft.baseBundleDir.isEmpty() &&
				QFileInfo(draft.baseBundleDir + "/" + slot.defaultPath).isFile()) {
			path = draft.baseBundleDir + "/" + slot.defaultPath;
		}
	} else if (!own.isEmpty() && QFileInfo(own + "/" + value).isFile()) {
		path = own + "/" + value;
	} else if (!draft.baseBundleDir.isEmpty() &&
			QFileInfo(draft.baseBundleDir + "/" + value).isFile()) {
		path = draft.baseBundleDir + "/" + value;
	}
	if (path.isEmpty()) {
		return QImage();
	}
	return cachedArt(path);
}

/* Blend one straight-ARGB pixel over the canvas. */
static void putArtPixel(pl_canvas *c, int x, int y, QRgb p) {
	const unsigned a = qAlpha(p);
	if (a == 0) {
		return;
	}
	if (a == 255) {
		pl_put(c, x, y, p);
		return;
	}
	if (x < c->x || x >= c->x + c->width || y < c->y || y >= c->y + c->height) {
		return;
	}
	uint32_t *dst = &c->px[(size_t)(y - c->y) * c->stride + (x - c->x)];
	const uint32_t bg = *dst;
	const unsigned r = (qRed(p) * a + ((bg >> 16) & 0xFF) * (255 - a)) / 255;
	const unsigned g = (qGreen(p) * a + ((bg >> 8) & 0xFF) * (255 - a)) / 255;
	const unsigned b = (qBlue(p) * a + (bg & 0xFF) * (255 - a)) / 255;
	*dst = 0xFF000000u | r << 16 | g << 8 | b;
}

/* Nearest-neighbour ARGB blit, like the gallery's tiles. */
static void blitArt(pl_canvas *c, const QImage &image, int dx, int dy, int dw, int dh) {
	if (image.isNull() || dw <= 0 || dh <= 0) {
		return;
	}
	const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
	for (int y = 0; y < dh; ++y) {
		const int sy = y * argb.height() / dh;
		if (sy < 0 || sy >= argb.height()) {
			continue;
		}
		const QRgb *row = reinterpret_cast<const QRgb *>(argb.constScanLine(sy));
		for (int x = 0; x < dw; ++x) {
			const int sx = x * argb.width() / dw;
			if (sx < 0 || sx >= argb.width()) {
				continue;
			}
			putArtPixel(c, dx + x, dy + y, row[sx]);
		}
	}
}

/* Tile an image across a rectangle (menu backgrounds and highlights). */
static void tileArt(pl_canvas *c, const QImage &image, int x0, int y0, int x1, int y1) {
	if (image.isNull() || x1 < x0 || y1 < y0) {
		return;
	}
	const QImage argb = image.convertToFormat(QImage::Format_ARGB32);
	for (int y = y0; y <= y1; ++y) {
		const int sy = (y - y0) % argb.height();
		const QRgb *row = reinterpret_cast<const QRgb *>(argb.constScanLine(sy));
		for (int x = x0; x <= x1; ++x) {
			putArtPixel(c, x, y, row[(x - x0) % argb.width()]);
		}
	}
}

static ThemeArtSlot artSlot(const QString &key) {
	for (const ThemeArtSlot &slot : themeArtSlots()) {
		if (slot.key == key) {
			return slot;
		}
	}
	return ThemeArtSlot{};
}

static QString artStateName(ThemeArtState state) {
	switch (state) {
	case ThemeArtState::Inherit: return "Inherited";
	case ThemeArtState::Platinum: return "Platinum";
	case ThemeArtState::Custom: return "Custom";
	}
	return QString();
}

ThemeEditorWindow::ThemeEditorWindow(int presetIndex, QWidget *parent) : QWidget(parent) {
	setFixedSize(W, H);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	buildDraft(presetIndex);
	setWindowTitle(m_draft.linked ? "Theme Editor (copy of " + m_sourceName + ")"
		: QString("Theme Editor"));
	buildPaletteControls();
	buildAppearanceControls();

	m_name.rect = QRect(132, 14, 190, PL_EDIT_H);
	m_name.hint = "The theme's name, as the Appearance panel lists it.";
	m_name.maxLength = 64;
	m_name.setText(m_draft.name);
	m_lastName = m_draft.name;
	m_name.edited = [this] {
		pushUndo(m_lastName);
		m_lastName = m_name.text;
		markDirty();
	};

	m_light = PanelButton("Light Palette", QRect(RIGHT_X, 14, 108, 22));
	m_light.hint = "Starts the window and menu colours from Platinum's light greys.";
	m_light.clicked = [this] {
		pushUndo();
		fillPalette(false);
		markDirty();
	};
	m_dark = PanelButton("Dark Palette", QRect(RIGHT_X + 116, 14, 108, 22));
	m_dark.hint = "Starts the window and menu colours from a dark grey palette.";
	m_dark.clicked = [this] {
		pushUndo();
		fillPalette(true);
		markDirty();
	};

	m_delete = PanelButton("Delete Theme...", QRect(LEFT_X, BUTTON_TOP, 112, 22));
	m_delete.hint = "Removes a theme made with this editor. Imported themes are kept.";
	m_delete.enabled = m_source >= 0 && appearanceThemeIsUser(m_source);
	m_delete.clicked = [this] {
		if (m_source < 0 || !appearanceThemeIsUser(m_source)) {
			return;
		}
		const auto answer = QMessageBox::question(this, "Delete Theme",
			"Delete \"" + (m_name.text.trimmed().isEmpty() ? m_draft.name
				: m_name.text.trimmed()) + "\"? Its own artwork is removed; any imported base is kept.",
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (answer != QMessageBox::Yes) {
			return;
		}
		m_error.clear();
		if (!themeDraftDelete(m_source, &m_error)) {
			update();
			return;
		}
		m_saved = true;
		close();
	};

	m_cancel = PanelButton("Cancel", QRect(RIGHT_R - 90, BUTTON_TOP, 90, 22));
	m_cancel.hint = "Closes without saving your changes.";
	m_cancel.clicked = [this] { cancel(); };
	m_save = PanelButton("Save", QRect(RIGHT_R - 190, BUTTON_TOP, 90, 22), true);
	m_save.hint = "Saves the theme and uses it at once.";
	m_save.clicked = [this] { save(); };

	m_duplicate = PanelButton("Duplicate...", QRect(LEFT_X + 120, BUTTON_TOP, 112, 22));
	m_duplicate.hint = "Saves these settings as a new theme, leaving this one as it is.";
	m_duplicate.clicked = [this] {
		bool ok = false;
		const QString currentName = m_name.text.trimmed().isEmpty()
			? m_draft.name : m_name.text.trimmed();
		const QString name = QInputDialog::getText(this, "Duplicate Theme",
			"Name of the copy:", QLineEdit::Normal, currentName + " Copy", &ok).trimmed();
		if (!ok || name.isEmpty()) {
			return;
		}
		ThemeDraft copy = m_draft;
		copy.name = name;
		/* A duplicate keeps the artwork files the source bundle owns. */
		const QString sourceBundle = copy.bundleId;
		const QString sourceDir = configBundleDir(sourceBundle);
		if (!sourceDir.isEmpty()) {
			for (const ThemeArtSlot &slot : themeArtSlots()) {
				const QString value = copy.chrome.value(slot.key);
				if (!copy.artSources.contains(slot.key) && !value.isEmpty() &&
						value != "off" && !value.startsWith('/') &&
						QFileInfo(sourceDir + "/" + value).isFile()) {
					copy.artSources[slot.key] = sourceDir + "/" + value;
				} else if (!copy.artSources.contains(slot.key) && value.isEmpty() &&
						!slot.defaultPath.isEmpty() &&
						QFileInfo(sourceDir + "/" + slot.defaultPath).isFile()) {
					copy.artSources[slot.key] = sourceDir + "/" + slot.defaultPath;
				}
			}
		}
		copy.bundleId.clear();
		copy.presetPath.clear();
		copy.linked = false;
		m_error.clear();
		if (!themeDraftSave(&copy, -1, &m_error)) {
			update();
			return;
		}
		m_saved = true;
		close();
	};

	m_artwork = PanelButton("Artwork...", QRect(LEFT_X + 240, BUTTON_TOP, 100, 22));
	m_artwork.hint = "Chooses each title bar, button, frame and menu picture: imported, Platinum or your own image.";
	m_artwork.clicked = [this] { openArtwork(); };

	m_host.buttons = { &m_light, &m_dark, &m_delete, &m_duplicate, &m_artwork,
		&m_cancel, &m_save };
	m_host.popups = { &m_accent, &m_highlight, &m_pattern, &m_wallpaper, &m_placement,
		&m_soundSet, &m_alert, &m_font };
	m_host.edits = { &m_name };
	m_host.defaultButton = &m_save;
	m_host.cancelButton = &m_cancel;
	m_host.setFocus(&m_name);
	for (const ColourWell &well : m_wells) {
		m_host.hintAreas.push_back({ &well.rect, well.hint });
	}
	m_host.hintAreas.push_back({ &m_previewRect,
		"How the theme will look: its menu bar, an open menu and a window." });
}

void ThemeEditorWindow::buildDraft(int presetIndex) {
	m_source = presetIndex;
	QString error;
	if (presetIndex >= 0) {
		if (!themeDraftFromPreset(presetIndex, &m_draft, &error)) {
			m_error = error;
		}
		m_sourceName = m_draft.name;
	} else {
		m_draft.name = "My Theme";
		for (const char *key : { "accent", "highlight", "pattern", "wallpaper",
				"background", "wallpaper-mode", "alert-sound", "sound-theme",
				"font-family" }) {
			m_draft.settings.insert(key, setting(key));
		}
		m_draft.settings["background"] = setting("background").isEmpty()
			? QString("pattern") : setting("background");
		m_draft.settings["wallpaper-mode"] = setting("wallpaper-mode").isEmpty()
			? QString("fit") : setting("wallpaper-mode");
		m_draft.settings["sound-theme"] = setting("sound-theme").isEmpty()
			? QString("none") : setting("sound-theme");
		m_draft.chrome = themePaletteDefaults(false);
	}
	if (m_draft.linked || presetIndex < 0) {
		m_draft.name = uniqueThemeName(m_draft.linked ? m_draft.name + " Copy"
			: m_draft.name);
	}
	/* A wallpaper with no picture falls back to the pattern. */
	if (m_draft.settings.value("background") == "wallpaper" &&
			m_draft.settings.value("wallpaper").isEmpty()) {
		m_draft.settings["background"] = "pattern";
	}
	/* Fill in whatever an imported bundle did not name. Text on a dark
	 * bar becomes light instead of the Platinum default's black. */
	const QMap<QString, QString> defaults = themePaletteDefaults(false);
	const bool noTitleInk = m_draft.chrome.value("title-text").isEmpty();
	const bool noBarInk = m_draft.chrome.value("menu-bar-text").isEmpty();
	const bool noMenuInk = m_draft.chrome.value("menu-text").isEmpty();
	for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
		if (m_draft.chrome.value(it.key()).isEmpty()) {
			m_draft.chrome.insert(it.key(), it.value());
		}
	}
	auto ink = [this](const QString &backgroundKey) {
		const QString v = value(backgroundKey);
		bool ok = false;
		const uint32_t bg = 0xFF000000u | v.toUInt(&ok, 16);
		return hexColour(ok ? zac_ink_for(bg) : 0xFF000000u);
	};
	if (noTitleInk) {
		m_draft.chrome["title-text"] = ink("title-base");
	}
	if (noBarInk) {
		m_draft.chrome["menu-bar-text"] = ink("menu-bar");
	}
	if (noMenuInk) {
		m_draft.chrome["menu-text"] = ink("menu-background");
	}
	deriveSecondaryColours();
}

void ThemeEditorWindow::buildPaletteControls() {
	struct Row {
		const char *key;
		const char *label;
		const char *hint;
	};
	static const Row rows[] = {
		{ "title-base", "Title bar", "The title bar's background colour." },
		{ "title-stripe-light", "Stripe light", "The light lines across an active title bar." },
		{ "title-stripe-dark", "Stripe dark", "The shadow lines under the light stripes." },
		{ "title-text", "Title text", "The window title's colour. Inactive titles are dimmed from it." },
		{ "frame-face", "Face", "The window frame's face colour." },
		{ "frame-light", "Highlight", "The frame's lit top and left edge." },
		{ "frame-dark", "Shadow", "The frame's shaded bottom and right edge." },
		{ "frame-border", "Border", "The outline around a window." },
		{ "menu-bar", "Menu bar", "The menu bar's background." },
		{ "menu-bar-text", "Bar text", "The menu bar's titles." },
		{ "menu-background", "Menu face", "The background of pull-down menus." },
		{ "menu-text", "Menu text", "Menu commands and shortcuts." },
	};
	static const int rowTops[] = { 76, 100, 124, 148, 210, 234, 258, 282,
		344, 368, 392, 416 };
	for (int i = 0; i < 12; ++i) {
		ColourWell well;
		well.rect = QRect(LEFT_X + 214, rowTops[i], 62, 17);
		well.key = rows[i].key;
		well.label = rows[i].label;
		well.hint = rows[i].hint;
		m_wells.push_back(well);
	}
}

void ThemeEditorWindow::buildAppearanceControls() {
	const QRect desktop(RIGHT_X + 176, 0, RIGHT_R - RIGHT_X - 176 - 8, PL_POPUP_H);
	auto place = [](PanelPopup &popup, const QRect &base, int row, const QString &label) {
		popup.rect = QRect(base.x(), row, base.width(), base.height());
		popup.label = label;
	};

	QStringList accents;
	m_accentIds.clear();
	for (int i = 0; i < pl_accent_count(); ++i) {
		accents << pl_accent_name(i);
		m_accentIds << pl_accent_id(i);
	}
	m_accent.items.clear();
	for (const QString &name : accents) {
		m_accent.items.push_back(PopupItem(name));
	}
	place(m_accent, desktop, 76, "Accent:");
	m_accent.hint = "Colours scroll bars, sliders, progress bars and selected menu items.";
	m_accent.chosen = [this](int i) {
		pushUndo();
		m_accent.selected = i;
		m_draft.settings["accent"] = m_accentIds.value(i);
		markDirty();
	};

	const QList<QPair<QString, QString>> highlights = {
		{ "Accent Color", "" }, { "Yellow", "FFFF00" }, { "Gold", "FFCC66" },
		{ "Orange", "FFCC99" }, { "Green", "CCFFCC" }, { "Blue", "CCEEFF" },
		{ "Pink", "FFCCEE" }, { "Gray", "CCCCCC" },
	};
	m_highlightValues.clear();
	m_highlight.items.clear();
	for (const auto &entry : highlights) {
		m_highlight.items.push_back(PopupItem(entry.first));
		m_highlightValues << entry.second;
	}
	place(m_highlight, desktop, 102, "Highlight:");
	m_highlight.hint = "The colour of selected text.";
	m_highlight.chosen = [this](int i) {
		pushUndo();
		m_highlight.selected = i;
		m_draft.settings["highlight"] = m_highlightValues.value(i);
		markDirty();
	};

	m_patternIds.clear();
	m_pattern.items.clear();
	m_pattern.items.push_back(PopupItem("None"));
	m_patternIds << "";
	for (int i = 0; i < desktopPatternCount(); ++i) {
		m_pattern.items.push_back(PopupItem(desktopPatternName(i)));
		m_patternIds << desktopPatternId(i);
	}
	place(m_pattern, desktop, 128, "Pattern:");
	m_pattern.hint = "The tile that covers the desktop.";
	m_pattern.chosen = [this](int i) {
		pushUndo();
		m_pattern.selected = i;
		m_draft.settings["pattern"] = m_patternIds.value(i);
		m_draft.settings["background"] = "pattern";
		markDirty();
	};

	m_wallpaperIds.clear();
	m_wallpaper.items.clear();
	m_wallpaper.items.push_back(PopupItem("None"));
	m_wallpaperIds << "";
	for (int i = 0; i < desktopWallpaperCount(); ++i) {
		m_wallpaper.items.push_back(PopupItem(desktopWallpaperName(i)));
		m_wallpaperIds << desktopWallpaperId(i);
	}
	place(m_wallpaper, desktop, 154, "Picture:");
	m_wallpaper.hint = "A picture for the desktop, over its pattern.";
	m_wallpaper.chosen = [this](int i) {
		pushUndo();
		m_wallpaper.selected = i;
		m_draft.settings["wallpaper"] = m_wallpaperIds.value(i);
		m_draft.settings["background"] = i == 0 ? "pattern" : "wallpaper";
		markDirty();
	};

	place(m_placement, desktop, 180, "Placement:");
	m_placement.items = { PopupItem("Fit"), PopupItem("Fill"), PopupItem("Stretch"),
		PopupItem("Center"), PopupItem("Tile") };
	m_placement.hint = "How the desktop picture fills the screen.";
	m_placement.chosen = [this](int i) {
		pushUndo();
		static const char *const modes[] = { "fit", "fill", "stretch", "center", "tile" };
		m_placement.selected = i;
		m_draft.settings["wallpaper-mode"] = modes[i];
		markDirty();
	};

	const QRect right(RIGHT_X + 176, 0, RIGHT_R - RIGHT_X - 176 - 8, PL_POPUP_H);
	m_soundSetIds.clear();
	m_soundSet.items.clear();
	for (int i = 0; i < soundThemeCount(); ++i) {
		m_soundSet.items.push_back(PopupItem(soundThemeName(i)));
		m_soundSetIds << soundThemeId(i);
	}
	place(m_soundSet, right, 244, "Sound set:");
	m_soundSet.hint = "The sounds menus, windows and controls make.";
	m_soundSet.chosen = [this](int i) {
		pushUndo();
		m_soundSet.selected = i;
		m_draft.settings["sound-theme"] = m_soundSetIds.value(i);
		markDirty();
	};

	m_alertIds = alertSoundIds();
	m_alert.items.clear();
	for (const QString &id : m_alertIds) {
		m_alert.items.push_back(PopupItem(alertSoundDisplayName(id)));
	}
	place(m_alert, right, 270, "Alert:");
	m_alert.hint = "The sound alerts play.";
	m_alert.chosen = [this](int i) {
		pushUndo();
		m_alert.selected = i;
		m_draft.settings["alert-sound"] = m_alertIds.value(i);
		markDirty();
	};

	m_fontIds.clear();
	m_font.items.clear();
	m_fontIds << "";
	m_font.items.push_back(PopupItem("System Font"));
	QStringList families = themeFontFamilies();
	for (const QString &family : QFontDatabase::families()) {
		if (!family.isEmpty() && !families.contains(family)) {
			families << family;
		}
	}
	for (const QString &family : families) {
		m_fontIds << family;
		m_font.items.push_back(PopupItem(family));
	}
	place(m_font, right, 334, "Text:");
	m_font.hint = "The typeface programs use. Programs already open change when opened again.";
	m_font.chosen = [this](int i) {
		pushUndo();
		m_font.selected = i;
		m_draft.settings["font-family"] = m_fontIds.value(i);
		markDirty();
	};

	m_previewRect = QRect(RIGHT_X + 8, PREVIEW_TOP + 8,
		RIGHT_R - RIGHT_X - 16, PREVIEW_BOTTOM - PREVIEW_TOP - 16);
	loadValues();
}

void ThemeEditorWindow::loadValues() {
	auto indexOf = [](const QStringList &list, const QString &v) {
		return std::max(0, static_cast<int>(list.indexOf(v)));
	};
	const QString accent = m_draft.settings.value("accent");
	m_accent.selected = indexOf(m_accentIds, accent);
	const QString highlight = m_draft.settings.value("highlight").toUpper();
	m_highlight.selected = indexOf(m_highlightValues, highlight);
	const QString pattern = m_draft.settings.value("pattern");
	m_pattern.selected = indexOf(m_patternIds, pattern);
	const QString wallpaper = m_draft.settings.value("wallpaper");
	m_wallpaper.selected = indexOf(m_wallpaperIds, wallpaper);
	static const QStringList modes{ "fit", "fill", "stretch", "center", "tile" };
	const QString placement = m_draft.settings.value("wallpaper-mode");
	m_placement.selected = indexOf(modes, placement.isEmpty() ? QString("fit") : placement);
	const QString sound = m_draft.settings.value("sound-theme");
	m_soundSet.selected = indexOf(m_soundSetIds, sound.isEmpty() ? QString("none") : sound);
	const QString alert = m_draft.settings.value("alert-sound");
	m_alert.selected = indexOf(m_alertIds, alert.isEmpty() ? QString("platinum") : alert);
	const QString font = m_draft.settings.value("font-family");
	m_font.selected = indexOf(m_fontIds, font);
}

void ThemeEditorWindow::fillPalette(bool dark) {
	const QMap<QString, QString> colours = themePaletteDefaults(dark);
	for (auto it = colours.cbegin(); it != colours.cend(); ++it) {
		m_draft.chrome[it.key()] = it.value();
	}
}

uint32_t ThemeEditorWindow::colour(const QString &key) const {
	bool ok = false;
	const uint32_t v = value(key).toUInt(&ok, 16);
	return ok ? 0xFF000000u | v : 0xFF000000u;
}

void ThemeEditorWindow::setValue(const QString &key, const QString &v) {
	m_draft.chrome[key] = v;
}

void ThemeEditorWindow::deriveSecondaryColours() {
	const uint32_t white = 0xFFFFFFFFu, black = 0xFF000000u;
	const uint32_t titleBase = colour("title-base"), titleText = colour("title-text");
	const uint32_t face = colour("frame-face"), border = colour("frame-border");
	const uint32_t bar = colour("menu-bar"), barText = colour("menu-bar-text");
	const uint32_t menu = colour("menu-background"), menuText = colour("menu-text");
	const uint32_t menuInk = zac_ink_for(menu);
	const struct pl_accent accent = pl_accent_at(pl_accent_find(
		m_draft.settings.value("accent").toUtf8().constData()));
	const QString selectedInk = hexColour(zac_ink_for(accent.shadow));
	setValue("title-text-inactive", hexColour(zac_blend(titleText, titleBase, 35)));
	setValue("frame-face-inactive", hexColour(zac_blend(face, white, 10)));
	setValue("frame-border-inactive", hexColour(zac_blend(border, face, 35)));
	setValue("menu-bar-text-dimmed", hexColour(zac_blend(barText, bar, 45)));
	setValue("menu-bar-text-selected", selectedInk);
	setValue("menu-bar-light", hexColour(zac_blend(bar, white, 25)));
	setValue("menu-bar-shadow", hexColour(zac_blend(bar, black, 25)));
	setValue("menu-bar-border", hexColour(zac_blend(bar, zac_ink_for(bar), 45)));
	setValue("menu-text-dimmed", hexColour(zac_blend(menuText, menu, 45)));
	setValue("menu-text-selected", selectedInk);
	setValue("menu-border", hexColour(zac_blend(menu, menuInk, 50)));
	setValue("menu-light", hexColour(zac_blend(menu, white, 30)));
	setValue("menu-shadow", hexColour(zac_blend(menu, black, 25)));
	setValue("menu-separator", hexColour(zac_blend(menuText, menu, 50)));
	setValue("menu-separator-light", hexColour(zac_blend(menu, white, 20)));
}

void ThemeEditorWindow::markDirty() {
	deriveSecondaryColours();
	m_dirty = true;
	update();
}

void ThemeEditorWindow::pushUndo(const QString &name) {
	Snapshot snapshot{ m_draft, name.isNull() ? m_name.text : name };
	m_undo.push_back(snapshot);
	if (m_undo.size() > 80) {
		m_undo.erase(m_undo.begin());
	}
	m_redo.clear();
}

void ThemeEditorWindow::undo() {
	if (m_undo.empty()) {
		return;
	}
	m_redo.push_back({ m_draft, m_name.text });
	const Snapshot snapshot = m_undo.back();
	m_undo.pop_back();
	restoreSnapshot(snapshot);
}

void ThemeEditorWindow::redo() {
	if (m_redo.empty()) {
		return;
	}
	m_undo.push_back({ m_draft, m_name.text });
	const Snapshot snapshot = m_redo.back();
	m_redo.pop_back();
	restoreSnapshot(snapshot);
}

void ThemeEditorWindow::restoreSnapshot(const Snapshot &snapshot) {
	m_draft = snapshot.draft;
	m_name.setText(snapshot.name);
	m_lastName = snapshot.name;
	m_error.clear();
	loadValues();
	if (m_artworkWindow) {
		m_artworkWindow->reload();
	}
	m_dirty = true;
	update();
}

void ThemeEditorWindow::openArtwork() {
	if (m_artworkWindow) {
		m_artworkWindow->raise();
		m_artworkWindow->activateWindow();
		return;
	}
	auto *window = new ThemeArtworkWindow(&m_draft, [this] { pushUndo(); },
		[this] { markDirty(); }, [this] { undo(); }, [this] { redo(); }, this);
	window->setWindowFlag(Qt::Window, true);
	window->setAttribute(Qt::WA_DeleteOnClose);
	m_artworkWindow = window;
	connect(window, &QObject::destroyed, this, [this] { m_artworkWindow = nullptr; });
	window->show();
	window->raise();
	window->activateWindow();
}

void ThemeEditorWindow::chooseColour(const ColourWell &well) {
	const QString current = value(well.key);
	const QColor chosen = QColorDialog::getColor(QColor("#" + current), this, well.label,
		QColorDialog::DontUseNativeDialog);
	if (!chosen.isValid()) {
		return;
	}
	pushUndo();
	setValue(well.key, chosen.name(QColor::HexRgb).mid(1).toUpper());
	markDirty();
}

void ThemeEditorWindow::paintWell(pl_canvas *c, const ColourWell &well, int baseline) const {
	panelText(c, well.label, LEFT_X + 10, baseline, PL_FONT_VIEWS);
	panelLabel(c, "#" + value(well.key), well.rect.left() - 8, baseline, C_BLACK);
	pl_fill(c, well.rect.x() + 1, well.rect.y() + 1, well.rect.right() - 1,
		well.rect.bottom() - 1, colour(well.key));
	pl_outline(c, well.rect.x(), well.rect.y(), well.rect.right(), well.rect.bottom(),
		GRAY(0x8));
	pl_hline(c, well.rect.x(), well.rect.right(), well.rect.bottom(), C_WHITE);
	pl_vline(c, well.rect.right(), well.rect.y(), well.rect.bottom(), C_WHITE);
}

void ThemeEditorWindow::paintPreview(pl_canvas *c, const QRect &r) const {
	const int x0 = r.left(), y0 = r.top(), x1 = r.right(), y1 = r.bottom();
	/* Desktop. */
	const QString background = m_draft.settings.value("background");
	const int wall = desktopWallpaperFind(m_draft.settings.value("wallpaper"));
	const int pat = desktopPatternFind(m_draft.settings.value("pattern"));
	if (background == "wallpaper" && wall >= 0) {
		desktopWallpaperFill(c, wall, m_draft.settings.value("wallpaper-mode"), x0, y0, x1, y1);
	} else if (pat >= 0) {
		desktopPatternFill(c, pat, x0, y0, x1, y1);
	} else {
		pl_fill(c, x0, y0, x1, y1, GRAY(0x8));
	}
	const struct pl_accent accent = pl_accent_at(pl_accent_find(
		m_draft.settings.value("accent").toUtf8().constData()));

	/* The menu bar, with File highlighted. */
	const int barH = 18;
	const QImage menubarArt = artImageFor(m_draft, artSlot("menu-bar-image"));
	if (!menubarArt.isNull()) {
		tileArt(c, menubarArt, x0, y0, x1, y0 + barH - 1);
	} else {
		pl_fill(c, x0, y0, x1, y0 + barH - 1, colour("menu-bar"));
	}
	pl_hline(c, x0, x1, y0 + barH - 1, colour("menu-bar-border"));
	pl_hline(c, x0, x1, y0, colour("menu-bar-light"));
	Text file("File", 40, PL_FONT_VIEWS), edit("Edit", 40, PL_FONT_VIEWS),
		view("View", 40, PL_FONT_VIEWS);
	pl_fill(c, x0 + 7, y0 + 2, x0 + 7 + file.inkWidth() + 8, y0 + barH - 2, accent.shadow);
	pl_hline(c, x0 + 7, x0 + 7 + file.inkWidth() + 8, y0 + 2, accent.dark);
	pl_text(c, file.t, x0 + 11, y0 + 12, colour("menu-bar-text-selected"));
	pl_text(c, edit.t, x0 + 11 + file.inkWidth() + 18, y0 + 12, colour("menu-bar-text"));
	pl_text(c, view.t, x0 + 11 + file.inkWidth() + 18 + edit.inkWidth() + 18, y0 + 12,
		colour("menu-bar-text"));

	/* A window: shadow, frame, title bar, boxes and a light content area.
	 * It sits to the right of the open menu so both stay visible. */
	const int wx = x0 + 116, wy = y0 + 24;
	const int ww = x1 - wx - 8;
	const int wh = std::min(66, y1 - wy - 4);
	if (ww > 80 && wh > 52) {
		pl_fill(c, wx + 3, wy + 3, wx + ww + 2, wy + wh + 2, GRAY(0x5));
		pl_fill(c, wx, wy, wx + ww - 1, wy + wh - 1, colour("frame-face"));
		pl_outline(c, wx, wy, wx + ww - 1, wy + wh - 1, colour("frame-border"));
		pl_hline(c, wx + 1, wx + ww - 2, wy + 1, colour("frame-light"));
		pl_vline(c, wx + 1, wy + 1, wy + wh - 2, colour("frame-light"));
		pl_vline(c, wx + ww - 2, wy + 2, wy + wh - 2, colour("frame-dark"));
		pl_hline(c, wx + 2, wx + ww - 2, wy + wh - 2, colour("frame-dark"));
		pl_fill(c, wx + 1, wy + 1, wx + ww - 2, wy + 20, colour("title-base"));
		pl_hline(c, wx + 1, wx + ww - 2, wy + 21, colour("frame-border"));
		/* The theme's own bar tiles when it has them, else Platinum
		 * stripes. Buttons fall back to the Platinum boxes. */
		const QImage barLeft = artImageFor(m_draft, artSlot("bar-left"));
		const QImage barMiddle = artImageFor(m_draft, artSlot("bar-middle"));
		const QImage barRight = artImageFor(m_draft, artSlot("bar-right"));
		const QImage barSpan = artImageFor(m_draft, artSlot("bar-span"));
		const bool leftCluster = m_draft.chrome.value("title-cluster") == "left";
		const bool artBar = !barMiddle.isNull() || (leftCluster && !barSpan.isNull());
		const int titleX = wx + 26, titleR = wx + ww - 34;
		if (!barMiddle.isNull()) {
			const int lw = barLeft.isNull() ? 0 : std::min(barLeft.width(), 44);
			const int rw = barRight.isNull() ? 0 : std::min(barRight.width(), 44);
			if (lw > 0) {
				blitArt(c, barLeft, wx + 1, wy + 1, lw, 20);
			}
			if (rw > 0) {
				blitArt(c, barRight, wx + ww - 1 - rw, wy + 1, rw, 20);
			}
			const int middle = ww - 2 - lw - rw;
			if (middle > 0) {
				blitArt(c, barMiddle, wx + 1 + lw, wy + 1, middle, 20);
			}
		} else if (leftCluster && !barSpan.isNull()) {
			blitArt(c, barSpan, wx + 1, wy + 1, ww - 2, 20);
		}
		const QString previewName = m_name.text.trimmed().isEmpty()
			? QString("My Theme") : m_name.text.trimmed();
		Text title(previewName, ww - 70, PL_FONT_VIEWS);
		const int titleW = title.inkWidth();
		const int inkX = wx + (ww - titleW) / 2;
		if (!artBar) {
			for (int i = 0; i < 6; ++i) {
				const int yy = wy + 4 + i * 3;
				if (titleX < inkX - 6) {
					pl_hline(c, titleX, inkX - 6, yy, colour("title-stripe-light"));
					pl_hline(c, titleX + 1, inkX - 5, yy + 1, colour("title-stripe-dark"));
				}
				if (inkX + titleW + 5 < titleR) {
					pl_hline(c, inkX + titleW + 5, titleR, yy, colour("title-stripe-light"));
					pl_hline(c, inkX + titleW + 6, titleR + 1, yy + 1, colour("title-stripe-dark"));
				}
			}
		}
		const struct { const char *key; int x; } buttons[] = {
			{ "button-close", wx + 4 },
			{ "button-zoom", wx + ww - 33 },
			{ "button-grow", wx + ww - 17 },
		};
		for (const auto &button : buttons) {
			const QImage art = artImageFor(m_draft, artSlot(button.key));
			if (!art.isNull()) {
				blitArt(c, art, button.x, wy + 4, 13, 13);
			} else {
				pl_fill(c, button.x, wy + 4, button.x + 11, wy + 15, C_WHITE);
				pl_outline(c, button.x, wy + 4, button.x + 11, wy + 15, C_BLACK);
			}
		}
		pl_text(c, title.t, inkX, wy + 15, colour("title-text"));
		pl_fill(c, wx + 3, wy + 22, wx + ww - 4, wy + wh - 4, C_WHITE);
		pl_outline(c, wx + 3, wy + 22, wx + ww - 4, wy + wh - 4, GRAY(0x8));
		Text hello("Welcome to ZacOS", ww - 20, PL_FONT_VIEWS);
		pl_text(c, hello.t, wx + 10, wy + 40, C_BLACK);
		pl_hline(c, wx + 10, wx + ww - 12, wy + 46, GRAY(0xC));
	}

	/* An open File menu, with Open selected. */
	const int mx = x0 + 10, my = y0 + barH;
	const int mw = 108, mh = 62;
	pl_fill(c, mx + 2, my + 2, mx + mw + 1, my + mh + 1, GRAY(0x4));
	const QImage menuFace = artImageFor(m_draft, artSlot("menu-background-image"));
	if (!menuFace.isNull()) {
		tileArt(c, menuFace, mx, my, mx + mw - 1, my + mh - 1);
	} else {
		pl_fill(c, mx, my, mx + mw - 1, my + mh - 1, colour("menu-background"));
	}
	pl_outline(c, mx, my, mx + mw - 1, my + mh - 1, colour("menu-border"));
	pl_hline(c, mx + 1, mx + mw - 2, my + 1, colour("menu-light"));
	pl_vline(c, mx + 1, my + 1, my + mh - 2, colour("menu-light"));
	pl_vline(c, mx + mw - 2, my + 2, my + mh - 2, colour("menu-shadow"));
	pl_hline(c, mx + 2, mx + mw - 2, my + mh - 2, colour("menu-shadow"));
	Text itemNew("New", 80, PL_FONT_VIEWS), itemOpen("Open", 80, PL_FONT_VIEWS),
		itemQuit("Quit", 80, PL_FONT_VIEWS);
	pl_text(c, itemNew.t, mx + 12, my + 15, colour("menu-text"));
	const QImage menuHighlight = artImageFor(m_draft, artSlot("menu-highlight-image"));
	if (!menuHighlight.isNull()) {
		tileArt(c, menuHighlight, mx + 1, my + 18, mx + mw - 2, my + 34);
	} else {
		pl_fill(c, mx + 1, my + 18, mx + mw - 2, my + 34, accent.shadow);
		pl_hline(c, mx + 1, mx + mw - 2, my + 18, accent.dark);
	}
	pl_text(c, itemOpen.t, mx + 12, my + 31, colour("menu-text-selected"));
	pl_hline(c, mx + 8, mx + mw - 9, my + 39, colour("menu-separator"));
	pl_hline(c, mx + 8, mx + mw - 9, my + 40, colour("menu-separator-light"));
	pl_text(c, itemQuit.t, mx + 12, my + 54, colour("menu-text"));
}

void ThemeEditorWindow::paintEvent(QPaintEvent *) {
	Pixels px(W, H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, H - 1, FACE);

	panelText(c, "Theme name:", LEFT_X, 26, PL_FONT_SYSTEM);

	panelGroup(c, LEFT_X, TITLE_TOP, LEFT_R, TITLE_BOTTOM, "Window Title", FACE);
	panelGroup(c, LEFT_X, FRAME_TOP, LEFT_R, FRAME_BOTTOM, "Window Frame", FACE);
	panelGroup(c, LEFT_X, MENU_TOP, LEFT_R, MENU_BOTTOM, "Menus", FACE);
	panelGroup(c, RIGHT_X, TITLE_TOP, RIGHT_R, DESKTOP_BOTTOM, "Desktop", FACE);
	panelGroup(c, RIGHT_X, SOUND_TOP, RIGHT_R, SOUND_BOTTOM, "Sound", FACE);
	panelGroup(c, RIGHT_X, TEXT_TOP, RIGHT_R, TEXT_BOTTOM, "Text", FACE);
	panelGroup(c, RIGHT_X, PREVIEW_TOP, RIGHT_R, PREVIEW_BOTTOM, "Preview", FACE);

	static const int baselines[] = { 88, 112, 136, 160, 222, 246, 270, 294,
		356, 380, 404, 428 };
	for (size_t i = 0; i < m_wells.size(); ++i) {
		paintWell(c, m_wells[i], baselines[i]);
	}
	m_host.paintControls(c, FACE);
	paintPreview(c, m_previewRect);
	if (!m_error.isEmpty()) {
		const QStringList lines = panelWrap(m_error, LEFT_R - LEFT_X, PL_FONT_VIEWS);
		for (int i = 0; i < lines.size() && i < 2; ++i) {
			panelText(c, lines[i], LEFT_X, 462 + i * 12, PL_FONT_VIEWS,
				C_BLACK, LEFT_R - LEFT_X);
		}
	}
	QPainter p(this);
	px.blit(p);
}

void ThemeEditorWindow::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	for (const ColourWell &well : m_wells) {
		if (well.rect.contains(pos)) {
			chooseColour(well);
			return;
		}
	}
	m_host.hostPress(e);
}

void ThemeEditorWindow::mouseMoveEvent(QMouseEvent *e) {
	m_host.hostMove(e);
}

void ThemeEditorWindow::mouseReleaseEvent(QMouseEvent *e) {
	m_host.hostRelease(e);
}

void ThemeEditorWindow::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		cancel();
		return;
	}
	if (e->modifiers() & Qt::ControlModifier) {
		if (e->key() == Qt::Key_Z) {
			if (e->modifiers() & Qt::ShiftModifier) {
				redo();
			} else {
				undo();
			}
			return;
		}
		if (e->key() == Qt::Key_Y) {
			redo();
			return;
		}
	}
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	if (e->key() == Qt::Key_Escape) {
		cancel();
		return;
	}
	QWidget::keyPressEvent(e);
}

void ThemeEditorWindow::save() {
	m_error.clear();
	m_draft.name = m_name.text;
	if (!themeDraftSave(&m_draft, m_source, &m_error)) {
		update();
		return;
	}
	m_saved = true;
	close();
}

void ThemeEditorWindow::cancel() {
	if (m_dirty && !confirmDiscard()) {
		return;
	}
	m_saved = true;
	close();
}

bool ThemeEditorWindow::confirmDiscard() {
	const QString name = m_name.text.trimmed().isEmpty() ? m_draft.name : m_name.text.trimmed();
	return QMessageBox::question(this, "Theme Editor",
		"Discard your changes to \"" + name + "\"?",
		QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

void ThemeEditorWindow::closeEvent(QCloseEvent *e) {
	if (!m_saved && m_dirty && !confirmDiscard()) {
		e->ignore();
		return;
	}
	e->accept();
}

/* ---- per-slot window artwork ------------------------------------------ */

static constexpr int ART_W = 640, ART_H = 390;
static constexpr int ART_LIST_X = 16, ART_LIST_R = 360;
static constexpr int ART_PREVIEW_X = 376, ART_PREVIEW_R = 624;
static constexpr int ART_BUTTON_Y = 354;

ThemeArtworkWindow::ThemeArtworkWindow(ThemeDraft *draft, std::function<void()> begin,
		std::function<void()> changed, std::function<void()> undo,
		std::function<void()> redo, QWidget *parent)
	: QWidget(parent), m_draft(draft), m_begin(std::move(begin)),
	  m_changed(std::move(changed)), m_undo(std::move(undo)),
	  m_redo(std::move(redo)), m_slots(themeArtSlots()) {
	setWindowTitle("Window Artwork");
	setFixedSize(ART_W, ART_H);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);

	m_list.frame = QRect(ART_LIST_X + 10, 56, ART_LIST_R - ART_LIST_X - 20, 272);
	m_list.hint = "A piece of the window or menu chrome to paint.";
	m_list.picked = [this](int row) {
		if (row >= 0 && row < m_slots.size()) {
			m_selected = row;
			loadPreview();
			update();
		}
	};

	m_imported = PanelButton("Use Imported", QRect(ART_LIST_X, ART_BUTTON_Y, 110, 22));
	m_imported.hint = "Uses the artwork from the imported theme this copy is linked to.";
	m_imported.clicked = [this] { useImported(); };
	m_platinum = PanelButton("Platinum", QRect(ART_LIST_X + 118, ART_BUTTON_Y, 90, 22));
	m_platinum.hint = "Draws this part with the palette's flat Platinum paint.";
	m_platinum.clicked = [this] { usePlatinum(); };
	m_choose = PanelButton("Choose Image...", QRect(ART_LIST_X + 216, ART_BUTTON_Y, 132, 22));
	m_choose.hint = "Uses a PNG, JPEG or other picture for this part.";
	m_choose.clicked = [this] { chooseImage(); };
	m_all.rect = QRect(ART_LIST_X + 356, ART_BUTTON_Y, 160, PL_POPUP_H);
	m_all.items = { PopupItem("All Imported"), PopupItem("All Platinum") };
	m_all.hint = "Sets every part at once: the import's artwork where it has one, or flat Platinum paint.";
	m_all.chosen = [this](int choice) {
		m_all.selected = choice;
		m_begin();
		if (choice == 1) {
			themeDraftDropChromeArt(m_draft);
		} else {
			for (const ThemeArtSlot &slot : m_slots) {
				if (slot.defaultPath.isEmpty()) {
					if (!m_draft->importedChrome.value(slot.key).isEmpty()) {
						m_draft->chrome[slot.key] = m_draft->importedChrome.value(slot.key);
					} else {
						m_draft->chrome.remove(slot.key);
					}
				} else if (!m_draft->baseBundleDir.isEmpty() &&
						QFileInfo::exists(m_draft->baseBundleDir + "/" + slot.defaultPath)) {
					m_draft->chrome[slot.key] = slot.defaultPath;
				} else {
					m_draft->chrome[slot.key] = "off";
				}
				m_draft->artSources.remove(slot.key);
			}
			if (!m_draft->importedChrome.value("title-cluster").isEmpty()) {
				m_draft->chrome["title-cluster"] = m_draft->importedChrome.value("title-cluster");
			} else {
				m_draft->chrome.remove("title-cluster");
			}
		}
		m_changed();
		refreshList();
		loadPreview();
		update();
	};
	m_done = PanelButton("Done", QRect(ART_PREVIEW_R - 80, ART_BUTTON_Y, 80, 22), true);
	m_done.clicked = [this] { close(); };

	m_host.buttons = { &m_imported, &m_platinum, &m_choose, &m_done };
	m_host.popups = { &m_all };
	m_host.defaultButton = &m_done;
	m_host.cancelButton = &m_done;
	m_host.hintAreas.push_back({ &m_list.frame, m_list.hint });
	m_previewRect = QRect(ART_PREVIEW_X + 10, 56, ART_PREVIEW_R - ART_PREVIEW_X - 20, 238);
	m_host.hintAreas.push_back({ &m_previewRect,
		"The selected part. A checked pattern means transparency." });

	refreshList();
	m_list.select(0, false);
	loadPreview();
}

void ThemeArtworkWindow::reload() {
	refreshList();
	loadPreview();
	update();
}

void ThemeArtworkWindow::selectSlot(const QString &key) {
	for (int i = 0; i < m_slots.size(); ++i) {
		if (m_slots[i].key == key) {
			m_selected = i;
			m_list.select(i, false);
			m_list.ensureVisible(i);
			loadPreview();
			update();
			return;
		}
	}
}

void ThemeArtworkWindow::refreshList() {
	QStringList labels;
	for (const ThemeArtSlot &slot : m_slots) {
		labels << artStateName(themeArtState(*m_draft, slot)) + "  " + slot.group +
			" — " + slot.label;
	}
	m_list.setItems(labels);
	const int row = std::clamp(m_selected, 0, std::max(0, (int)m_slots.size() - 1));
	m_list.select(row, false);
	m_list.ensureVisible(row);
}

void ThemeArtworkWindow::loadPreview() {
	const ThemeArtSlot &slot = currentSlot();
	m_preview = artImageFor(*m_draft, slot);
	const ThemeArtState state = themeArtState(*m_draft, slot);
	QString info = artStateName(state) + " — " + slot.group + " — " + slot.label;
	if (!m_preview.isNull()) {
		info += QString(" (%1 × %2)").arg(m_preview.width()).arg(m_preview.height());
	} else if (state == ThemeArtState::Platinum) {
		info += " · flat Platinum paint";
	}
	m_info = info;
	const bool hasInherit = slot.defaultPath.isEmpty()
		? !m_draft->importedChrome.value(slot.key).isEmpty()
		: (!m_draft->baseBundleDir.isEmpty() &&
			QFileInfo::exists(m_draft->baseBundleDir + "/" + slot.defaultPath));
	m_imported.enabled = hasInherit;
}

void ThemeArtworkWindow::useImported() {
	const ThemeArtSlot &slot = currentSlot();
	if (!m_imported.enabled) {
		return;
	}
	m_begin();
	if (slot.defaultPath.isEmpty()) {
		m_draft->chrome[slot.key] = m_draft->importedChrome.value(slot.key);
	} else {
		m_draft->chrome[slot.key] = slot.defaultPath;
	}
	if ((slot.key == "bar-span" || slot.key == "bar-span-inactive") &&
			!m_draft->importedChrome.value("title-cluster").isEmpty()) {
		m_draft->chrome["title-cluster"] = m_draft->importedChrome.value("title-cluster");
	}
	m_draft->artSources.remove(slot.key);
	m_changed();
	refreshList();
	loadPreview();
	update();
}

void ThemeArtworkWindow::usePlatinum() {
	const ThemeArtSlot &slot = currentSlot();
	m_begin();
	if (slot.defaultPath.isEmpty()) {
		m_draft->chrome.remove(slot.key);
	} else {
		m_draft->chrome[slot.key] = "off";
	}
	m_draft->artSources.remove(slot.key);
	if (slot.key == "bar-span" || slot.key == "bar-span-inactive") {
		const QString span = m_draft->chrome.value("bar-span");
		const QString spanInactive = m_draft->chrome.value("bar-span-inactive");
		if ((span.isEmpty() || span == "off") &&
				(spanInactive.isEmpty() || spanInactive == "off") &&
				m_draft->importedChrome.value("title-cluster").isEmpty()) {
			m_draft->chrome.remove("title-cluster");
		}
	}
	m_changed();
	refreshList();
	loadPreview();
	update();
}

void ThemeArtworkWindow::chooseImage() {
	const ThemeArtSlot &slot = currentSlot();
	const QString path = QFileDialog::getOpenFileName(this, "Choose " + slot.label,
		QDir::homePath(), "Images (*.png *.jpg *.jpeg *.bmp *.gif *.tif *.tiff)");
	if (path.isEmpty()) {
		return;
	}
	const QImage probe(path);
	if (probe.isNull() || probe.width() > 4096 || probe.height() > 4096 ||
			(qint64)probe.width() * probe.height() > 4096LL * 1024) {
		QMessageBox::warning(this, "Window Artwork",
			"That image could not be read, or is too large for a window tile (4096 pixels a side, about four megapixels).");
		return;
	}
	if (slot.argb && slot.key.startsWith("button") &&
			(probe.width() > 64 || probe.height() > 64)) {
		QMessageBox::warning(this, "Window Artwork",
			"Title-bar button art must be at most 64 by 64 pixels; larger art is scaled down.");
		return;
	}
	if (slot.argb && slot.key.startsWith("bar") &&
			(probe.width() > 2048 || probe.height() > 256)) {
		QMessageBox::warning(this, "Window Artwork",
			"Title-bar art must be at most 2048 by 256 pixels.");
		return;
	}
	m_begin();
	m_draft->artSources[slot.key] = path;
	m_draft->chrome[slot.key] = "chrome/custom-" + slot.key +
		(slot.argb ? ".argb" : ".png");
	if (slot.key == "bar-span" || slot.key == "bar-span-inactive") {
		m_draft->chrome["title-cluster"] = "left";
	}
	m_changed();
	refreshList();
	loadPreview();
	update();
}

void ThemeArtworkWindow::paintPreviewArt(pl_canvas *c, const QRect &rect) const {
	for (int y = rect.top(); y <= rect.bottom(); ++y) {
		for (int x = rect.left(); x <= rect.right(); ++x) {
			const bool dark = ((x - rect.left()) / 8 + (y - rect.top()) / 8) % 2 == 0;
			pl_put(c, x, y, dark ? GRAY(0xB) : C_WHITE);
		}
	}
	if (m_preview.isNull()) {
		const QString label = themeArtState(*m_draft, currentSlot()) == ThemeArtState::Platinum
			? "Platinum paint" : "No artwork";
		Text shown(label, rect.width() - 8, PL_FONT_VIEWS);
		pl_text(c, shown.t, rect.left() + (rect.width() - shown.inkWidth()) / 2,
			rect.center().y() + 4, C_BLACK);
	} else {
		const int availW = std::max(1, rect.width() - 8);
		const int availH = std::max(1, rect.height() - 8);
		int dw = availW;
		int dh = std::max(1, m_preview.height() * availW / std::max(1, m_preview.width()));
		if (dh > availH) {
			dh = availH;
			dw = std::max(1, m_preview.width() * availH / std::max(1, m_preview.height()));
		}
		blitArt(c, m_preview, rect.left() + (rect.width() - dw) / 2,
			rect.top() + (rect.height() - dh) / 2, dw, dh);
	}
	pl_outline(c, rect.left(), rect.top(), rect.right(), rect.bottom(), GRAY(0x8));
}

void ThemeArtworkWindow::paintEvent(QPaintEvent *) {
	Pixels px(ART_W, ART_H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, ART_W - 1, ART_H - 1, FACE);
	panelText(c, "Window artwork", ART_LIST_X, 26, PL_FONT_SYSTEM);
	panelGroup(c, ART_LIST_X, 48, ART_LIST_R, 340, "Parts", FACE);
	panelGroup(c, ART_PREVIEW_X, 48, ART_PREVIEW_R, 340, "Preview", FACE);
	m_list.paint(c, false);
	m_host.paintControls(c, FACE);
	paintPreviewArt(c, m_previewRect);
	const QStringList lines = panelWrap(m_info, ART_PREVIEW_R - ART_PREVIEW_X - 20,
		PL_FONT_VIEWS);
	for (int i = 0; i < lines.size() && i < 3; ++i) {
		panelText(c, lines[i], ART_PREVIEW_X + 10, 306 + i * 12, PL_FONT_VIEWS,
			C_BLACK, ART_PREVIEW_R - ART_PREVIEW_X - 20);
	}
	QPainter p(this);
	px.blit(p);
}

void ThemeArtworkWindow::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_list.press(pos)) {
		update();
		return;
	}
	m_host.hostPress(e);
}

void ThemeArtworkWindow::mouseMoveEvent(QMouseEvent *e) {
	bool changed = m_list.move(e->position().toPoint());
	changed |= m_host.hostMove(e);
	if (changed) {
		update();
	}
}

void ThemeArtworkWindow::mouseReleaseEvent(QMouseEvent *e) {
	m_list.release();
	m_host.hostRelease(e);
	update();
}

void ThemeArtworkWindow::keyPressEvent(QKeyEvent *e) {
	if (e->modifiers() & Qt::ControlModifier) {
		if (e->key() == Qt::Key_Z) {
			if (e->modifiers() & Qt::ShiftModifier) {
				m_redo();
			} else {
				m_undo();
			}
			return;
		}
		if (e->key() == Qt::Key_Y) {
			m_redo();
			return;
		}
	}
	if (e->key() == Qt::Key_Escape) {
		close();
		return;
	}
	if (m_list.key(e->key(), e->text())) {
		update();
		return;
	}
	if (m_host.hostKey(e)) {
		update();
		return;
	}
	QWidget::keyPressEvent(e);
}
