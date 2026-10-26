#pragma once

/*
 * The viewer's side of VNC (RFB 3.8, RFC 6143), for
 * zacos9-screenshare-viewer: enough to see and use another ZacOS
 * computer's screen through its zacos9-screenshare-host (wayvnc).
 *
 *   security   None, or VeNCrypt X509Plain: TLS (the host's self-signed
 *              certificate, checked by its fingerprint, which its Screen
 *              Sharing panel shows), then the owner's name and password
 *   encodings  ZRLE (zlib, one stream for the connection), CopyRect, Raw;
 *              the host's pointer shape (Cursor) and screen size
 *              (DesktopSize)
 *   pixels     32 bits, 0x00RRGGBB: a QImage::Format_RGB32 framebuffer
 *
 * Asynchronous: callbacks report what the server says; the socket is
 * read as bytes arrive, a message at a time once it is all there.
 */

#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSslSocket>
#include <functional>
#include <memory>
#include <zlib.h>

namespace screenshare {

/* Decodes ZRLE's data (already inflated) for a w x h rectangle into dst
 * (stride in pixels), 32-bit pixels sent as 3-byte CPIXELs. False if the
 * data is short or wrong. (Kept apart to be tested.) */
bool zrleDecode(const uint8_t *data, size_t size, int w, int h, uint32_t *dst, int stride);

/* Where a w x h framebuffer goes on a W x H screen (pixels): one for one
 * if it fits, else shrunk evenly to fit; centred, never stretched. */
QRect fitRect(int w, int h, int screenW, int screenH);

class RfbClient : public QObject {
public:
	RfbClient();
	~RfbClient() override;

	/* The certificate's fingerprint ("AB12 CD34 …"): true to go on. */
	std::function<bool(const QString &fingerprint)> checkCertificate;
	/* The owner's name and password; false to give up. */
	std::function<bool(QString *user, QString *password, const QString &error)> credentials;
	std::function<void(const QString &name)> connected; /* the server's name for itself */
	std::function<void()> resized;                       /* framebuffer() has a new size */
	std::function<void(const QRect &)> updated;          /* pixels changed there */
	std::function<void(const QImage &, QPoint hotspot)> cursorShape;
	std::function<void(const QString &why)> failed;      /* the connection is over */

	void open(const QString &host, quint16 port);
	void close();
	const QImage &framebuffer() const { return m_fb; }

	/* Input, in framebuffer pixels. Buttons: 1 left, 2 middle, 4 right,
	 * 8/16 wheel up/down, 32/64 wheel left/right. */
	void pointer(QPoint at, int buttons);
	void key(quint32 keysym, bool down);

private:
	enum class State {
		Version, SecurityTypes, VeNCryptVersion, VeNCryptAck, VeNCryptSubtypes, VeNCryptChosen,
		Tls, SecurityResult, SecurityReason, ServerInit, Running, Done,
	};
	void readable();
	bool step();               /* one message (or part) from m_in; false: wait */
	bool serverMessage();
	bool rectangle();
	void fail(const QString &why);
	void sendCredentials();
	void requestUpdate(bool incremental);
	bool need(qsizetype n) const { return m_in.size() - m_pos >= n; }
	quint8 u8();
	quint16 u16();
	quint32 u32();
	qint32 s32() { return static_cast<qint32>(u32()); }
	QByteArray bytes(qsizetype n);
	void consume() { m_in.remove(0, m_pos); m_pos = 0; }
	bool inflateZrle(const QByteArray &in, QByteArray *out);

	QSslSocket m_socket;
	QString m_host;
	State m_state = State::Version;
	QByteArray m_in;
	qsizetype m_pos = 0;
	bool m_vencrypt = false;
	QString m_error; /* the last attempt's, for the credentials prompt */
	QImage m_fb;
	int m_rectsLeft = 0;
	QRect m_damage;
	z_stream m_zlib{};
	bool m_zlibOpen = false;
};

} // namespace screenshare
