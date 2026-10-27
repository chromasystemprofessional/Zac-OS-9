#include "extensioncatalog.h"

#include <algorithm>

#include <QFile>
#include <QTextStream>

#include "extensions.h"

namespace {

QString normalize(QString module) {
	return module.replace('-', '_');
}

const pl_extension *entryFor(const QString &module) {
	return pl_extension_for_module(module.toLatin1().constData());
}

ExtensionEntry fromCatalog(const pl_extension &entry) {
	ExtensionEntry out;
	out.key = QString::fromLatin1(entry.key);
	out.name = QString::fromUtf8(entry.name);
	out.icon = entry.icon;
	out.description = QString::fromUtf8(entry.description);
	return out;
}

QStringList normalized(const QStringList &modules) {
	QStringList out;
	for (const QString &module : modules) {
		const QString name = normalize(module);
		if (!out.contains(name)) {
			out << name;
		}
	}
	return out;
}

/* Does this module belong to the catalog entry? A pattern claims its
 * family, and the more specific entry claims a module first, so a codec
 * belongs to Built-in Sound and not to HDMI Sound. */
bool belongsTo(const pl_extension &entry, const QString &module) {
	return entryFor(module) == &entry;
}

/* The entry's modules that are loaded or blacklisted, sorted. */
QStringList modulesOf(const pl_extension &entry, const QStringList &loaded,
		const QStringList &blacklisted) {
	QStringList out;
	for (const QStringList *list : { &loaded, &blacklisted }) {
		for (const QString &raw : *list) {
			const QString module = normalize(raw);
			if (belongsTo(entry, module) && !out.contains(module)) {
				out << module;
			}
		}
	}
	out.sort();
	return out;
}

/* Every module the entry names as a plain name: modprobe.d cannot
 * blacklist a pattern. */
QStringList exactModulesOf(const pl_extension &entry) {
	QStringList out;
	for (const char *const *module = entry.modules; *module; module++) {
		const QString name = normalize(QString::fromLatin1(*module));
		if (!name.contains(QLatin1Char('*')) && !out.contains(name)) {
			out << name;
		}
	}
	out.sort();
	return out;
}

bool anyStartup(const pl_extension &entry, const QStringList &startup) {
	for (const QString &module : startup) {
		if (belongsTo(entry, normalize(module))) {
			return true;
		}
	}
	return false;
}

/* Turned off: the entry has modules on this computer, and every one of
 * them is blacklisted. Anything shorter than all of them (a stray line,
 * an older blacklist) leaves it on, so it says what would really happen. */
bool turnedOff(const pl_extension &entry, const QStringList &loaded,
		const QStringList &blacklisted) {
	const QStringList modules = modulesOf(entry, loaded, blacklisted);
	if (modules.isEmpty()) {
		return false;
	}
	return std::all_of(modules.begin(), modules.end(), [&blacklisted](const QString &module) {
		return blacklisted.contains(normalize(module));
	});
}

void sortByName(std::vector<ExtensionEntry> &entries) {
	std::sort(entries.begin(), entries.end(), [](const ExtensionEntry &a, const ExtensionEntry &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
}

} // namespace

std::vector<ExtensionEntry> curatedExtensions(const QStringList &loaded,
		const QStringList &startup, const QStringList &blacklisted) {
	const QStringList off = normalized(blacklisted);
	std::vector<ExtensionEntry> out;
	for (int i = 0; i < pl_n_extensions; i++) {
		const pl_extension &catalog = pl_extensions[i];
		if (turnedOff(catalog, loaded, off)) {
			continue;
		}
		ExtensionEntry entry = fromCatalog(catalog);
		entry.modules = modulesOf(catalog, loaded, {});
		entry.startup = anyStartup(catalog, startup);
		out.push_back(entry);
	}
	sortByName(out);
	return out;
}

std::vector<ExtensionEntry> disabledExtensions(const QStringList &loaded,
		const QStringList &startup, const QStringList &blacklisted) {
	const QStringList off = normalized(blacklisted);
	std::vector<ExtensionEntry> out;
	for (int i = 0; i < pl_n_extensions; i++) {
		const pl_extension &catalog = pl_extensions[i];
		if (!turnedOff(catalog, loaded, off)) {
			continue;
		}
		ExtensionEntry entry = fromCatalog(catalog);
		entry.modules = modulesOf(catalog, loaded, off);
		entry.startup = anyStartup(catalog, startup);
		out.push_back(entry);
	}
	sortByName(out);
	return out;
}

QStringList extensionToggleModules(const QString &key, bool on,
		const QStringList &loaded, const QStringList &blacklisted) {
	const pl_extension *entry = pl_extension_find(key.toLatin1().constData());
	if (!entry) {
		return {};
	}
	QStringList out = modulesOf(*entry, loaded, blacklisted);
	if (!on) {
		out += exactModulesOf(*entry);
	}
	out.removeDuplicates();
	out.sort();
	return out;
}

ExtensionEntry extensionEntry(const QString &key) {
	const pl_extension *entry = pl_extension_find(key.toLatin1().constData());
	return entry ? fromCatalog(*entry) : ExtensionEntry{};
}

pl_icon_kind extensionModuleIcon(const QString &module) {
	const pl_extension *entry = entryFor(normalize(module));
	return entry ? entry->icon : PL_ICON_EXT_GENERIC;
}

QString blacklistPath() {
	const QString path = qEnvironmentVariable("ZACOS9_MODPROBE_CONF");
	return path.isEmpty() ? QStringLiteral("/etc/modprobe.d/zacos9-extensions.conf") : path;
}

QStringList blacklistedModules(const QString &path) {
	QFile file(path.isEmpty() ? blacklistPath() : path);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return {};
	}
	QStringList out;
	QTextStream in(&file);
	while (!in.atEnd()) {
		QString line = in.readLine();
		const int comment = line.indexOf(QLatin1Char('#'));
		if (comment >= 0) {
			line.truncate(comment);
		}
		const QStringList words = line.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
		if (words.size() < 2 || words.first() != QLatin1String("blacklist")) {
			continue;
		}
		const QString module = normalize(words[1]);
		if (!out.contains(module)) {
			out << module;
		}
	}
	out.sort();
	return out;
}
