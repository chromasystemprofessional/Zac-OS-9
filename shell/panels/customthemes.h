#pragma once

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
