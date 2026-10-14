#include "textfile.h"

#include <QFile>
#include <QSaveFile>
#include <QStringDecoder>

static constexpr qint64 MAX_SIZE = 16 * 1024 * 1024;

bool readTextFile(const QString &path, TextFile *file, QString *error) {
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) {
		*error = f.errorString();
		return false;
	}
	if (f.size() > MAX_SIZE) {
		*error = QStringLiteral("It is too large for Scribble.");
		return false;
	}
	const QByteArray bytes = f.readAll();
	if (bytes.contains('\0')) {
		*error = QStringLiteral("It isn't a text document.");
		return false;
	}
	QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
	QString text = utf8.decode(bytes);
	if (utf8.hasError()) {
		text = QString::fromLatin1(bytes);
	}
	if (text.startsWith(QChar(0xFEFF))) {
		text.remove(0, 1);
	}
	file->ending = text.contains("\r\n") ? LineEnding::CrLf :
		text.contains('\r') ? LineEnding::Cr : LineEnding::Lf;
	text.replace("\r\n", "\n");
	text.replace('\r', '\n');
	file->text = text;
	return true;
}

bool writeTextFile(const QString &path, const TextFile &file, QString *error) {
	QString text = file.text;
	if (file.ending == LineEnding::CrLf) {
		text.replace('\n', "\r\n");
	} else if (file.ending == LineEnding::Cr) {
		text.replace('\n', '\r');
	}
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(text.toUtf8()) < 0 || !f.commit()) {
		*error = f.errorString();
		return false;
	}
	return true;
}
