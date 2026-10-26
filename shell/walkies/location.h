#pragma once

/*
 * Walkies (Mac OS 9's Location Manager, gap plan 8.5): named places, each
 * remembering the network connection, sound output and its volume, time
 * zone, default printer and Extensions Manager set, so one choice switches them
 * all. Kept in ~/.config/zacos9/Walkies/NAME.location (key=value lines);
 * the place last switched to is "walkies-location" in desktop.conf.
 */

#include <QList>
#include <QString>
#include <QStringList>

struct Location {
	QString name;
	QString network;   /* a NetworkManager connection's name */
	QString sink;      /* a PulseAudio/PipeWire sink */
	int volume = -1;   /* percent, -1: leave it */
	QString timezone;  /* Europe/London */
	QString printer;   /* a CUPS queue */
	QString trickSet;  /* a Extensions Manager set (shell/files/tricksets.h) */
};

QStringList locationNames();
bool readLocation(const QString &name, Location *location);
bool saveLocation(const Location &location, QString *error = nullptr);
bool deleteLocation(const QString &name);
QString currentLocation();
void setCurrentLocation(const QString &name);

/* One command to run. */
struct WalkiesStep {
	QString what;      /* "the network", shown when it fails */
	QString program;
	QStringList args;
};

/* What switching to `location` runs, in order (the Extensions Manager set's
 * login items are switched apart, in the process). */
QList<WalkiesStep> applySteps(const Location &location, const QMap<QString, bool> &services);
/* What capturing the current settings runs, and how each answer is read. */
QList<WalkiesStep> captureSteps();
/* Fills in `location` from captureSteps()' outputs, in the same order. */
void parseCapture(const QStringList &outputs, Location *location);
/* A line for the window: "Network: Home Wi-Fi · Sound: … · …". */
QString describe(const Location &location);
