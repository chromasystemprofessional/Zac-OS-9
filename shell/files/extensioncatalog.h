#pragma once

/*
 * The Extensions Manager's curated catalog: familiar names, icons and plain
 * descriptions for the kernel modules people would recognize, grouped one
 * entry per feature (all the Bluetooth modules are "Bluetooth"). Helper and
 * library modules are deliberately not listed; they appear only under
 * Show All, with their technical names.
 */

#include <QString>
#include <QStringList>
#include <vector>

#include "icons.h"

struct ExtensionEntry {
	QString key;  /* stable id, e.g. "bluetooth" */
	QString name; /* e.g. "Bluetooth" */
	pl_icon_kind icon = PL_ICON_EXT_GENERIC;
	QString description;
	QStringList modules; /* loaded modules, or the catalog's for a disabled entry */
	bool startup = false; /* one of them is listed in modules-load.d */
};

/* The entries with at least one loaded module, sorted by name. Modules
 * may be given with '-' or '_'; the kernel treats them the same. */
std::vector<ExtensionEntry> curatedExtensions(const QStringList &loaded,
		const QStringList &startup = {});

/* Catalog entries with no loaded module, sorted by name. Their modules are
 * the catalog's patterns, for the Extensions (Disabled) view. */
std::vector<ExtensionEntry> disabledExtensions(const QStringList &loaded,
		const QStringList &startup = {});

/* The catalog entry for a key, without any loaded modules; empty key if
 * there is none. */
ExtensionEntry extensionEntry(const QString &key);

/* The icon for one module: its catalog entry's, or the generic piece. */
pl_icon_kind extensionModuleIcon(const QString &module);

/* The modprobe.d file the helper blacklists a turned-off extension's
 * modules in: $ZACOS9_MODPROBE_CONF (tests), else
 * /etc/modprobe.d/zacos9-extensions.conf. */
QString blacklistPath();

/* The modules a `blacklist <module>` file names, '-' and '_' the same,
 * once each and sorted. An absent file turns nothing off. A path given
 * overrides blacklistPath(), for tests. */
QStringList blacklistedModules(const QString &path = {});
