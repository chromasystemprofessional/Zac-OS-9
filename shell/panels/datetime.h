#pragma once

#include <QDateTime>
#include <QDialog>
#include <QTimer>
#include <QWidget>

#include "panelkit.h"

/*
 * A clock control (HIG figure 2-22) editing a date or a time: click a
 * part (month, hour...) to select it, then use the little arrows, the
 * arrow keys or digits. Other parts keep ticking while nothing is being
 * edited.
 */
struct ClockField {
	enum class Kind { Date, Time };
	Kind kind = Kind::Date;
	QPoint pos;          /* top-left of the field */
	int fieldW = 100;
	QDateTime value;
	int selected = -1;   /* index into parts(), or -1 */
	bool focused = false;
	bool editing = false; /* the user changed it; don't tick */
	bool enabled = true;
	bool use24h = false;
	pl_arrows_part pressed = PL_ARROWS_NONE;
	std::function<void(const QDateTime &)> changed;

	struct Part {
		QString text;
		bool editable;
	};
	std::vector<Part> parts() const;
	QRect rect() const; /* field and arrows */
	void paint(pl_canvas *c, uint32_t bg) const;
	bool press(QPoint p);
	void release();
	bool key(int key, const QString &text);
	void step(int delta);

private:
	int partAt(QPoint p) const;
	QString m_digits;
};

/* The time zone picker: a list of the system's zones, OK and Cancel. */
class TimeZoneDialog : public QDialog {
public:
	TimeZoneDialog(const QStringList &zones, const QString &current);
	QString chosen() const;

protected:
	void paintEvent(QPaintEvent *) override;
	void showEvent(QShowEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void wheelEvent(QWheelEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	PanelList m_list;
	PanelButton m_ok, m_cancel;
};

/*
 * The Date & Time control panel: the date and time (set through
 * systemd's timedated, `timedatectl`), the time zone, network time, and
 * the menu bar clock's options (desktop.conf: clock, clock-24h,
 * clock-weekday). TODO: the layout is ours; the HIG shows its controls
 * but not this panel.
 */
class DateTimePanel : public QWidget {
public:
	DateTimePanel();

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *) override;
	void mouseMoveEvent(QMouseEvent *) override;
	void mouseReleaseEvent(QMouseEvent *) override;
	void keyPressEvent(QKeyEvent *) override;

private:
	void refresh();
	void applyDateTime(const QDateTime &t);
	bool run(const QStringList &args, QString *out = nullptr, bool quiet = false);
	void chooseTimeZone();

	bool m_available = false; /* timedatectl works here */
	QString m_timezone;
	ClockField m_date, m_time;
	ClockField *m_focus = nullptr;
	PanelButton m_zoneButton;
	PanelCheckbox m_ntp, m_showClock, m_clock24, m_weekday;
	QTimer m_tick, m_applyTimer;
	QDateTime m_pending;
	qint64 m_offset = 0; /* edited time minus real time, while editing */
};
