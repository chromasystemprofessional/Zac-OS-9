#include "folderstate.h"

#include <QCryptographicHash>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

static QSettings &store() {
	static QSettings settings(
		QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
			"/platinum/finder/folders.ini",
		QSettings::IniFormat);
	return settings;
}

/* Paths contain '/', which QSettings treats as nesting: key by hash. */
static QString group(const QString &path) {
	return "f" + QCryptographicHash::hash(QDir(path).absolutePath().toUtf8(),
		QCryptographicHash::Sha1).toHex().left(16);
}

FolderState FolderState::load(const QString &path) {
	FolderState s;
	QSettings &st = store();
	st.beginGroup(group(path));
	s.known = st.contains("path");
	s.hasPosition = st.contains("x");
	s.position = QPoint(st.value("x").toInt(), st.value("y").toInt());
	s.size = QSize(st.value("w", 420).toInt(), st.value("h", 280).toInt());
	s.viewMode = st.value("view", 0).toInt();
	s.sortColumn = st.value("sort", 0).toInt();
	st.endGroup();
	return s;
}

void FolderState::save(const QString &path) const {
	QSettings &st = store();
	st.beginGroup(group(path));
	st.setValue("path", QDir(path).absolutePath());
	if (hasPosition) {
		st.setValue("x", position.x());
		st.setValue("y", position.y());
	}
	st.setValue("w", size.width());
	st.setValue("h", size.height());
	st.setValue("view", viewMode);
	st.setValue("sort", sortColumn);
	st.endGroup();
	st.sync();
}
