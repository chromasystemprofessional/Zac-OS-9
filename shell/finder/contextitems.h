#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/*
 * Contextual Menu Items, as Mac OS 9's folder of contextual-menu plug-ins:
 * desktop entries (.desktop, Type=Application) in the Contextual Menu
 * Items folder (System Folder) add commands to the Sniffer's contextual
 * menus. A command is offered when every item clicked matches its
 * MimeType= (none: anything; "inode/directory" for folders and window
 * backgrounds; "image/" and a star, a whole family; "all/allfiles" any
 * file but not folders) and runs its Exec= with them, as the
 * desktop-entry spec's %f %F %u %U say.
 */
struct ContextCommand {
	QString name;
	QString file; /* the .desktop file */
};

QString contextItemsDir();
/* The commands for these paths, sorted by name. */
QList<ContextCommand> contextCommands(const QString &dir, const QStringList &paths);
/* Run one on the paths; false (and why) if it couldn't start. */
bool runContextCommand(const ContextCommand &command, const QStringList &paths, QString *error);
