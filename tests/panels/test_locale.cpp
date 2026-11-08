/* The Text panel's language choice: unlike Numbers, it must write LANGUAGE
 * as well as LANG, or the installer's LANGUAGE keeps winning in gettext. */
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <cstdio>
#include <cstring>
#include <memory>

#include "panelregistry.h"
#include "process.h"

static int fails;
static void check(bool ok, const char *what) {
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	fails += !ok;
}

static QWidget *panel(const char *name) {
	for (const PanelInfo &info : panels()) {
		if (strcmp(info.name, name) == 0) {
			return info.make().release();
		}
	}
	return nullptr;
}

static void type(QWidget *w, const char *text) {
	for (const char *p = text; *p; p++) {
		QKeyEvent event(QEvent::KeyPress, 0, Qt::NoModifier, QString(QLatin1Char(*p)));
		QCoreApplication::sendEvent(w, &event);
	}
}

/* The test selects en_US, which every test machine has; the helper must not
 * be needed for that. */
QString runPrivilegedHelper(const QString &, const QStringList &, const QByteArray &,
		bool *ok, QString *err) {
	if (ok) {
		*ok = false;
	}
	if (err) {
		*err = "unexpected privileged helper call";
	}
	return {};
}

int main(int argc, char **argv) {
	QTemporaryDir home;
	qputenv("HOME", home.path().toUtf8());
	qputenv("XDG_CONFIG_HOME", (home.path() + "/config").toUtf8());
	qputenv("LANG", "en_US.UTF-8");
	qputenv("LANGUAGE", "en");
	QApplication app(argc, argv);

	/* The package-build container need not have en_US.UTF-8 generated.
	 * Make a private one so the panel's localeAvailable() check is real. */
	QTemporaryDir localeRoot;
	const ProcessResult localedef = runProcess("localedef",
		{ "-i", "en_US", "-f", "UTF-8", localeRoot.path() + "/en_US.UTF-8" });
	if (!localedef.ok()) {
		std::printf("FAIL  localedef: %s%s\n",
			localedef.error.toUtf8().constData(), localedef.err.constData());
		return 1;
	}
	qputenv("LOCPATH", localeRoot.path().toUtf8());

	std::unique_ptr<QWidget> text(panel("text"));
	check(text != nullptr, "the Text panel is registered");
	type(text.get(), "American");
	QFile conf(home.path() + "/config/locale.conf");
	conf.open(QIODevice::ReadOnly);
	const QByteArray text_conf = conf.readAll();
	conf.close();
	check(text_conf.contains("LANG=en_US.UTF-8") &&
		text_conf.contains("LANGUAGE=en") &&
		text_conf.contains("LC_COLLATE=en_US.UTF-8"),
		"the Text panel writes LANG, LANGUAGE and the sort order");

	QFile::remove(home.path() + "/config/locale.conf");
	std::unique_ptr<QWidget> numbers(panel("numbers"));
	check(numbers != nullptr, "the Numbers panel is registered");
	type(numbers.get(), "American");
	conf.open(QIODevice::ReadOnly);
	const QByteArray numbers_conf = conf.readAll();
	check(!numbers_conf.contains("LANGUAGE"),
		"the Numbers panel leaves the interface language alone");
	return fails ? 1 : 0;
}
