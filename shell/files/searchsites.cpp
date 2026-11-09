#include "searchsites.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QXmlStreamReader>

#include "appdb.h"
#include "settings.h"
#include "tr.h"

QString systemSearchSitesFolder() {
	return QString::fromUtf8(pl_data_dir()) + "/Internet Search Sites";
}

QString userSearchSitesFolder() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
		"/zacos9/Internet Search Sites";
}

static QString unquote(QString value) {
	value = value.trimmed();
	if (value.size() >= 2 && ((value.startsWith('"') && value.endsWith('"')) ||
			(value.startsWith('\'') && value.endsWith('\'')))) {
		value = value.mid(1, value.size() - 2);
	}
	return value.trimmed();
}

static QString unescape(QString value) {
	value.replace("&amp;", "&");
	value.replace("&quot;", "\"");
	value.replace("&apos;", "'");
	value.replace("&lt;", "<");
	value.replace("&gt;", ">");
	return value;
}

static bool prepareUrl(QString *url, QString *error) {
	url->replace("%s", "{query}");
	if (!url->contains("{query}")) {
		*url += (url->contains('?') ? "&" : "?") + QStringLiteral("q={query}");
	}
	QString probe = *url;
	probe.replace("{query}", "test");
	const QUrl parsed(probe, QUrl::StrictMode);
	if (!parsed.isValid() || (parsed.scheme() != "http" && parsed.scheme() != "https")) {
		if (error) {
			*error = tr9("“%1” isn't an http or https search URL.").arg(*url);
		}
		return false;
	}
	return true;
}

static bool readOpenSearch(const QByteArray &data, SearchSite *site, QString *error) {
	QXmlStreamReader xml(data);
	QString shortName, description, url, channel, icon;
	while (!xml.atEnd()) {
		if (xml.readNext() != QXmlStreamReader::StartElement) {
			continue;
		}
		const QString tag = xml.name().toString();
		if (tag == "ShortName") {
			shortName = xml.readElementText().trimmed();
		} else if (tag == "Description") {
			description = xml.readElementText().trimmed();
		} else if (tag == "Channel") {
			channel = xml.readElementText().trimmed();
		} else if (tag == "Image") {
			icon = xml.readElementText().trimmed();
		} else if (tag == "Url") {
			const QString type = xml.attributes().value("type").toString();
			const QString tmpl = xml.attributes().value("template").toString();
			if ((type.isEmpty() || type == "text/html") && !tmpl.isEmpty() && url.isEmpty()) {
				url = tmpl;
			}
		}
	}
	if (xml.hasError()) {
		if (error) {
			*error = tr9("The OpenSearch site “%1” is damaged.").arg(site->path);
		}
		return false;
	}
	site->name = shortName.isEmpty() ? description : shortName;
	if (site->name.isEmpty()) {
		site->name = QFileInfo(site->path).completeBaseName();
	}
	site->url = url;
	site->url.replace("{searchTerms}", "{query}");
	site->channel = channel;
	site->icon = icon;
	if (site->url.isEmpty()) {
		if (error) {
			*error = tr9("The OpenSearch site “%1” has no search URL.").arg(site->path);
		}
		return false;
	}
	return prepareUrl(&site->url, error);
}

static bool readSearchSite(const QByteArray &data, SearchSite *site, QString *error) {
	QString name, url, channel, icon;
	for (const QString &raw : QString::fromUtf8(data).split('\n')) {
		const QString line = raw.trimmed();
		if (line.isEmpty() || line.startsWith('#') || line.startsWith(';') ||
				line.startsWith('[')) {
			continue;
		}
		const int at = line.indexOf('=');
		if (at <= 0) {
			continue;
		}
		const QString key = line.left(at).trimmed().toLower();
		const QString value = unquote(line.mid(at + 1));
		if (key == "name") {
			name = value;
		} else if (key == "url") {
			url = value;
		} else if (key == "channel") {
			channel = value;
		} else if (key == "icon") {
			icon = value;
		}
	}
	site->name = name.isEmpty() ? QFileInfo(site->path).completeBaseName() : name;
	site->url = url;
	site->channel = channel;
	site->icon = icon;
	if (site->url.isEmpty()) {
		if (error) {
			*error = tr9("The search site “%1” has no URL.").arg(site->path);
		}
		return false;
	}
	return prepareUrl(&site->url, error);
}

static bool readSherlockSource(const QByteArray &data, SearchSite *site, QString *error) {
	const QString text = QString::fromUtf8(data);
	QString name = text;
	QString url;
	QString channel;
	QRegularExpressionMatch match = QRegularExpression(
		"<name>([^<]+)</name>", QRegularExpression::CaseInsensitiveOption).match(text);
	if (match.hasMatch()) {
		name = match.captured(1).trimmed();
	}
	match = QRegularExpression("<channel>([^<]+)</channel>",
		QRegularExpression::CaseInsensitiveOption).match(text);
	if (match.hasMatch()) {
		channel = match.captured(1).trimmed();
	}
	match = QRegularExpression(
		"<search[^>]*\\baction\\s*=\\s*[\"']([^\"']+)[\"']",
		QRegularExpression::CaseInsensitiveOption).match(text);
	if (match.hasMatch()) {
		url = unescape(match.captured(1));
	} else {
		match = QRegularExpression("<url>([^<]+)</url>",
			QRegularExpression::CaseInsensitiveOption).match(text);
		if (match.hasMatch()) {
			url = unescape(match.captured(1));
		}
	}
	site->name = name.isEmpty() ? QFileInfo(site->path).completeBaseName() : name;
	site->url = url;
	site->channel = channel;
	if (site->url.isEmpty()) {
		if (error) {
			*error = tr9("The Sherlock search site “%1” has no search action.").arg(site->path);
		}
		return false;
	}
	return prepareUrl(&site->url, error);
}

bool readSearchSiteFile(const QString &path, SearchSite *site, QString *error) {
	if (error) {
		error->clear();
	}
	if (!site) {
		if (error) {
			*error = tr9("No search-site destination was provided.");
		}
		return false;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		if (error) {
			*error = tr9("Could not read the search site “%1”: %2").arg(path, file.errorString());
		}
		return false;
	}
	const QByteArray data = file.read(256 * 1024);
	*site = SearchSite{};
	site->path = path;
	const QString suffix = QFileInfo(path).suffix().toLower();
	bool ok = false;
	if (suffix == "xml") {
		ok = readOpenSearch(data, site, error);
	} else if (suffix == "src") {
		ok = readSherlockSource(data, site, error);
	} else {
		ok = readSearchSite(data, site, error);
	}
	if (!ok) {
		*site = SearchSite{};
	}
	return ok;
}

QList<SearchSite> searchSites(QString *error) {
	if (error) {
		error->clear();
	}
	QList<SearchSite> sites;
	QHash<QString, int> byName;
	QStringList problems;
	auto load = [&](const QString &folder, bool user) {
		const QFileInfoList files = QDir(folder).entryInfoList(
			QDir::Files | QDir::Readable, QDir::Name);
		for (const QFileInfo &info : files) {
			SearchSite site;
			QString problem;
			if (!readSearchSiteFile(info.absoluteFilePath(), &site, &problem)) {
				if (!problem.isEmpty()) {
					problems << problem;
				}
				continue;
			}
			site.user = user;
			const QString key = site.name.toLower();
			if (byName.contains(key)) {
				sites[byName.value(key)] = site;
			} else {
				byName.insert(key, sites.size());
				sites << site;
			}
		}
	};
	load(systemSearchSitesFolder(), false);
	load(userSearchSitesFolder(), true);
	if (error && !problems.isEmpty()) {
		*error = problems.join('\n');
	}
	return sites;
}

QStringList webSearchChannels() {
	QStringList names;
	for (const SearchSite &site : searchSites(nullptr)) {
		names << site.name;
	}
	return names;
}

const SearchSite *searchSiteNamed(const QList<SearchSite> &sites, const QString &name) {
	for (const SearchSite &site : sites) {
		if (site.name.compare(name, Qt::CaseInsensitive) == 0) {
			return &site;
		}
	}
	return nullptr;
}

QString searchSiteUrl(const SearchSite &site, const QString &words) {
	QString url = site.url;
	url.replace("{query}", QString::fromLatin1(QUrl::toPercentEncoding(words.trimmed())));
	return url;
}

bool openSearchSite(const SearchSite &site, const QString &words, QString *error) {
	if (error) {
		error->clear();
	}
	const QString url = searchSiteUrl(site, words);
	if (!appOpenUrl(url)) {
		if (error) {
			*error = tr9("The web search could not be opened. Check that a web browser is available.");
		}
		return false;
	}
	return true;
}

bool openWebSearch(const QString &site, const QString &words, QString *error) {
	if (error) {
		error->clear();
	}
	const QList<SearchSite> sites = searchSites(nullptr);
	const SearchSite *found = searchSiteNamed(sites, site);
	if (!found) {
		if (error) {
			*error = tr9("The search site “%1” isn't installed any more.").arg(site);
		}
		return false;
	}
	return openSearchSite(*found, words, error);
}
