#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/* A keyboard layout as the Keyboard panel lists it: its xkb code ("us",
 * "us(dvorak)") and readable name ("English (Dvorak)"). */
struct XkbLayout {
	QString code, name;
};

/* Every layout and variant this system's xkb knows (rules/evdev.xml, or
 * $ZACOS9_XKB_RULES), sorted by name. */
QList<XkbLayout> xkbLayouts();
/* The layouts in use: the Keyboard panel's choice, else Debian's
 * /etc/default/keyboard (as zacos9-wm reads them, compositor/src/kblayout.c). */
QStringList xkbLayoutsInUse();
