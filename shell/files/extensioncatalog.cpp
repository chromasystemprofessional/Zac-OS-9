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

} // namespace

std::vector<ExtensionEntry> curatedExtensions(const QStringList &loaded,
		const QStringList &startup) {
	QStringList startupModules;
	for (const QString &module : startup) {
		startupModules << normalize(module);
	}
	std::vector<ExtensionEntry> out;
	for (const QString &raw : loaded) {
		const QString module = normalize(raw);
		const pl_extension *entry = entryFor(module);
		if (!entry) {
			continue;
		}
		auto it = std::find_if(out.begin(), out.end(), [entry](const ExtensionEntry &e) {
			return e.key == QLatin1String(entry->key);
		});
		if (it == out.end()) {
			out.push_back(fromCatalog(*entry));
			it = out.end() - 1;
		}
		if (!it->modules.contains(module)) {
			it->modules << module;
		}
		it->startup = it->startup || startupModules.contains(module);
	}
	for (ExtensionEntry &entry : out) {
		entry.modules.sort();
	}
	std::sort(out.begin(), out.end(), [](const ExtensionEntry &a, const ExtensionEntry &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return out;
}

std::vector<ExtensionEntry> disabledExtensions(const QStringList &loaded,
		const QStringList &startup) {
	QStringList loadedKeys;
	for (const QString &raw : loaded) {
		if (const pl_extension *entry = entryFor(normalize(raw))) {
			loadedKeys << QString::fromLatin1(entry->key);
		}
	}
	QStringList startupModules;
	for (const QString &module : startup) {
		startupModules << normalize(module);
	}
	std::vector<ExtensionEntry> out;
	for (int i = 0; i < pl_n_extensions; i++) {
		const pl_extension &catalog = pl_extensions[i];
		const QString key = QString::fromLatin1(catalog.key);
		if (loadedKeys.contains(key)) {
			continue;
		}
		ExtensionEntry entry = fromCatalog(catalog);
		for (const char *const *module = catalog.modules; *module; module++) {
			entry.modules << QString::fromLatin1(*module);
		}
		entry.modules.sort();
		for (const QString &module : entry.modules) {
			entry.startup = entry.startup || startupModules.contains(module);
		}
		out.push_back(entry);
	}
	std::sort(out.begin(), out.end(), [](const ExtensionEntry &a, const ExtensionEntry &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
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
