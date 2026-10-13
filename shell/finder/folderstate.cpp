#include "folderstate.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include "vfs.h"

/* Virtual paths are already absolute and name a node by its stable id;
 * QDir would read them as relative to the working directory. */
static QString stateKeyPath(const QString &path) {
	return vfsIsVirtual(path) ? path : QDir(path).absolutePath();
}

static QSettings &store() {
	static QSettings settings(
		QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
			"/zacos9/finder/folders.ini",
		QSettings::IniFormat);
	return settings;
}

/* Paths contain '/', which QSettings treats as nesting: key by hash. */
static QString group(const QString &path) {
	return "f" + QCryptographicHash::hash(stateKeyPath(path).toUtf8(),
		QCryptographicHash::Sha1).toHex().left(16);
}

/* The folder enclosing `path`, if it is a real one. */
static QString parentOf(const QString &path) {
	if (vfsIsVirtual(path)) {
		return {};
	}
	const QString parent = QFileInfo(QDir(path).absolutePath()).absolutePath();
	return parent == QDir(path).absolutePath() ? QString() : parent;
}

FolderState FolderState::load(const QString &path) {
	FolderState s;
	QSettings &st = store();
	/* What a new window starts from: its parent window's view, or the
	 * standard views. */
	ViewOptions inherited = standardViews();
	int inheritedView = 0;
	const QString parent = parentOf(path);
	if (!parent.isEmpty()) {
		st.beginGroup(group(parent));
		if (st.contains("path")) {
			inherited = ViewOptions::load(st, inherited);
			inheritedView = st.value("view", 0).toInt();
		}
		st.endGroup();
	}
	st.beginGroup(group(path));
	s.known = st.contains("path");
	s.hasPosition = st.contains("x");
	s.position = QPoint(st.value("x").toInt(), st.value("y").toInt());
	s.size = QSize(st.value("w", 420).toInt(), st.value("h", 280).toInt());
	s.viewMode = st.value("view", inheritedView).toInt();
	s.sortColumn = std::clamp(st.value("sort", 0).toInt(), 0, COL_COUNT - 1);
	s.options = ViewOptions::load(st, inherited);
	st.beginGroup("icons");
	for (const QString &key : st.childKeys()) {
		const QStringList xy = st.value(key).toString().split(',');
		if (xy.size() == 2) {
			s.icons.insert(QUrl::fromPercentEncoding(key.toUtf8()),
				QPoint(xy[0].toInt(), xy[1].toInt()));
		}
	}
	st.endGroup();
	st.endGroup();
	return s;
}

void FolderState::save(const QString &path) const {
	QSettings &st = store();
	st.beginGroup(group(path));
	st.setValue("path", stateKeyPath(path));
	if (hasPosition) {
		st.setValue("x", position.x());
		st.setValue("y", position.y());
	}
	st.setValue("w", size.width());
	st.setValue("h", size.height());
	st.setValue("view", viewMode);
	st.setValue("sort", sortColumn);
	options.save(st);
	/* Names may hold '/', '=' and the like: store them percent-encoded. */
	st.remove("icons");
	st.beginGroup("icons");
	for (auto it = icons.cbegin(); it != icons.cend(); ++it) {
		st.setValue(QString::fromLatin1(QUrl::toPercentEncoding(it.key())),
			QStringLiteral("%1,%2").arg(it.value().x()).arg(it.value().y()));
	}
	st.endGroup();
	st.endGroup();
	st.sync();
}
