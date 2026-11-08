#include "cueimage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

CueImage cueImageInfo(const QString &path) {
	CueImage image;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		image.error = QStringLiteral("The CUE sheet could not be read.");
		return image;
	}
	/* A CUE sheet is small; bound it because it comes from removable media. */
	const QString text = QString::fromUtf8(file.read(64 * 1024));
	static const QRegularExpression filePattern(
		QStringLiteral("^FILE\\s+(?:\"([^\"]+)\"|(\\S+))\\s+\\S+"),
		QRegularExpression::CaseInsensitiveOption);
	static const QRegularExpression trackPattern(
		QStringLiteral("^TRACK\\s+\\d+\\s+(\\S+)"),
		QRegularExpression::CaseInsensitiveOption);

	QString fileName;
	bool multipleFiles = false;
	for (const QString &raw : text.split(QLatin1Char('\n'))) {
		const QString line = raw.trimmed();
		if (line.isEmpty() || line.startsWith(QLatin1Char(';'))) {
			continue;
		}
		const QRegularExpressionMatch fileMatch = filePattern.match(line);
		if (fileMatch.hasMatch()) {
			if (!fileName.isEmpty()) {
				multipleFiles = true;
			} else {
				fileName = fileMatch.captured(1).isEmpty()
					? fileMatch.captured(2) : fileMatch.captured(1);
			}
			continue;
		}
		const QRegularExpressionMatch trackMatch = trackPattern.match(line);
		if (trackMatch.hasMatch()) {
			image.hasTracks = true;
			image.tracks++;
			const QString mode = trackMatch.captured(1).toUpper();
			if (mode == QLatin1String("AUDIO")) {
				image.audio = true;
			} else {
				image.data = true;
			}
		}
	}
	if (multipleFiles) {
		image.error = QStringLiteral("CUE sheets with more than one FILE are not supported.");
		return image;
	}
	if (!image.hasTracks) {
		image.error = QStringLiteral("The CUE sheet has no tracks.");
		return image;
	}
	if (fileName.isEmpty()) {
		image.error = QStringLiteral("The CUE sheet names no track file.");
		return image;
	}
	if (QFileInfo(fileName).isAbsolute() || fileName.contains(QLatin1String(".."))) {
		image.error = QStringLiteral("The CUE sheet names a track outside its own folder.");
		return image;
	}
	const QFileInfo cue(path);
	const QDir directory = cue.absoluteDir();
	const QFileInfo track(directory.absoluteFilePath(fileName));
	if (!track.isFile()) {
		image.error = QStringLiteral("The CUE sheet's track file is missing.");
		return image;
	}
	const QString folder = directory.canonicalPath();
	const QString trackPath = track.canonicalFilePath();
	if (folder.isEmpty() || trackPath.isEmpty() ||
			!trackPath.startsWith(folder + QLatin1Char('/'))) {
		image.error = QStringLiteral("The CUE sheet names a track outside its own folder.");
		return image;
	}
	image.binPath = trackPath;
	return image;
}
