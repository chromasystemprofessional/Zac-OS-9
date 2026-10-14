#include "engine.h"

#include <QRegularExpression>
#include <cmath>

static constexpr int DIGITS = 12;

QString CalcEngine::format(double v) {
	if (!std::isfinite(v)) {
		return QStringLiteral("Error");
	}
	if (v == 0) {
		return QStringLiteral("0");
	}
	QString s = QString::number(v, 'g', DIGITS);
	/* Fit the display: fewer digits until it does. */
	for (int digits = DIGITS - 1; s.size() > DIGITS + 1 && digits > 1; digits--) {
		s = QString::number(v, 'g', digits);
	}
	return s;
}

QString CalcEngine::display() const {
	if (m_error) {
		return QStringLiteral("Error");
	}
	return m_entry.isEmpty() ? format(m_total) : m_entry;
}

void CalcEngine::apply() {
	const double x = m_operand;
	switch (m_op.unicode()) {
	case '+': m_total += x; break;
	case '-': m_total -= x; break;
	case '*': m_total *= x; break;
	case '/':
		if (x == 0) {
			m_error = true;
			return;
		}
		m_total /= x;
		break;
	default: m_total = x; break;
	}
	if (!std::isfinite(m_total)) {
		m_error = true;
	}
}

bool CalcEngine::press(QChar key) {
	const char k = key.toLatin1();
	if (k == 'C' || k == 'c') {
		*this = CalcEngine();
		return true;
	}
	if (m_error) {
		return k == 'E' || k == 'e' ? (*this = CalcEngine(), true) : false;
	}
	if (k == 'E' || k == 'e') {
		m_entry.clear();
		if (!m_op.isNull()) {
			m_entry = QStringLiteral("0");
		} else {
			m_total = 0;
		}
		return true;
	}
	if (k >= '0' && k <= '9') {
		if (m_entry == "0") {
			m_entry.clear();
		}
		if (m_entry.count(QRegularExpression("[0-9]")) < DIGITS) {
			m_entry += key;
		}
		return true;
	}
	if (k == '.') {
		if (m_entry.isEmpty()) {
			m_entry = QStringLiteral("0");
		}
		if (!m_entry.contains('.')) {
			m_entry += '.';
		}
		return true;
	}
	if (k == '\b') {
		m_entry.chop(1);
		return true;
	}
	if (k == '+' || k == '-' || k == '*' || k == '/') {
		if (!m_entry.isEmpty()) {
			m_operand = m_entry.toDouble();
			m_entry.clear();
			apply();
		}
		m_op = key;
		m_lastOp = key;
		return true;
	}
	if (k == '=') {
		if (!m_entry.isEmpty()) {
			m_operand = m_entry.toDouble();
			m_entry.clear();
		} else if (m_op.isNull()) {
			m_op = m_lastOp; /* = again: the last operation again */
		}
		apply();
		m_op = QChar();
		return true;
	}
	return false;
}

bool CalcEngine::enter(const QString &text) {
	bool ok = false;
	const double v = text.trimmed().toDouble(&ok);
	if (!ok) {
		return false;
	}
	m_entry = format(v);
	m_error = false;
	return true;
}
