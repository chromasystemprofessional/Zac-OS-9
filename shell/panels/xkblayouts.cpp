#include "xkblayouts.h"

#include <QCollator>
#include <QFile>
#include <QXmlStreamReader>
#include <algorithm>

extern "C" {
#include "kblayout.h"
}

QList<XkbLayout> xkbLayouts() {
	QString path = qEnvironmentVariable("ZACOS9_XKB_RULES");
	if (path.isEmpty()) {
		path = QFile::exists("/usr/share/X11/xkb/rules/evdev.xml") ?
			"/usr/share/X11/xkb/rules/evdev.xml" : "/usr/share/X11/xkb/rules/base.xml";
	}
	QFile file(path);
	QList<XkbLayout> layouts;
	if (!file.open(QIODevice::ReadOnly)) {
		return layouts;
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
				if (!item.code.isEmpty() && !item.name.isEmpty() && !layout.isEmpty() &&
						layout != "custom") {
					layouts << item;
				}
			}
		}
	}
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
