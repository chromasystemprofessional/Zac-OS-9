#include "conf.h"

#include <QFile>
#include <QSaveFile>

static QString keyOf(const QString &line) {
	const int eq = line.indexOf('=');
	return eq < 0 ? QString() : line.left(eq).trimmed();
}

KeyValueFile KeyValueFile::read(const QString &path) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		return KeyValueFile();
	}
	KeyValueFile file = parse(f.readAll());
	file.m_exists = true;
	return file;
}

KeyValueFile KeyValueFile::parse(const QByteArray &text) {
	KeyValueFile file;
	file.m_exists = true;
	for (const QString &line : QString::fromUtf8(text).split('\n')) {
		if (!line.trimmed().isEmpty()) {
			file.m_lines << line;
		}
	}
	return file;
}

int KeyValueFile::find(const QString &key) const {
	for (int i = 0; i < m_lines.size(); i++) {
		if (keyOf(m_lines[i]) == key) {
			return i;
		}
	}
	return -1;
}

bool KeyValueFile::contains(const QString &key) const {
	return find(key) >= 0;
}

QString KeyValueFile::value(const QString &key, const QString &fallback) const {
	const int i = find(key);
	return i < 0 ? fallback : m_lines[i].mid(m_lines[i].indexOf('=') + 1).trimmed();
}

void KeyValueFile::set(const QString &key, const QString &value) {
	const int i = find(key);
	if (i < 0) {
		m_lines << key + '=' + value;
		return;
	}
	m_lines[i] = key + '=' + value;
	for (int j = m_lines.size() - 1; j > i; j--) {
		if (keyOf(m_lines[j]) == key) {
			m_lines.removeAt(j);
		}
	}
}

void KeyValueFile::remove(const QString &key) {
	for (int j = m_lines.size() - 1; j >= 0; j--) {
		if (keyOf(m_lines[j]) == key) {
			m_lines.removeAt(j);
		}
	}
}

QByteArray KeyValueFile::text() const {
	QByteArray out;
	for (const QString &line : m_lines) {
		out += line.toUtf8() + '\n';
	}
	return out;
}

bool KeyValueFile::save(const QString &path, QString *error) const {
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(text()) < 0 || !f.commit()) {
		if (error) {
			*error = f.errorString();
		}
		return false;
	}
	return true;
}
