#include "datetime.h"

#include <QFile>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QProcess>
#include <QTimeZone>
#include <QWheelEvent>

#include "alert.h"
#include "platinumshell.h"
#include "settings.h"

static constexpr uint32_t FACE = GRAY(0xD);
/* Text in the clock control: first ink 11 px in, baseline 13 rows down;
 * the selected part's highlight runs 3 px before its ink to 1 px after,
 * rows 2..17 (HIG figure 2-22). */
static constexpr int CLOCK_TEXT_X = 11, CLOCK_BASELINE = 13;

static bool setting(const char *key) {
	char v[16];
	return pl_setting(key, v, sizeof(v)) && (strcmp(v, "1") == 0 || strcmp(v, "true") == 0);
}

/* ---- the clock control --------------------------------------------------------- */

std::vector<ClockField::Part> ClockField::parts() const {
	if (kind == Kind::Date) {
		const QDate d = value.date();
		return { { QString::number(d.month()), true }, { "/", false },
			{ QString::number(d.day()), true }, { "/", false },
			{ QString::number(d.year()), true } };
	}
	const QTime t = value.time();
	const auto two = [](int v) { return QString("%1").arg(v, 2, 10, QChar('0')); };
	if (use24h) {
		return { { two(t.hour()), true }, { ":", false }, { two(t.minute()), true },
			{ ":", false }, { two(t.second()), true } };
	}
	const int h = t.hour() % 12 ? t.hour() % 12 : 12;
	return { { QString::number(h), true }, { ":", false }, { two(t.minute()), true },
		{ ":", false }, { two(t.second()), true }, { " ", false },
		{ t.hour() < 12 ? "AM" : "PM", true } };
}

QRect ClockField::rect() const {
	return QRect(pos, QSize(fieldW + 2 + PL_ARROWS_W, PL_CLOCK_H));
}

void ClockField::paint(pl_canvas *c, uint32_t bg) const {
	pl_clock_paint(c, pos.x(), pos.y(), fieldW, focused && enabled, pressed, enabled,
		pl_accent_current(), bg);
	int pen = pos.x() + CLOCK_TEXT_X - 1;
	const auto ps = parts();
	for (int i = 0; i < static_cast<int>(ps.size()); i++) {
		Text t(ps[i].text, 1000, PL_FONT_SYSTEM);
		if (t.t && t.t->ink_l >= 0) {
			const int ink = pen + t.t->ink_l - 1;
			if (i == selected && focused && enabled) {
				pl_fill(c, ink - 3, pos.y() + 2, ink + t.inkWidth(), pos.y() + 17,
					pl_highlight_current());
			}
			pl_text(c, t.t, ink, pos.y() + CLOCK_BASELINE, enabled ? C_BLACK : GRAY(0x8));
		}
		pen += t.t ? t.t->advance : 0;
	}
}

int ClockField::partAt(QPoint p) const {
	int pen = pos.x() + CLOCK_TEXT_X - 1;
	const auto ps = parts();
	int best = -1;
	for (int i = 0; i < static_cast<int>(ps.size()); i++) {
		Text t(ps[i].text, 1000, PL_FONT_SYSTEM);
		const int adv = t.t ? t.t->advance : 0;
		if (ps[i].editable) {
			best = best < 0 ? i : best;
			if (p.x() < pen + adv + 2) {
				return i;
			}
			best = i;
		}
		pen += adv;
	}
	return best;
}

bool ClockField::press(QPoint p) {
	if (!enabled || !rect().contains(p)) {
		return false;
	}
	focused = true;
	m_digits.clear();
	const auto part = pl_little_arrows_hit(PL_CLOCK_ARROWS_X(pos.x(), fieldW), pos.y(), p.x(), p.y());
	if (part != PL_ARROWS_NONE) {
		pressed = part;
		if (selected < 0) {
			selected = 0;
		}
		step(part == PL_ARROWS_UP ? 1 : -1);
		return true;
	}
	if (p.x() < pos.x() + fieldW) {
		selected = partAt(p);
	}
	return true;
}

void ClockField::release() {
	pressed = PL_ARROWS_NONE;
}

void ClockField::step(int delta) {
	if (selected < 0) {
		return;
	}
	QDateTime v = value;
	if (kind == Kind::Date) {
		const QDate d = v.date();
		switch (selected) {
		case 0: v.setDate(d.addMonths(delta)); break;
		case 2: v.setDate(d.addDays(delta)); break;
		default: v.setDate(d.addYears(delta)); break;
		}
	} else {
		switch (selected) {
		case 0: v = v.addSecs(3600 * delta); break;
		case 2: v = v.addSecs(60 * delta); break;
		case 4: v = v.addSecs(delta); break;
		default: v = v.addSecs(12 * 3600 * (v.time().hour() < 12 ? 1 : -1)); break;
		}
		v.setDate(value.date()); /* the time field never moves the date */
	}
	value = v;
	editing = true;
	if (changed) {
		changed(value);
	}
}

bool ClockField::key(int key, const QString &text) {
	if (!enabled) {
		return false;
	}
	const int last = static_cast<int>(parts().size()) - 1;
	if (key == Qt::Key_Up || key == Qt::Key_Down) {
		if (selected < 0) {
			selected = 0;
		}
		step(key == Qt::Key_Up ? 1 : -1);
		return true;
	}
	if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Tab) {
		/* Move between the editable parts. */
		int i = selected < 0 ? 0 : selected;
		const int dir = key == Qt::Key_Left ? -1 : 1;
		do {
			i += dir;
		} while (i >= 0 && i <= last && !parts()[i].editable);
		if (i >= 0 && i <= last) {
			selected = i;
			m_digits.clear();
			return true;
		}
		return key != Qt::Key_Tab;
	}
	if (selected >= 0 && text.size() == 1 && text[0].isDigit()) {
		/* Typed digits replace the selected part. */
		m_digits += text;
		const int n = m_digits.toInt();
		QDateTime v = value;
		if (kind == Kind::Date) {
			const QDate d = v.date();
			if (selected == 0 && n >= 1 && n <= 12) {
				v.setDate(QDate(d.year(), n, std::min(d.day(), QDate(d.year(), n, 1).daysInMonth())));
			} else if (selected == 2 && n >= 1 && n <= d.daysInMonth()) {
				v.setDate(QDate(d.year(), d.month(), n));
			} else if (selected == 4 && m_digits.size() == 4) {
				v.setDate(QDate(n, d.month(), std::min(d.day(), QDate(n, d.month(), 1).daysInMonth())));
			}
		} else {
			const QTime t = v.time();
			if (selected == 0 && n <= (use24h ? 23 : 12)) {
				int h = n;
				if (!use24h) {
					h = (n % 12) + (t.hour() >= 12 ? 12 : 0);
				}
				v.setTime(QTime(h, t.minute(), t.second()));
			} else if (selected == 2 && n <= 59) {
				v.setTime(QTime(t.hour(), n, t.second()));
			} else if (selected == 4 && n <= 59) {
				v.setTime(QTime(t.hour(), t.minute(), n));
			}
		}
		if (m_digits.size() >= (selected == 4 && kind == Kind::Date ? 4 : 2)) {
			m_digits.clear();
		}
		value = v;
		editing = true;
		if (changed) {
			changed(value);
		}
		return true;
	}
	if (selected >= 0 && kind == Kind::Time && !use24h && (text == "a" || text == "p" ||
			text == "A" || text == "P")) {
		const bool pm = text.toLower() == "p";
		if (pm != (value.time().hour() >= 12)) {
			value = value.addSecs(pm ? 12 * 3600 : -12 * 3600);
			editing = true;
			if (changed) {
				changed(value);
			}
		}
		return true;
	}
	return false;
}

/* ---- the time zone dialog -------------------------------------------------------- */

static constexpr int TZ_W = 300, TZ_H = 270;

TimeZoneDialog::TimeZoneDialog(const QStringList &zones, const QString &current) {
	setWindowTitle("Set Time Zone");
	setFixedSize(TZ_W, TZ_H);
	m_list.frame = QRect(QPoint(13, 33), QPoint(TZ_W - 14, 33 + 11 * PL_LIST_ROW_H + 1));
	QStringList names;
	for (const QString &z : zones) {
		names << QString(z).replace('_', ' ');
	}
	m_list.setItems(names);
	m_list.select(std::max<int>(0, zones.indexOf(current)), false);
	m_list.state.top = std::max(0, m_list.state.selected - 5);
	m_list.scrollTo(m_list.state.top);
	m_ok = PanelButton("OK", QRect(TZ_W - 13 - 58 - 3, TZ_H - 13 - 20 - 3, 58, 20), true);
	m_ok.clicked = [this] { accept(); };
	m_cancel = PanelButton("Cancel", QRect(m_ok.rect.x() - 12 - 3 - 58, m_ok.rect.y(), 58, 20));
	m_cancel.clicked = [this] { reject(); };
}

QString TimeZoneDialog::chosen() const {
	return m_list.items.value(m_list.state.selected).replace(' ', '_');
}

void TimeZoneDialog::showEvent(QShowEvent *e) {
	QDialog::showEvent(e);
	platinumSetFrameStyle(this, FrameStyle::MovableModal);
}

void TimeZoneDialog::paintEvent(QPaintEvent *) {
	Pixels px(TZ_W, TZ_H);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, TZ_W - 1, TZ_H - 1, FACE);
	Text prompt("Choose the city or region you're in:", TZ_W - 26, PL_FONT_SYSTEM);
	pl_text(c, prompt.t, 13, 22, C_BLACK);
	m_list.paint(c, true);
	m_cancel.paint(c);
	m_ok.paint(c);
	QPainter p(this);
	px.blit(p);
}

void TimeZoneDialog::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_ok.press(pos) || m_cancel.press(pos) || m_list.press(pos)) {
		update();
	}
}

void TimeZoneDialog::mouseMoveEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_ok.move(pos) || m_cancel.move(pos) || m_list.move(pos)) {
		update();
	}
}

void TimeZoneDialog::mouseReleaseEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_list.release() | m_ok.release(pos) || m_cancel.release(pos)) {
		update();
	}
}

void TimeZoneDialog::wheelEvent(QWheelEvent *e) {
	if (m_list.wheel(e->position().toPoint(), e->angleDelta().y())) {
		update();
	}
}

void TimeZoneDialog::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		accept();
	} else if (e->key() == Qt::Key_Escape ||
			((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_Period)) {
		reject();
	} else if (m_list.key(e->key(), e->text())) {
		update();
	}
}

/* ---- the panel ------------------------------------------------------------------- */

static constexpr int W = 420;
/* Tall enough for the menu bar clock options, plus a note when the
 * system time can't be changed from here. */
static constexpr int H = 262, H_NOTE = 280;
static constexpr int M = 12;              /* window margin */
static constexpr int COL = 206;           /* the right column's group boxes */
static constexpr int ROW1 = 26, ROW2 = 98; /* group boxes' top lines */
static constexpr int ROW3 = 170;

DateTimePanel::DateTimePanel() {
	setWindowTitle("Date & Time");

	m_date.kind = ClockField::Kind::Date;
	m_date.pos = QPoint(M + 11, ROW1 + 13);
	m_date.fieldW = 104;
	m_time.kind = ClockField::Kind::Time;
	m_time.pos = QPoint(M + 11, ROW2 + 13);
	m_time.fieldW = 104;
	m_time.use24h = setting("clock-24h");
	for (ClockField *f : { &m_date, &m_time }) {
		f->changed = [this](const QDateTime &t) {
			m_pending = t;
			m_offset = QDateTime::currentDateTime().secsTo(t);
			m_applyTimer.start(1200);
			update();
		};
	}

	m_zoneButton = PanelButton("Set Time Zone…", QRect(COL + 11, ROW1 + 36, 130, 20));
	m_zoneButton.clicked = [this] { chooseTimeZone(); };

	m_ntp = PanelCheckbox("Set automatically", QPoint(COL + 11, ROW2 + 13 + 5));
	m_ntp.toggled = [this](bool on) {
		run({ "set-ntp", on ? "true" : "false" });
		refresh();
	};

	m_showClock = PanelCheckbox("Show the clock in the menu bar", QPoint(M + 11, ROW3 + 14));
	m_clock24 = PanelCheckbox("Use 24-hour time", QPoint(M + 11, ROW3 + 34));
	m_weekday = PanelCheckbox("Show the day of the week", QPoint(M + 11, ROW3 + 54));
	m_showClock.toggled = [this](bool on) {
		pl_setting_set("clock", on ? nullptr : "off");
		m_clock24.enabled = m_weekday.enabled = on;
		update();
	};
	m_clock24.toggled = [this](bool on) {
		pl_setting_set("clock-24h", on ? "1" : nullptr);
		m_time.use24h = on;
		update();
	};
	m_weekday.toggled = [this](bool on) {
		pl_setting_set("clock-weekday", on ? "1" : nullptr);
	};
	char v[16];
	m_showClock.on = !(pl_setting("clock", v, sizeof(v)) && strcmp(v, "off") == 0);
	m_clock24.on = setting("clock-24h");
	m_weekday.on = setting("clock-weekday");
	m_clock24.enabled = m_weekday.enabled = m_showClock.on;

	m_tick.callOnTimeout([this] {
		const QDateTime now = QDateTime::currentDateTime();
		for (ClockField *f : { &m_date, &m_time }) {
			f->value = f->editing ? now.addSecs(m_offset) : now;
		}
		update();
	});
	m_tick.start(1000);
	m_applyTimer.setSingleShot(true);
	m_applyTimer.callOnTimeout([this] { applyDateTime(m_pending); });
	refresh();
	setFixedSize(W, m_available ? H : H_NOTE);
	m_date.value = m_time.value = QDateTime::currentDateTime();
}

/* Runs `timedatectl ARGS`; on failure, explains in an alert (unless quiet). */
bool DateTimePanel::run(const QStringList &args, QString *out, bool quiet) {
	/* ZACOS9_DATETIME_DRYRUN=1 (the UI tests) logs changes instead of
	 * making them. */
	if (qEnvironmentVariableIsSet("ZACOS9_DATETIME_DRYRUN") && args.value(0).startsWith("set-")) {
		fprintf(stderr, "zacos9-datetime: would run: timedatectl %s\n",
			args.join(' ').toUtf8().constData());
		return true;
	}
	QProcess p;
	p.start("timedatectl", args);
	if (!p.waitForFinished(10000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
		if (!quiet) {
			QString why = QString::fromUtf8(p.readAllStandardError()).trimmed();
			Alert::ask(why.isEmpty() ? "The date and time couldn't be changed." : why, "OK",
				QString());
		}
		return false;
	}
	if (out) {
		*out = QString::fromUtf8(p.readAllStandardOutput());
	}
	return true;
}

void DateTimePanel::refresh() {
	QString out;
	m_available = run({ "show", "--property=Timezone", "--property=NTP", "--property=CanNTP" },
		&out, true);
	bool ntp = false, canNtp = false;
	for (const QString &line : out.split('\n')) {
		if (line.startsWith("Timezone=")) {
			m_timezone = line.mid(9);
		} else if (line.startsWith("NTP=")) {
			ntp = line.mid(4) == "yes";
		} else if (line.startsWith("CanNTP=")) {
			canNtp = line.mid(7) == "yes";
		}
	}
	if (!m_available) {
		m_timezone = QString::fromUtf8(QTimeZone::systemTimeZoneId());
	}
	m_ntp.on = ntp;
	m_ntp.enabled = m_available && canNtp;
	m_date.enabled = m_time.enabled = m_available && !ntp;
	m_zoneButton.enabled = m_available;
	update();
}

void DateTimePanel::applyDateTime(const QDateTime &t) {
	const bool ok = run({ "set-time", t.toString("yyyy-MM-dd HH:mm:ss") });
	m_date.editing = m_time.editing = false;
	m_offset = 0;
	if (!ok) {
		refresh();
	}
	update();
}

void DateTimePanel::chooseTimeZone() {
	QString out;
	QStringList zones;
	if (run({ "list-timezones" }, &out, true)) {
		zones = out.split('\n', Qt::SkipEmptyParts);
	}
	if (zones.isEmpty()) {
		for (const QByteArray &id : QTimeZone::availableTimeZoneIds()) {
			zones << QString::fromUtf8(id);
		}
	}
	TimeZoneDialog dialog(zones, m_timezone);
	if (dialog.exec() == QDialog::Accepted && dialog.chosen() != m_timezone) {
		run({ "set-timezone", dialog.chosen() });
		refresh();
	}
}

void DateTimePanel::paintEvent(QPaintEvent *) {
	const int h = height();
	Pixels px(W, h);
	pl_canvas *c = &px.c;
	pl_fill(c, 0, 0, W - 1, h - 1, FACE);

	const int box_r = COL - 12, clock_bottom = PL_CLOCK_H + 1 + PL_GROUP_MARGIN_TOP + PL_GROUP_MARGIN;
	panelGroup(c, M, ROW1, box_r, ROW1 + clock_bottom, "Current Date", FACE);
	panelGroup(c, M, ROW2, box_r, ROW2 + clock_bottom, "Current Time", FACE);
	m_date.paint(c, FACE);
	m_time.paint(c, FACE);

	panelGroup(c, COL, ROW1, W - M - 2, ROW1 + clock_bottom + 14, "Time Zone", FACE);
	Text zone(QString(m_timezone).replace('_', ' '), W - M - COL - 26, PL_FONT_SYSTEM);
	pl_text(c, zone.t, COL + 11, ROW1 + 26, C_BLACK);
	m_zoneButton.paint(c);

	panelGroup(c, COL, ROW2 + 14, W - M - 2, ROW2 + clock_bottom, "Network Time", FACE);
	m_ntp.pos = QPoint(COL + 11, ROW2 + 14 + 13);
	m_ntp.paint(c);

	panelGroup(c, M, ROW3, W - M - 2, ROW3 + 78, "Menu Bar Clock", FACE);
	m_showClock.paint(c);
	m_clock24.paint(c);
	m_weekday.paint(c);

	if (!m_available) {
		Text note("The date, time and time zone are set by the system here.", W - 2 * M,
			PL_FONT_VIEWS);
		pl_text(c, note.t, M, h - 12, GRAY(0x5));
	}
	QPainter p(this);
	px.blit(p);
}

void DateTimePanel::mousePressEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	for (ClockField *f : { &m_date, &m_time }) {
		if (f->press(pos)) {
			for (ClockField *g : { &m_date, &m_time }) {
				g->focused = g == f;
			}
			m_focus = f;
			update();
			return;
		}
	}
	if (m_zoneButton.press(pos) || m_ntp.press(pos) || m_showClock.press(pos) ||
			m_clock24.press(pos) || m_weekday.press(pos)) {
		update();
	}
}

void DateTimePanel::mouseMoveEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	if (m_zoneButton.move(pos) || m_ntp.move(pos) || m_showClock.move(pos) ||
			m_clock24.move(pos) || m_weekday.move(pos)) {
		update();
	}
}

void DateTimePanel::mouseReleaseEvent(QMouseEvent *e) {
	const QPoint pos = e->position().toPoint();
	m_date.release();
	m_time.release();
	m_zoneButton.release(pos);
	m_ntp.release(pos);
	m_showClock.release(pos);
	m_clock24.release(pos);
	m_weekday.release(pos);
	update();
}

void DateTimePanel::keyPressEvent(QKeyEvent *e) {
	if ((e->modifiers() & Qt::ControlModifier) && e->key() == Qt::Key_W) {
		close();
		return;
	}
	if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
		if (m_applyTimer.isActive()) {
			m_applyTimer.stop();
			applyDateTime(m_pending);
		}
		return;
	}
	if (m_focus && m_focus->key(e->key(), e->text())) {
		update();
	}
}
