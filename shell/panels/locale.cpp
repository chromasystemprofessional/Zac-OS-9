/* The Numbers and Text control panel (zacos9-controlpanel numbers | text). */
#include "controlpanel.h"
#include "conf.h"

/* Mac OS 9's Numbers and Text control panels, over the locale: Numbers sets
 * how numbers, money, dates and measurements are written (LC_NUMERIC,
 * LC_MONETARY, LC_TIME, LC_MEASUREMENT, LC_PAPER), Text the language and
 * sort order (LANG, LC_COLLATE). Kept in ~/.config/locale.conf, which
 * zacos9-session reads at login; a locale the system hasn't made yet is made
 * by zacos9-accounts-helper. */
class LocalePanel : public ControlPanel {
	static constexpr int G0 = 12, LIST_ROWS = 9;
	static constexpr int LIST_GROUP_H = 1 + PL_GROUP_MARGIN_TOP + LIST_ROWS * PL_LIST_ROW_H + 2 + PL_GROUP_MARGIN;
	static constexpr int G1 = G0 + LIST_GROUP_H + 20, SAMPLE_H = 1 + PL_GROUP_MARGIN_TOP + 4 * 14 + PL_GROUP_MARGIN;
	static constexpr int PANEL_H = G1 + SAMPLE_H + 34;

public:
	explicit LocalePanel(bool numbers)
		: ControlPanel(numbers ? "Numbers" : "Text", PANEL_H), m_numbers(numbers) {
		m_groups = { { G0, G0 + LIST_GROUP_H, numbers ? "Number Format" : "Language" },
			{ G1, G1 + SAMPLE_H, "Sample" } };
		m_list.frame = QRect(M + IN, G0 + 1 + PL_GROUP_MARGIN_TOP, W - 2 * M - 2 * IN - 2,
			LIST_ROWS * PL_LIST_ROW_H + 2);
		m_hintRects.push_back({ &m_list.frame, numbers
			? "Chooses how numbers, money, dates and measurements are written. Type to find one."
			: "Chooses the language programs use and how lists are sorted. Type to find one." });
		/* The system's UTF-8 locales, by their own name for themselves. */
		QFile supported("/usr/share/i18n/SUPPORTED");
		if (supported.open(QIODevice::ReadOnly)) {
			for (const QByteArray &line : supported.readAll().split('\n')) {
				const QList<QByteArray> f = line.simplified().split(' ');
				if (f.size() == 2 && f[1] == "UTF-8" && !f[0].contains('@')) {
					const QString code = QString::fromLatin1(f[0]);
					const QLocale l(code.section('.', 0, 0));
					if (l.language() == QLocale::C) {
						continue;
					}
					m_codes << code;
					m_names << QString("%1 (%2)").arg(l.nativeLanguageName(), l.nativeTerritoryName());
				}
			}
		}
		/* Sorted by those names. */
		QList<int> order(m_codes.size());
		std::iota(order.begin(), order.end(), 0);
		std::sort(order.begin(), order.end(), [this](int a, int b) {
			return m_names[a].compare(m_names[b], Qt::CaseInsensitive) < 0;
		});
		QStringList codes, names;
		for (int i : order) {
			codes << m_codes[i];
			names << m_names[i];
		}
		m_codes = codes;
		m_names = names;
		m_list.setItems(m_names);
		const QString current = currentLocale().section('.', 0, 0);
		for (int i = 0; i < m_codes.size(); i++) {
			if (m_codes[i].section('.', 0, 0) == current) {
				m_list.select(i, false);
				m_list.ensureVisible(i);
			}
		}
		m_list.picked = [this](int row) { choose(row); };
		m_notes = { "Programs you open after your next login use it." };
	}

protected:
	void paintExtra(pl_canvas *c) override {
		m_list.paint(c, true);
		const int row = m_list.state.selected;
		const QLocale l(row >= 0 ? m_codes[row].section('.', 0, 0) : QString("C"));
		QStringList lines;
		if (m_numbers) {
			lines << "Number:  " + l.toString(1234567.89, 'f', 2)
				<< "Money:  " + l.toCurrencyString(1234.5)
				<< "Date:  " + l.toString(QDate::currentDate(), QLocale::LongFormat)
				<< QString("Measurement:  ") + (l.measurementSystem() == QLocale::MetricSystem ? "metric" : "U.S.");
		} else {
			QStringList words = { "zebra", "Apple", "émigré", "Zoo", "apple", "Ångström" };
			QCollator collator(l);
			std::sort(words.begin(), words.end(), collator);
			lines << "Language:  " + l.nativeLanguageName() << "Sorted:  " + words.join(", ");
		}
		int y = G1 + 1 + PL_GROUP_MARGIN_TOP + 10;
		for (const QString &line : lines) {
			panelText(c, line, M + IN, y, PL_FONT_SYSTEM, C_BLACK, W - 2 * M - 2 * IN);
			y += 14;
		}
		if (!m_error.isEmpty()) {
			panelText(c, m_error, M, height() - 20, PL_FONT_VIEWS, RGB(0x99, 0, 0), W - 2 * M);
		}
	}
	bool pressExtra(QPoint pos, bool) override {
		return m_list.press(pos);
	}
	void moveExtra(QPoint pos) override {
		if (m_list.move(pos)) {
			update();
		}
	}
	void releaseExtra(QPoint) override {
		m_list.release();
	}
	void keyExtra(QKeyEvent *e) override {
		if (m_list.key(e->key(), e->text())) {
			update();
		}
	}
	void wheelEvent(QWheelEvent *e) override {
		if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
			update();
		}
	}

private:
	bool m_numbers;
	PanelList m_list;
	QStringList m_codes, m_names;
	QString m_error;

	static QString confPath() {
		return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/locale.conf";
	}

	QStringList keys() const {
		return m_numbers ? QStringList{ "LC_NUMERIC", "LC_MONETARY", "LC_TIME", "LC_MEASUREMENT", "LC_PAPER" }
			: QStringList{ "LANG", "LC_COLLATE" };
	}

	/* This panel's locale: locale.conf's, else the session's. */
	QString currentLocale() const {
		const QString key = keys().first();
		const KeyValueFile f = KeyValueFile::read(confPath());
		if (f.contains(key)) {
			return f.value(key).remove('"');
		}
		const QString env = qEnvironmentVariable(key.toLatin1().constData());
		return env.isEmpty() ? qEnvironmentVariable("LANG") : env;
	}

	void choose(int row) {
		if (row < 0 || row >= m_codes.size()) {
			return;
		}
		m_error.clear();
		const QString code = m_codes[row];
		/* Made by the system yet? (locale -a writes "fr_FR.utf8".) */
		if (!localeAvailable(code)) {
			bool ok = false;
			QString error;
			runPrivilegedHelper("accounts", { "locale", code }, QByteArray(), &ok, &error);
			if (!ok) {
				m_error = error.isEmpty() ? QString("That language couldn't be added to the system.") : error;
				update();
				return;
			}
		}
		/* Rewrite this panel's lines, keeping the other panel's. */
		KeyValueFile conf = KeyValueFile::read(confPath());
		for (const QString &key : keys()) {
			conf.set(key, code);
		}
		QDir().mkpath(QFileInfo(confPath()).path());
		if (!conf.save(confPath())) {
			m_error = "Your choice couldn't be saved.";
		}
		update();
	}

	static bool localeAvailable(const QString &code) {
		/* glibc's own check: setlocale with the name. */
		const QByteArray name = code.toLatin1();
		const char *was = setlocale(LC_CTYPE, nullptr);
		const QByteArray saved = was ? QByteArray(was) : QByteArray("C");
		const bool ok = setlocale(LC_CTYPE, name.constData()) != nullptr;
		setlocale(LC_CTYPE, saved.constData());
		return ok;
	}
};

PANEL("numbers", "Numbers", true, LocalePanel(true));
PANEL("text", "Text", true, LocalePanel(false));
