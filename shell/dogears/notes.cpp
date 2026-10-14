#include "notes.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

static const struct {
	const char *name;
	unsigned rgb;
} COLORS[NOTE_COLORS] = {
	{ "Yellow", 0xFFFFA0 }, { "Blue", 0xC8E6FF }, { "Green", 0xC8FFC8 },
	{ "Pink", 0xFFD2E6 }, { "Purple", 0xE1D2FF }, { "Gray", 0xE6E6E6 },
};

const char *noteColorName(NoteColor c) {
	return COLORS[static_cast<int>(c)].name;
}

unsigned noteColorRgb(NoteColor c) {
	return COLORS[static_cast<int>(c)].rgb;
}

QString notesPath() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/zacos9/dog-ears.json";
}

QList<Note> loadNotes(const QString &path) {
	QFile f(path);
	QList<Note> notes;
	if (!f.open(QIODevice::ReadOnly)) {
		return notes;
	}
	for (const QJsonValue &v : QJsonDocument::fromJson(f.readAll()).object().value("notes").toArray()) {
		const QJsonObject o = v.toObject();
		Note n;
		n.text = o.value("text").toString();
		int color = 0;
		for (int i = 0; i < NOTE_COLORS; i++) {
			if (o.value("color").toString() == QLatin1String(COLORS[i].name)) {
				color = i;
			}
		}
		n.color = static_cast<NoteColor>(color);
		n.size = QSize(std::max(80, o.value("w").toInt(220)), std::max(60, o.value("h").toInt(160)));
		n.hasPosition = o.contains("x") && o.contains("y");
		n.position = QPoint(o.value("x").toInt(), o.value("y").toInt());
		notes << n;
	}
	return notes;
}

bool saveNotes(const QString &path, const QList<Note> &notes) {
	QJsonArray array;
	for (const Note &n : notes) {
		QJsonObject o{ { "text", n.text }, { "color", noteColorName(n.color) },
			{ "w", n.size.width() }, { "h", n.size.height() } };
		if (n.hasPosition) {
			o.insert("x", n.position.x());
			o.insert("y", n.position.y());
		}
		array << o;
	}
	QDir().mkpath(QFileInfo(path).absolutePath());
	QSaveFile f(path);
	return f.open(QIODevice::WriteOnly) &&
		f.write(QJsonDocument(QJsonObject{ { "notes", array } }).toJson()) >= 0 && f.commit();
}

QString noteTitle(const QString &text) {
	QString line = text.section('\n', 0, 0).trimmed();
	if (line.isEmpty()) {
		return QStringLiteral("Dog Ears");
	}
	return line.size() > 28 ? line.left(27) + QChar(0x2026) : line;
}
