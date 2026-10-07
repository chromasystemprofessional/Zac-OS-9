#include "extensioncatalog.h"

#include <algorithm>

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

ExtensionEntry extensionEntry(const QString &key) {
	const pl_extension *entry = pl_extension_find(key.toLatin1().constData());
	return entry ? fromCatalog(*entry) : ExtensionEntry{};
}

pl_icon_kind extensionModuleIcon(const QString &module) {
	const pl_extension *entry = entryFor(normalize(module));
	return entry ? entry->icon : PL_ICON_EXT_GENERIC;
}
