#include "clippings.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

static const QRegularExpression ClippingName(
	"^.* clipping( \\d+)?\\.(txt|png)$", QRegularExpression::CaseInsensitiveOption);

ClippingKind clippingKind(const QString &path) {
	const QString name = QFileInfo(path).fileName();
	if (name.endsWith(".url", Qt::CaseInsensitive)) {
		return ClippingKind::Location;
	}
	const QRegularExpressionMatch m = ClippingName.match(name);
	if (!m.hasMatch()) {
		return ClippingKind::None;
	}
	return m.captured(2).toLower() == "png" ? ClippingKind::Picture : ClippingKind::Text;
}

QString internetLocationUrl(const QString &path) {
	QSettings file(path, QSettings::IniFormat);
	return file.value("InternetShortcut/URL").toString();
}

/* Links, not files: the browser's kind of drag. */
static QList<QUrl> remoteUrls(const QMimeData *mime) {
	QList<QUrl> out;
	for (const QUrl &url : mime->urls()) {
		if (url.isLocalFile() || url.scheme().isEmpty() || url.scheme() == "file") {
			return {};
		}
		out << url;
	}
	return out;
}

bool canMakeClipping(const QMimeData *mime) {
	if (mime->hasUrls()) {
		return !remoteUrls(mime).isEmpty();
	}
	return mime->hasImage() || !mime->text().trimmed().isEmpty();
}

QString clippingBaseName(const QString &text) {
	// The first line's first words, as many as fit in about 24 characters.
	const QString line = text.trimmed().section('\n', 0, 0).simplified().remove(QRegularExpression("[/\\\\:]"));
	QString name;
	for (const QString &word : line.split(' ', Qt::SkipEmptyParts)) {
		if (!name.isEmpty() && name.size() + 1 + word.size() > 24) {
			break;
		}
		name += (name.isEmpty() ? "" : " ") + word.left(24);
	}
	return name.isEmpty() ? QString("text") : name;
}

/* "base.ext", else "base 2.ext", ... */
static QString freePath(const QString &folder, const QString &base, const QString &ext) {
	const QDir dir(folder);
	QString name = base + ext;
	for (int i = 2; dir.exists(name); i++) {
		name = QString("%1 %2%3").arg(base).arg(i).arg(ext);
	}
	return dir.filePath(name);
}

static bool save(const QString &path, const QByteArray &data, QString *error) {
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
		*error = f.errorString();
		return false;
	}
	return true;
}

/* A browser's link title: text/x-moz-url is "address\ntitle" in UTF-16. */
static QString linkTitle(const QMimeData *mime, const QUrl &url) {
	const QByteArray moz = mime->data("text/x-moz-url");
	if (!moz.isEmpty()) {
		const QString s = QString::fromUtf16(reinterpret_cast<const char16_t *>(moz.constData()),
			moz.size() / 2);
		const QStringList lines = s.split('\n');
		for (int i = 0; i + 1 < lines.size(); i += 2) {
			if (QUrl(lines[i].trimmed()) == url && !lines[i + 1].trimmed().isEmpty()) {
				return lines[i + 1].trimmed();
			}
		}
	}
	const QString text = mime->text().trimmed();
	if (!text.isEmpty() && QUrl(text) != url && !text.contains('\n') && text.size() < 80) {
		return text;
	}
	return (url.host() + url.path()).remove(QRegularExpression("/+$"));
}

QStringList makeClippings(const QMimeData *mime, const QString &folder, QString *error) {
	QStringList made;
	if (mime->hasUrls()) {
		for (const QUrl &url : remoteUrls(mime)) {
			QString title = linkTitle(mime, url).remove(QRegularExpression("[/\\\\:\\n]")).simplified().left(60);
			if (title.isEmpty()) {
				title = "Internet location";
			}
			const QString path = freePath(folder, title, ".url");
			const QByteArray body = "[InternetShortcut]\r\nURL=" + url.toString(QUrl::FullyEncoded).toUtf8() + "\r\n";
			if (!save(path, body, error)) {
				return made;
			}
			made << path;
		}
		return made;
	}
	if (mime->hasImage()) {
		const QImage image = qvariant_cast<QImage>(mime->imageData());
		const QString path = freePath(folder, "picture clipping", ".png");
		if (image.isNull() || !image.save(path, "PNG")) {
			*error = "The picture couldn't be saved.";
			return made;
		}
		return { path };
	}
	const QString text = mime->text();
	const QString path = freePath(folder, clippingBaseName(text) + " clipping", ".txt");
	if (save(path, text.toUtf8(), error)) {
		made << path;
	}
	return made;
}

void addClippingContent(QMimeData *mime, const QString &path) {
	switch (clippingKind(path)) {
	case ClippingKind::Text: {
		QFile f(path);
		if (f.open(QIODevice::ReadOnly)) {
			mime->setText(QString::fromUtf8(f.readAll()));
		}
		break;
	}
	case ClippingKind::Picture: {
		const QImage image(path);
		if (!image.isNull()) {
			mime->setImageData(image);
		}
		break;
	}
	case ClippingKind::Location: {
		const QString url = internetLocationUrl(path);
		if (!url.isEmpty()) {
			mime->setText(url);
		}
		break;
	}
	case ClippingKind::None:
		break;
	}
}

/* ---- the windows ------------------------------------------------------------------- */

/* Shows text or a picture: a clipping's, or the clipboard's. */
class ContentView : public QWidget {
public:
	explicit ContentView(QWidget *parent = nullptr) : QWidget(parent) {
		auto *layout = new QVBoxLayout(this);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setSpacing(0);
		m_text = new QPlainTextEdit(this);
		m_text->setReadOnly(true);
		m_text->setFrameShape(QFrame::NoFrame);
		m_scroll = new QScrollArea(this);
		m_scroll->setFrameShape(QFrame::NoFrame);
		m_scroll->setAlignment(Qt::AlignCenter);
		m_picture = new QLabel;
		m_scroll->setWidget(m_picture);
		m_empty = new QLabel(this);
		m_empty->setAlignment(Qt::AlignCenter);
		layout->addWidget(m_text);
		layout->addWidget(m_scroll);
		layout->addWidget(m_empty);
		m_status = new QLabel(this);
		m_status->setContentsMargins(6, 3, 6, 3);
		layout->addWidget(m_status);
	}
	void showText(const QString &text) {
		m_text->setPlainText(text);
		reveal(m_text);
	}
	void showPicture(const QImage &image) {
		m_picture->setPixmap(QPixmap::fromImage(image));
		m_picture->resize(image.size());
		reveal(m_scroll);
	}
	void showNothing(const QString &why) {
		m_empty->setText(why);
		reveal(m_empty);
	}
	void setStatus(const QString &status) {
		m_status->setText(status);
		m_status->setVisible(!status.isEmpty());
	}

private:
	void reveal(QWidget *which) {
		for (QWidget *w : { static_cast<QWidget *>(m_text), static_cast<QWidget *>(m_scroll),
				static_cast<QWidget *>(m_empty) }) {
			w->setVisible(w == which);
		}
	}
	QPlainTextEdit *m_text;
	QScrollArea *m_scroll;
	QLabel *m_picture, *m_empty, *m_status;
};

static QSize fitted(QSize content) {
	return content.boundedTo(QSize(640, 480)).expandedTo(QSize(200, 120));
}

void openClippingWindow(const QString &path) {
	auto *w = new ContentView;
	w->setAttribute(Qt::WA_DeleteOnClose);
	QString name = QFileInfo(path).completeBaseName();
	w->setWindowTitle(name);
	w->setStatus(QString());
	if (clippingKind(path) == ClippingKind::Picture) {
		const QImage image(path);
		w->showPicture(image);
		w->resize(fitted(image.size() + QSize(4, 4)));
	} else {
		QFile f(path);
		w->showText(f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString());
		w->resize(360, 220);
	}
	w->show();
	w->activateWindow();
}

void showClipboardWindow() {
	static QPointer<ContentView> window;
	if (!window) {
		window = new ContentView;
		window->setAttribute(Qt::WA_DeleteOnClose);
		window->setWindowTitle("Clipboard");
		window->resize(360, 220);
		ContentView *w = window;
		auto update = [w] {
			const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
			if (mime && mime->hasImage()) {
				w->showPicture(qvariant_cast<QImage>(mime->imageData()));
				w->setStatus("Clipboard contents: picture");
			} else if (mime && mime->hasText() && !mime->text().isEmpty()) {
				w->showText(mime->text());
				w->setStatus("Clipboard contents: text");
			} else {
				w->showNothing(QString());
				w->setStatus("Clipboard contents: none");
			}
		};
		QObject::connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, w, update);
		update();
	}
	window->show();
	window->raise();
	window->activateWindow();
}
