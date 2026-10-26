#pragma once

#include <QString>
#include <QStringList>
#include <vector>

/* FreeDesktop autostart discovery (Desktop Entry + Autostart specs), parsed
 * with GLib's GKeyFile. The user directory overrides system directories by
 * desktop-file ID (file name); a user entry with Hidden=true masks a system
 * one. Entries are read-only data here; only autostartSetEnabled writes, and
 * only inside the user's own autostart directory. */
struct AutostartEntry {
	QString id;          /* file name, e.g. "foo.desktop" */
	QString name;        /* localized Name, else the ID's base name */
	QString description; /* localized Comment */
	QString path;
	QString command;     /* Exec as written; never shown by Finder */
	bool userEntry = false;     /* the user's file is the effective one */
	bool overridesSystem = false;
	bool enabled = false;
	QString reason;      /* why it is not enabled; empty when enabled */
};

struct AutostartContext {
	QString userDir;
	QStringList systemDirs;
	QStringList desktops; /* XDG_CURRENT_DESKTOP components */
	QString locale;       /* e.g. "fr_FR.UTF-8"; empty = process locale */
};

AutostartContext autostartDefaultContext();
/* Every effective entry (enabled or not), sorted by name, one per desktop-file
 * ID. Distinct IDs are preserved even when they run the same command. */
std::vector<AutostartEntry> autostartEntries(const AutostartContext &context);
std::vector<AutostartEntry> autostartEntries();
/* Enable or disable by ID through a user-directory override. The system
 * file is never touched. */
bool autostartSetEnabled(const AutostartContext &context, const QString &id, bool enabled,
	QString *error = nullptr);
/* Startup Items: make the application whose desktop entry is `desktopFile`
 * (desktop-file ID `id`) open at login. A disabled entry of that ID is
 * re-enabled; otherwise the application's entry is copied into the user's
 * autostart directory, without the menu-only desktop filters, since the
 * user asked for it here. Already enabled is success. */
bool autostartAddApplication(const AutostartContext &context, const QString &id,
	const QString &desktopFile, QString *error = nullptr);
/* Stop `id` opening at login. An entry only the user has is moved to the
 * Trash (so it can be dragged back); one from a system directory is masked
 * with a Hidden=true user override. `trashed` says which happened. */
bool autostartRemove(const AutostartContext &context, const QString &id,
	bool *trashed = nullptr, QString *error = nullptr);
/* Launch the effective entries once for this login session. */
bool autostartRunSession(QString *error = nullptr);
bool autostartRunSession(const AutostartContext &context, QString *error = nullptr);
