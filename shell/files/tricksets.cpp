#include "tricksets.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>

#include "settings.h"

static QString setsDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/zacos9/Tricks Sets";
}

static bool validName(const QString &name) {
	return !name.trimmed().isEmpty() && !name.contains('/') && !name.startsWith('.') && name.size() <= 64;
}

QStringList trickSetNames() {
	QStringList names;
	for (const QString &f : QDir(setsDir()).entryList({ "*.set" }, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
		names << f.chopped(4);
	}
	return names;
}

bool readTrickSet(const QString &name, TrickSet *set) {
	QFile f(setsDir() + '/' + name + ".set");
	if (!validName(name) || !f.open(QIODevice::ReadOnly)) {
		return false;
	}
	*set = TrickSet();
	for (const QString &line : QString::fromUtf8(f.readAll()).split('\n')) {
		const QStringList p = line.split('\t');
		if (p.size() != 3 || (p[2] != "on" && p[2] != "off") || p[1].isEmpty()) {
			continue;
		}
		if (p[0] == "login") {
			set->loginItems.insert(p[1], p[2] == "on");
		} else if (p[0] == "service") {
			set->services.insert(p[1], p[2] == "on");
		}
	}
	return true;
}

bool saveTrickSet(const QString &name, const TrickSet &set, QString *error) {
	if (!validName(name)) {
		if (error) {
			*error = "Use a name without slashes, 64 characters at most.";
		}
		return false;
	}
	QDir().mkpath(setsDir());
	QSaveFile f(setsDir() + '/' + name.trimmed() + ".set");
	QByteArray text;
	for (auto it = set.loginItems.cbegin(); it != set.loginItems.cend(); ++it) {
		text += "login\t" + it.key().toUtf8() + '\t' + (it.value() ? "on" : "off") + '\n';
	}
	for (auto it = set.services.cbegin(); it != set.services.cend(); ++it) {
		text += "service\t" + it.key().toUtf8() + '\t' + (it.value() ? "on" : "off") + '\n';
	}
	if (!f.open(QIODevice::WriteOnly) || f.write(text) < 0 || !f.commit()) {
		if (error) {
			*error = "The set couldn't be saved.";
		}
		return false;
	}
	return true;
}

bool deleteTrickSet(const QString &name) {
	return validName(name) && QFile::remove(setsDir() + '/' + name + ".set");
}

QString currentTrickSet() {
	char v[96];
	return pl_setting("tricks-set", v, sizeof(v)) ? QString::fromUtf8(v) : QString();
}

void setCurrentTrickSet(const QString &name) {
	pl_setting_set("tricks-set", name.isEmpty() ? nullptr : name.toUtf8().constData());
}
