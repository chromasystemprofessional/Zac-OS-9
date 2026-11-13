#pragma once

#include <QMap>
#include <QList>
#include <QStringList>
#include <functional>

class QObject;
QString soundThemesFolder();
QString userAlertSoundsFolder();
bool importAlertSound(const QString &source, QString *id, QString *error);
/* Every alert sound the panels offer, in order: Chime, each WAVE in the
 * installed set and then in the user's own folder, and None. The Sound and
 * Appearance panels and the System Folder's Sounds folder all read this. */
QStringList alertSoundIds();
/* A sound's name as shown: "Chime" for the built-in one, else its id with
 * the first letter capitalized. */
QString alertSoundDisplayName(const QString &id);
QString appearanceThemesFolder();
QStringList themeFontFamilies();
/* One preset's stored settings (accent, highlight, pattern, wallpaper,
 * wallpaper-mode, alert-sound, sound-theme, theme-bundle, font-family),
 * for the Appearance panel's visual theme cards. */
QMap<QString, QString> appearanceThemeValues(int index);
void watchCustomThemes(QObject *owner, std::function<void()> changed);
int soundThemeCount();
QString soundThemeId(int index);
QString soundThemeName(int index);
bool applySoundTheme(int index, QString *error);
void previewSoundTheme(int index);
QStringList customThemeErrors();
int appearanceThemeCount();
QString appearanceThemeId(int index);
QString appearanceThemeName(int index);
bool applyAppearanceTheme(int index, QString *error);
bool saveAppearanceTheme(const QString &name, QString *error);
/* The preset file the catalog read the theme from (empty for Silver
 * Coat, which is built in). */
QString appearanceThemePath(int index);
/* The bundle a theme names, or empty. */
QString appearanceThemeBundle(int index);
/* Whether the theme is editable without touching an imported bundle:
 * the editor wrote its bundle, or it has none. */
bool appearanceThemeIsUser(int index);

/* The Theme Editor's working copy. `settings` are appearance-preset
 * values; `chrome` is the bundle's theme.conf (palette keys plus the
 * artwork keys copied from a linked import). */
struct ThemeDraft {
	QString name;
	QString bundleId;   /* user bundle to update; empty for a new one */
	QString inherits;   /* imported bundle supplying artwork, or empty */
	QString presetPath; /* preset being edited, or empty for a new theme */
	bool linked = false; /* the source preset belongs to an imported theme */
	QMap<QString, QString> settings;
	QMap<QString, QString> chrome;
	/* The inherited bundle's theme.conf and folder, so the Artwork window
	 * can offer "use the imported tile" per slot. */
	QMap<QString, QString> importedChrome;
	QString baseBundleDir;
	/* Slot key -> local image the user chose. Written into the bundle on
	 * Save, never serialised as a path. */
	QMap<QString, QString> artSources;
};

/* One editable piece of window/menu artwork. `defaultPath` is the
 * conventional bundle file the compositor reads when theme.conf names no
 * key (frame and menu skins); empty for the keyed title-bar/button tiles.
 * `argb` tiles are stored as width, height and straight RGBA. */
struct ThemeArtSlot {
	QString key;
	QString label;
	QString defaultPath;
	bool argb = false;
	QString group;
};
const QList<ThemeArtSlot> &themeArtSlots();/* Write one image as a chrome tile (.argb) or a PNG skin. */
bool themeArtWrite(const QString &source, const QString &path, bool argb, QString *error);
/* The slot's state from a draft: inherited, Platinum, or a custom image. */
enum class ThemeArtState { Inherit, Platinum, Custom };
ThemeArtState themeArtState(const ThemeDraft &draft, const ThemeArtSlot &slot);

/* Read a theme into a draft. Imported themes come back `linked`, with
 * their artwork keys and their bundle id in `inherits`: saving writes a
 * user bundle that points at them instead of changing the import. */
bool themeDraftFromPreset(int index, ThemeDraft *draft, QString *error);
/* Validate, write and apply a draft. `sourceIndex` is the preset being
 * edited, or -1 for a new theme. On success `draft` holds the bundle and
 * preset that were written. */
bool themeDraftSave(ThemeDraft *draft, int sourceIndex, QString *error);
/* Remove a user theme's preset and bundle. Imported themes are refused. */
bool themeDraftDelete(int index, QString *error);
/* Drop imported window and menu artwork from a draft: its palette then
 * paints the title bars, buttons and menus. Icons, pointers and fonts stay
 * inherited. */
void themeDraftDropChromeArt(ThemeDraft *draft);
/* A complete starting palette: Platinum's light chrome, or a dark one. */
QMap<QString, QString> themePaletteDefaults(bool dark);
