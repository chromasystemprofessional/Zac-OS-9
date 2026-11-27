#include "xkblayouts.h"

#include <QCollator>
#include <QFile>
#include <QXmlStreamReader>
#include <algorithm>
#include <climits>

extern "C" {
#include "kblayout.h"
#include "locate.h"
}

/* Adds the layouts and variants in an xkb rules XML file (evdev.xml's
 * shape) to `layouts`, without repeats. */
static void readLayouts(const QString &path, QList<XkbLayout> &layouts) {
	if (path.isEmpty()) {
		return;
	}
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}
	/* <layout><configItem><name>us</name><description>English (US)</description>
	 * </configItem><variantList><variant><configItem>... */
	QXmlStreamReader xml(&file);
	QString layout;
	bool inLayout = false, inVariant = false, inItem = false;
	XkbLayout item;
	while (!xml.atEnd()) {
		xml.readNext();
		const auto name = xml.name();
		if (xml.isStartElement()) {
			if (name == u"layout") {
				layout.clear();
				inLayout = true;
			} else if (name == u"variant") {
				inVariant = true;
			} else if (name == u"configItem" && inLayout) {
				inItem = true;
				item = {};
			} else if (inItem && name == u"name") {
				item.code = xml.readElementText();
			} else if (inItem && name == u"description") {
				item.name = xml.readElementText();
			}
		} else if (xml.isEndElement()) {
			if (name == u"layout") {
				inLayout = false;
			} else if (name == u"variant") {
				inVariant = false;
			} else if (name == u"configItem" && inItem && inLayout) {
				inItem = false;
				if (!inVariant) {
					layout = item.code;
				} else if (!layout.isEmpty()) {
					item.code = layout + "(" + item.code + ")";
				}
				/* "custom" is xkb's placeholder for a layout of one's own. */
				const bool known = std::any_of(layouts.begin(), layouts.end(),
					[&](const XkbLayout &l) { return l.code == item.code; });
				if (!known && !item.code.isEmpty() && !item.name.isEmpty() &&
						!layout.isEmpty() && layout != "custom") {
					layouts << item;
				}
			}
		}
	}
}

/* ZacOS's additions to the system's list: variants Debian's xkb-data has
 * symbols for but leaves out of its rules XML, such as br(abnt2). Kept out
 * of /usr/share/X11/xkb, so an xkb-data upgrade cannot remove them.
 * $ZACOS9_XKB_EXTRA overrides the file; empty means none. */
static QString extraLayoutsPath() {
	const QString env = qEnvironmentVariable("ZACOS9_XKB_EXTRA");
	if (!env.isNull()) {
		return env;
	}
	char path[PATH_MAX];
	if (pl_source_path("share/zacos9/xkb/rules/zacos9.xml", path, sizeof(path)) &&
			QFile::exists(QString::fromUtf8(path))) {
		return QString::fromUtf8(path);
	}
	return "/usr/share/zacos9/xkb/rules/zacos9.xml";
}

QList<XkbLayout> xkbLayouts() {
	QString path = qEnvironmentVariable("ZACOS9_XKB_RULES");
	if (path.isEmpty()) {
		path = QFile::exists("/usr/share/X11/xkb/rules/evdev.xml") ?
			"/usr/share/X11/xkb/rules/evdev.xml" : "/usr/share/X11/xkb/rules/base.xml";
	}
	QList<XkbLayout> layouts;
	readLayouts(path, layouts);
	readLayouts(extraLayoutsPath(), layouts);
	QCollator collator;
	collator.setCaseSensitivity(Qt::CaseInsensitive);
	std::stable_sort(layouts.begin(), layouts.end(), [&](const XkbLayout &a, const XkbLayout &b) {
		return collator.compare(a.name, b.name) < 0;
	});
	return layouts;
}

QStringList xkbLayoutsInUse() {
	kb_layouts loaded;
	kb_layouts_load(&loaded);
	QStringList codes;
	for (int i = 0; i < loaded.n; i++) {
		codes << QString::fromUtf8(loaded.codes[i]);
	}
	return codes.isEmpty() ? QStringList{ "us" } : codes;
}
