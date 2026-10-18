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
void fontInvalidate();
void fontWatchChanges(const std::function<void()> &changed);
plat_text *fontRenderName(const QString &name, int maxWidth,
	const QString &file, int index);
