#pragma once

/*
 * Extensions Manager's sets (Mac OS 9's Extensions Manager sets, gap plan 4.3):
 * named on/off choices for login items and user services, saved in
 * ~/.config/zacos9/Tricks Sets/NAME.set ("login<TAB>ID<TAB>on|off" and
 * "service<TAB>UNIT<TAB>on|off" lines). The set last chosen is
 * "tricks-set" in desktop.conf.
 */

#include <QMap>
#include <QString>
#include <QStringList>

struct TrickSet {
	QMap<QString, bool> loginItems, services;
};

QStringList trickSetNames();
bool readTrickSet(const QString &name, TrickSet *set);
bool saveTrickSet(const QString &name, const TrickSet &set, QString *error = nullptr);
bool deleteTrickSet(const QString &name);
QString currentTrickSet();
void setCurrentTrickSet(const QString &name);
