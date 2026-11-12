#pragma once

#include <QString>
#include <vector>

/*
 * Network disks saved to mount when the user logs in, as Mac OS 9's
 * Startup Items folder held an alias to a mounted server volume.
 *
 * Each saved mount gets its own XDG autostart entry, named after the disk
 * and tagged X-ZacOS9-NetworkMount, so it appears in System Folder ▸
 * Startup Items with the network-disk icon. A registered user's password
 * lives in the keychain (the Secret Service, through secret-tool), never in
 * the saved list. The entry runs zacos9-netmount --mount-id ID.
 */

struct StartupMount {
	enum Kind { AFP, SMB };
	Kind kind = AFP;
	QString server;  /* address or host name, as typed */
	QString volume;  /* AFP volume; empty for SMB */
	QString share;   /* SMB share; empty for AFP */
	QString user;    /* empty for Guest */
	bool guest = true;
	bool cleartext = false; /* AFP: the server only accepts an unencrypted password */
};

/* $ZACOS9_NETWORK_MOUNTS, else $XDG_CONFIG_HOME/zacos9/network-mounts.json. */
QString startupMountsFile();
/* $ZACOS9_NETWORK_AUTOSTART_DIR, else $XDG_CONFIG_HOME/autostart. */
QString startupMountsAutostartDir();

/* A stable desktop-file ID for the mount: the Startup Items entry's name,
 * and the argument zacos9-netmount takes. */
QString startupMountId(const StartupMount &mount);
QString startupMountAutostartFile(const StartupMount &mount);

/* The saved mounts, in the order they were added; malformed entries are
 * skipped rather than making every other disk fail. */
std::vector<StartupMount> startupMounts();
/* True when the mount is saved and its Startup Items entry is enabled. */
bool startupMountExists(const StartupMount &mount);
/* Save or update one mount: the keychain first, then the list and its
 * Startup Items entry. On failure nothing is left half-saved and `error`
 * says what happened. */
bool saveStartupMount(const StartupMount &mount, const QString &password, QString *error = nullptr);
/* Forget one mount, its keychain item and its Startup Items entry. */
bool forgetStartupMount(const StartupMount &mount, QString *error = nullptr);
/* The same, for the Startup Items entry the Finder is removing; false if
 * no saved mount has that ID. */
bool forgetStartupMountById(const QString &id, QString *error = nullptr);
/* A registered mount's password from the keychain; empty if it is missing. */
QString startupMountPassword(const StartupMount &mount);
/* Whether secret-tool is installed; Guest mounts need no keychain. */
bool startupMountKeychainAvailable();
