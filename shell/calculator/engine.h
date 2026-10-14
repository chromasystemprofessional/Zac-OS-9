#pragma once

#include <QString>

/*
 * The Calculator desk accessory's arithmetic, as Mac OS's: each operation
 * acts on the running total as you go (2 + 3 * 4 = 20), = repeats the last
 * operation, C clears everything and E (Clear Entry) only the number being
 * typed. Twelve digits are shown; anything wider is in exponent form.
 */
class CalcEngine {
public:
	/* One key: 0-9 . + - * / = C E, or '\b' to take back a digit.
	 * False for a key it doesn't know. */
	bool press(QChar key);
	/* What the display shows. */
	QString display() const;
	/* Typed or pasted: enters the number as if keyed in. False if `text`
	 * isn't one. */
	bool enter(const QString &text);

private:
	void apply();
	static QString format(double v);

	double m_total = 0;
	double m_operand = 0;   /* for = again */
	QChar m_op;              /* pending: + - * / or none */
	QChar m_lastOp;
	QString m_entry;         /* digits being typed; empty: showing m_total */
	bool m_error = false;
};
