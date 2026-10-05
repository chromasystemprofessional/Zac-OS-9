#pragma once

#include <QStringList>
#include <functional>

class QObject;
QString soundThemesFolder();
QString appearanceThemesFolder();
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
