#pragma once

#include <QStringList>
#include <functional>
#include <vector>

struct plat_text;

struct FontFace {
	QString file, family, style, name;
	int index = 0;
	QString id() const;
};

QString userFontsPath();
bool fontUserDirectory(const QString &directory);
bool fontCanRemove(const QString &file);
bool fontCanInstall(const QString &file, QString *error);
std::vector<FontFace> fontInspect(const QString &file, QString *error);
const std::vector<FontFace> &fontCatalog();
/* What a Fonts folder shows. Mac OS 9 came with a few dozen fonts: the
 * Fonts folder has the free fonts that stand in for them, once each, and
 * the user's own; its Language Kits folder has every other font installed.
 * In a ZacOS session programs (and so the Language Kits folder) see only
 * the Fonts folder's: see fontMacFamilies(). */
std::vector<FontFace> fontFolderFaces(bool languageKits);
/* Those free fonts' families. In a ZacOS session they are the only
 * installed fonts programs see, with the user's own
 * (share/fontconfig/zacos9-fonts.conf, which must list the same). */
const QStringList &fontMacFamilies();
/* The user's own fonts, by file, in ~/.config/zacos9/fonts-own.conf,
 * which zacos9-fonts.conf includes so programs see them too. Rewritten
 * only when the list changes (programs reload changed font settings). */
QString fontOwnAcceptsPath();
bool fontWriteOwnAccepts();
/* The catalog read again (a Fonts window opening), its rendered names kept. */
void fontRescan();
/* Everything read again (fonts changed on disk). */
void fontInvalidate();
void fontWatchChanges(const std::function<void()> &changed);
plat_text *fontRenderName(const QString &name, int maxWidth,
	const QString &file, int index, int pixels = 12);
/* How many times a font file has been opened to draw with, for tests. */
int fontFacesOpened();
/* "A" in the face, for its suitcase icon (in the system font if the face
 * has no A). */
plat_text *fontRenderSample(const QString &file, int index, int pixels);
