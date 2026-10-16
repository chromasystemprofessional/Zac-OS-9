#include "stash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeData>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace {

const char INDEX[] = "Stash Index";
/* Text files bigger than this aren't taken in. */
constexpr qint64 MAX_TEXT_FILE = 1024 * 1024;

bool readClip(const QString &dir, const QString &name, Clip *clip) {
	const QString path = dir + "/" + name;
	clip->file = name;
	clip->bytes = QFileInfo(path).size();
	if (name.endsWith(".png")) {
		clip->kind = Clip::Picture;
		return clip->image.load(path, "PNG");
	}
	QFile f(path);
	if (!name.endsWith(".txt") || !f.open(QIODevice::ReadOnly)) {
		return false;
	}
	clip->kind = Clip::Text;
	clip->text = QString::fromUtf8(f.readAll());
	return true;
}

} // namespace

QString Stash::defaultDir() {
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
		"/zacos9/Stash";
}

Stash::Stash(const QString &dir) : m_dir(dir) {
	QFile index(m_dir + "/" + INDEX);
	if (!index.open(QIODevice::ReadOnly)) {
		return;
	}
	for (const QByteArray &line : index.readAll().split('\n')) {
		const QString name = QString::fromUtf8(line).trimmed();
		Clip clip;
		/* Only names the Stash makes; a damaged index skips the rest. */
		if (!name.isEmpty() && !name.contains('/') && readClip(m_dir, name, &clip)) {
			m_clips.append(clip);
		}
	}
}

bool Stash::saveIndex() const {
	QSaveFile f(m_dir + "/" + INDEX);
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	for (const Clip &clip : m_clips) {
		f.write(clip.file.toUtf8() + '\n');
	}
	return f.commit();
}

bool Stash::insert(int at, Clip clip) {
	if (!QDir().mkpath(m_dir)) {
		return false;
	}
	const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	clip.file = id + (clip.kind == Clip::Picture ? ".png" : ".txt");
	QSaveFile f(m_dir + "/" + clip.file);
	if (!f.open(QIODevice::WriteOnly)) {
		return false;
	}
	const bool written = clip.kind == Clip::Picture
		? clip.image.save(&f, "PNG")
		: f.write(clip.text.toUtf8()) >= 0;
	if (!written || !f.commit()) {
		return false;
	}
	clip.bytes = QFileInfo(m_dir + "/" + clip.file).size();
	const int page = qBound(0, at, int(m_clips.size()));
	m_clips.insert(page, clip);
	if (!saveIndex()) {
		m_clips.removeAt(page);
		QFile::remove(m_dir + "/" + clip.file);
		return false;
	}
	return true;
}

bool Stash::insertText(int at, const QString &text) {
	if (text.isEmpty()) {
		return false;
	}
	Clip clip;
	clip.kind = Clip::Text;
	clip.text = text;
	return insert(at, clip);
}

bool Stash::insertImage(int at, const QImage &image) {
	if (image.isNull()) {
		return false;
	}
	Clip clip;
	clip.kind = Clip::Picture;
	clip.image = image;
	return insert(at, clip);
}

int Stash::insertMime(int at, const QMimeData *data) {
	if (!data) {
		return 0;
	}
	int added = 0;
	/* Files dragged from the Sniffer: pictures and text documents. */
	const QMimeDatabase mimes;
	for (const QUrl &url : data->urls()) {
		const QString path = url.toLocalFile();
		const QFileInfo info(path);
		if (path.isEmpty() || !info.isFile()) {
			continue;
		}
		const QMimeType type = mimes.mimeTypeForFile(info);
		QImageReader reader(path);
		QImage image;
		if (type.name().startsWith("image/") && reader.read(&image)) {
			added += insertImage(at + added, image);
		} else if (type.inherits("text/plain") && info.size() <= MAX_TEXT_FILE) {
			QFile f(path);
			if (f.open(QIODevice::ReadOnly)) {
				added += insertText(at + added, QString::fromUtf8(f.readAll()));
			}
		}
	}
	if (added || data->hasUrls()) {
		return added;
	}
	if (data->hasImage()) {
		return insertImage(at, qvariant_cast<QImage>(data->imageData()));
	}
	return data->hasText() && insertText(at, data->text());
}

bool Stash::remove(int index) {
	if (index < 0 || index >= m_clips.size()) {
		return false;
	}
	const Clip clip = m_clips.takeAt(index);
	if (!saveIndex()) {
		m_clips.insert(index, clip);
		return false;
	}
	QFile::remove(m_dir + "/" + clip.file);
	return true;
}

QMimeData *Stash::mimeData(int index) const {
	auto *data = new QMimeData;
	if (index < 0 || index >= m_clips.size()) {
		return data;
	}
	const Clip &clip = m_clips.at(index);
	if (clip.kind == Clip::Picture) {
		data->setImageData(clip.image);
	} else {
		data->setText(clip.text);
	}
	return data;
}
