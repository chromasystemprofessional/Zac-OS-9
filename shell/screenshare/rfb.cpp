#include "rfb.h"

#include <QSslCertificate>
#include <QSslConfiguration>
#include <QtEndian>
#include <cstring>

#include "common.h"

namespace screenshare {

namespace {
constexpr quint32 SEC_NONE = 1, SEC_VENCRYPT = 19;
constexpr quint32 VENCRYPT_X509PLAIN = 262;
constexpr qint32 ENC_RAW = 0, ENC_COPYRECT = 1, ENC_ZRLE = 16, ENC_CURSOR = -239, ENC_DESKTOPSIZE = -223;
constexpr int MAX_SIDE = 16384;
} // namespace

/* ---- pure parts ------------------------------------------------------------- */

QRect fitRect(int w, int h, int screenW, int screenH) {
	if (w <= 0 || h <= 0 || screenW <= 0 || screenH <= 0) {
		return QRect();
	}
	if (w <= screenW && h <= screenH) {
		return QRect((screenW - w) / 2, (screenH - h) / 2, w, h);
	}
	const double s = std::min(double(screenW) / w, double(screenH) / h);
	const int dw = std::max(1, std::min(screenW, int(w * s + 0.5)));
	const int dh = std::max(1, std::min(screenH, int(h * s + 0.5)));
	return QRect((screenW - dw) / 2, (screenH - dh) / 2, dw, dh);
}

namespace {
/* Reads ZRLE's bytes, failing (not crashing) past the end. */
struct Bytes {
	const uint8_t *p, *end;
	bool ok = true;
	uint8_t u8() {
		if (p >= end) {
			ok = false;
			return 0;
		}
		return *p++;
	}
	uint32_t cpixel() { /* 3 bytes: 0x00RRGGBB, little-endian */
		const uint32_t b = u8(), g = u8(), r = u8();
		return 0xFF000000u | r << 16 | g << 8 | b;
	}
	int run() { /* 1 + bytes of 255 ... + a last byte under 255 */
		int n = 1;
		for (uint8_t b = 255; ok && b == 255;) {
			b = u8();
			n += b;
			if (n > 64 * 64 + 255) {
				ok = false;
			}
		}
		return n;
	}
};
} // namespace

bool zrleDecode(const uint8_t *data, size_t size, int w, int h, uint32_t *dst, int stride) {
	Bytes in{ data, data + size };
	for (int ty = 0; ty < h; ty += 64) {
		for (int tx = 0; tx < w; tx += 64) {
			const int tw = std::min(64, w - tx), th = std::min(64, h - ty);
			uint32_t *tile = dst + (size_t)ty * stride + tx;
			const int sub = in.u8();
			if (!in.ok) {
				return false;
			}
			auto put = [&](int i, uint32_t px) { tile[(size_t)(i / tw) * stride + i % tw] = px; };
			const int n = tw * th;
			if (sub == 0) {
				for (int i = 0; i < n; i++) {
					put(i, in.cpixel());
				}
			} else if (sub == 1) {
				const uint32_t px = in.cpixel();
				for (int i = 0; i < n; i++) {
					put(i, px);
				}
			} else if (sub <= 16) {
				uint32_t palette[16];
				for (int k = 0; k < sub; k++) {
					palette[k] = in.cpixel();
				}
				const int bits = sub == 2 ? 1 : sub <= 4 ? 2 : 4;
				for (int y = 0; y < th; y++) {
					int have = 0;
					uint8_t byte = 0;
					for (int x = 0; x < tw; x++) {
						if (have == 0) {
							byte = in.u8();
							have = 8;
						}
						have -= bits;
						const int index = (byte >> have) & ((1 << bits) - 1);
						if (index >= sub) {
							return false;
						}
						put(y * tw + x, palette[index]);
					}
				}
			} else if (sub == 128) {
				for (int i = 0; i < n && in.ok;) {
					const uint32_t px = in.cpixel();
					const int len = in.run();
					if (i + len > n) {
						return false;
					}
					for (int k = 0; k < len; k++) {
						put(i++, px);
					}
				}
			} else if (sub >= 130) {
				const int colours = sub - 128;
				uint32_t palette[127];
				for (int k = 0; k < colours; k++) {
					palette[k] = in.cpixel();
				}
				for (int i = 0; i < n && in.ok;) {
					const uint8_t b = in.u8();
					const int index = b & 127;
					if (index >= colours) {
						return false;
					}
					const int len = b & 128 ? in.run() : 1;
					if (i + len > n) {
						return false;
					}
					for (int k = 0; k < len; k++) {
						put(i++, palette[index]);
					}
				}
			} else {
				return false; /* 17-127 and 129 are unused */
			}
			if (!in.ok) {
				return false;
			}
		}
	}
	return true;
}

/* ---- the connection -------------------------------------------------------- */

RfbClient::RfbClient() {
	connect(&m_socket, &QSslSocket::connected, this, [this] { m_state = State::Version; });
	connect(&m_socket, &QSslSocket::readyRead, this, [this] { readable(); });
	connect(&m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
		if (m_state == State::Done) {
			return;
		}
		if (e == QAbstractSocket::RemoteHostClosedError && m_state == State::Running) {
			fail("The other computer ended the connection.");
		} else if (e == QAbstractSocket::ConnectionRefusedError) {
			fail("Screen sharing isn’t on at " + m_host + ".");
		} else if (e == QAbstractSocket::HostNotFoundError) {
			fail("No computer called " + m_host + " was found.");
		} else {
			fail(m_socket.errorString());
		}
	});
	connect(&m_socket, &QSslSocket::encrypted, this, [this] {
		const QString print = derFingerprint(m_socket.peerCertificate().toDer());
		if (!checkCertificate || !checkCertificate(print)) {
			fail(QString());
			return;
		}
		sendCredentials();
	});
}

RfbClient::~RfbClient() {
	m_state = State::Done;
	m_socket.abort();
	if (m_zlibOpen) {
		inflateEnd(&m_zlib);
	}
}

void RfbClient::open(const QString &host, quint16 port) {
	m_host = host;
	m_in.clear();
	m_pos = 0;
	m_state = State::Version;
	if (m_zlibOpen) {
		inflateEnd(&m_zlib);
		m_zlibOpen = false;
	}
	/* The host's certificate is its own (self-signed): it is checked by
	 * its fingerprint, not by a certificate authority. */
	QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
	tls.setPeerVerifyMode(QSslSocket::VerifyNone);
	m_socket.setSslConfiguration(tls);
	m_socket.setReadBufferSize(0);
	m_socket.connectToHost(host, port);
	m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
}

void RfbClient::close() {
	m_state = State::Done;
	m_socket.abort();
}

void RfbClient::fail(const QString &why) {
	if (m_state == State::Done) {
		return;
	}
	m_state = State::Done;
	m_socket.abort();
	if (failed) {
		failed(why);
	}
}

quint8 RfbClient::u8() {
	return static_cast<quint8>(m_in[m_pos++]);
}

quint16 RfbClient::u16() {
	const quint16 v = qFromBigEndian<quint16>(m_in.constData() + m_pos);
	m_pos += 2;
	return v;
}

quint32 RfbClient::u32() {
	const quint32 v = qFromBigEndian<quint32>(m_in.constData() + m_pos);
	m_pos += 4;
	return v;
}

QByteArray RfbClient::bytes(qsizetype n) {
	const QByteArray b = m_in.mid(m_pos, n);
	m_pos += n;
	return b;
}

void RfbClient::readable() {
	m_in += m_socket.readAll();
	while (m_state != State::Done && m_state != State::Tls) {
		m_pos = 0;
		if (!step()) {
			break;
		}
		consume();
	}
}

void RfbClient::sendCredentials() {
	QString user, password;
	if (!credentials || !credentials(&user, &password, m_error)) {
		fail(QString());
		return;
	}
	const QByteArray u = user.toUtf8(), p = password.toUtf8();
	QByteArray out(8, 0);
	qToBigEndian<quint32>(u.size(), out.data());
	qToBigEndian<quint32>(p.size(), out.data() + 4);
	m_socket.write(out + u + p);
	m_state = State::SecurityResult;
}

bool RfbClient::step() {
	switch (m_state) {
	case State::Version: {
		if (!need(12)) {
			return false;
		}
		const QByteArray v = bytes(12);
		if (!v.startsWith("RFB 003.")) {
			fail(m_host + " isn’t a screen sharing server.");
			return false;
		}
		m_socket.write("RFB 003.008\n");
		m_state = State::SecurityTypes;
		return true;
	}
	case State::SecurityTypes: {
		if (!need(1)) {
			return false;
		}
		const int n = u8();
		if (n == 0) {
			m_state = State::SecurityReason;
			return true;
		}
		if (!need(n)) {
			return false;
		}
		bool none = false, vencrypt = false;
		for (int i = 0; i < n; i++) {
			const quint8 t = u8();
			none = none || t == SEC_NONE;
			vencrypt = vencrypt || t == SEC_VENCRYPT;
		}
		m_vencrypt = vencrypt;
		if (vencrypt) {
			m_socket.write(QByteArray(1, char(SEC_VENCRYPT)));
			m_state = State::VeNCryptVersion;
		} else if (none) {
			m_socket.write(QByteArray(1, char(SEC_NONE)));
			m_state = State::SecurityResult;
		} else {
			fail(m_host + " asks for a kind of password this viewer doesn’t know.");
			return false;
		}
		return true;
	}
	case State::VeNCryptVersion:
		if (!need(2)) {
			return false;
		}
		if (u8() != 0 || u8() < 2) {
			fail(m_host + "’s encryption is too old.");
			return false;
		}
		m_socket.write(QByteArray("\x00\x02", 2));
		m_state = State::VeNCryptAck;
		return true;
	case State::VeNCryptAck:
		if (!need(1)) {
			return false;
		}
		if (u8() != 0) {
			fail(m_host + " refused the encryption.");
			return false;
		}
		m_state = State::VeNCryptSubtypes;
		return true;
	case State::VeNCryptSubtypes: {
		if (!need(1)) {
			return false;
		}
		const int n = u8();
		if (!need(4 * n)) {
			return false;
		}
		bool x509plain = false;
		for (int i = 0; i < n; i++) {
			x509plain = x509plain || u32() == VENCRYPT_X509PLAIN;
		}
		if (!x509plain) {
			fail(m_host + " doesn’t offer an encrypted connection with a password.");
			return false;
		}
		QByteArray out(4, 0);
		qToBigEndian<quint32>(VENCRYPT_X509PLAIN, out.data());
		m_socket.write(out);
		m_state = State::VeNCryptChosen;
		return true;
	}
	case State::VeNCryptChosen:
		if (!need(1)) {
			return false;
		}
		if (u8() != 1) {
			fail(m_host + " refused the encryption.");
			return false;
		}
		/* TLS from here on; the password goes over it (encrypted). */
		m_state = State::Tls;
		m_socket.startClientEncryption();
		return true;
	case State::SecurityResult: {
		if (!need(4)) {
			return false;
		}
		if (u32() == 0) {
			m_error.clear();
			m_socket.write(QByteArray(1, 1)); /* ClientInit: shared */
			m_state = State::ServerInit;
			return true;
		}
		m_state = State::SecurityReason;
		return true;
	}
	case State::SecurityReason: {
		if (!need(4)) {
			return false;
		}
		const quint32 len = u32();
		if (len > 4096 || !need(len)) {
			return len <= 4096 ? false : (fail("The connection was refused."), false);
		}
		const QString why = QString::fromUtf8(bytes(len));
		if (m_vencrypt) {
			/* A wrong name or password: the server hangs up; ask again on
			 * a new connection. */
			m_error = "The name or password isn’t right. Try again.";
			const QString host = m_host;
			const quint16 port = m_socket.peerPort();
			m_state = State::Done;
			m_socket.abort();
			open(host, port);
			return false;
		}
		fail(why.isEmpty() ? QString("The connection was refused.") : why);
		return false;
	}
	case State::ServerInit: {
		if (!need(24)) {
			return false;
		}
		const int w = u16(), h = u16();
		m_pos += 16; /* its pixel format: ours comes next */
		const quint32 len = u32();
		if (len > 4096) {
			fail(m_host + " sent a name too long.");
			return false;
		}
		if (!need(len)) {
			return false;
		}
		const QString name = QString::fromUtf8(bytes(len));
		if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE) {
			fail(m_host + "’s screen has no size.");
			return false;
		}
		m_fb = QImage(w, h, QImage::Format_RGB32);
		m_fb.fill(Qt::black);
		/* SetPixelFormat: 32 bits, 24 deep, true colour, 0x00RRGGBB. */
		const char format[20] = { 0, 0, 0, 0, 32, 24, 0, 1, 0, char(255), 0, char(255), 0, char(255),
			16, 8, 0, 0, 0, 0 };
		m_socket.write(format, sizeof(format));
		/* SetEncodings: the best first. */
		const qint32 encodings[] = { ENC_ZRLE, ENC_COPYRECT, ENC_RAW, ENC_CURSOR, ENC_DESKTOPSIZE };
		QByteArray set(4 + 4 * std::size(encodings), 0);
		set[0] = 2;
		qToBigEndian<quint16>(std::size(encodings), set.data() + 2);
		for (size_t i = 0; i < std::size(encodings); i++) {
			qToBigEndian<qint32>(encodings[i], set.data() + 4 + 4 * i);
		}
		m_socket.write(set);
		m_state = State::Running;
		if (resized) {
			resized();
		}
		if (connected) {
			connected(name);
		}
		requestUpdate(false);
		return true;
	}
	case State::Running:
		return m_rectsLeft > 0 ? rectangle() : serverMessage();
	case State::Tls:
	case State::Done:
		return false;
	}
	return false;
}

void RfbClient::requestUpdate(bool incremental) {
	QByteArray out(10, 0);
	out[0] = 3;
	out[1] = incremental ? 1 : 0;
	qToBigEndian<quint16>(m_fb.width(), out.data() + 6);
	qToBigEndian<quint16>(m_fb.height(), out.data() + 8);
	m_socket.write(out);
}

bool RfbClient::serverMessage() {
	if (!need(1)) {
		return false;
	}
	const int type = u8();
	switch (type) {
	case 0: /* FramebufferUpdate */
		if (!need(3)) {
			return false;
		}
		m_pos += 1;
		m_rectsLeft = u16();
		m_damage = QRect();
		if (m_rectsLeft == 0) {
			requestUpdate(true);
		}
		return true;
	case 1: { /* SetColourMapEntries: not with true colour, but read past it */
		if (!need(5)) {
			return false;
		}
		m_pos += 3;
		const int n = u16();
		if (!need(6 * n)) {
			return false;
		}
		m_pos += 6 * n;
		return true;
	}
	case 2: /* Bell */
		return true;
	case 3: { /* ServerCutText: not yet */
		if (!need(7)) {
			return false;
		}
		m_pos += 3;
		const qint32 len = s32();
		const qint64 skip = len < 0 ? -qint64(len) : len;
		if (skip > (64 << 20)) {
			fail(m_host + " sent too much at once.");
			return false;
		}
		if (!need(skip)) {
			return false;
		}
		m_pos += skip;
		return true;
	}
	default:
		fail(QString("%1 sent something this viewer doesn’t understand (%2).").arg(m_host).arg(type));
		return false;
	}
}

bool RfbClient::rectangle() {
	if (!need(12)) {
		return false;
	}
	const int x = u16(), y = u16(), w = u16(), h = u16();
	const qint32 enc = s32();
	const QRect r(x, y, w, h);
	const bool inside = w >= 0 && h >= 0 && x + w <= m_fb.width() && y + h <= m_fb.height();
	auto pixels = [this](int px, int py) {
		return reinterpret_cast<uint32_t *>(m_fb.scanLine(py)) + px;
	};
	switch (enc) {
	case ENC_RAW: {
		if (!inside) {
			fail(m_host + " sent a picture off its screen.");
			return false;
		}
		if (!need(qsizetype(w) * h * 4)) {
			return false;
		}
		for (int j = 0; j < h; j++) {
			uint32_t *row = pixels(x, y + j);
			for (int i = 0; i < w; i++) {
				row[i] = 0xFF000000u | qFromLittleEndian<quint32>(m_in.constData() + m_pos);
				m_pos += 4;
			}
		}
		break;
	}
	case ENC_COPYRECT: {
		if (!need(4)) {
			return false;
		}
		const int sx = u16(), sy = u16();
		if (!inside || sx + w > m_fb.width() || sy + h > m_fb.height()) {
			fail(m_host + " sent a picture off its screen.");
			return false;
		}
		const QImage from = m_fb.copy(sx, sy, w, h);
		for (int j = 0; j < h; j++) {
			memcpy(pixels(x, y + j), from.constScanLine(j), size_t(w) * 4);
		}
		break;
	}
	case ENC_ZRLE: {
		if (!need(4)) {
			return false;
		}
		const quint32 len = u32();
		if (len > (64u << 20) || !inside) {
			fail(m_host + " sent a picture this viewer can’t show.");
			return false;
		}
		if (!need(len)) {
			return false;
		}
		QByteArray raw;
		if (!inflateZrle(bytes(len), &raw) ||
				!zrleDecode(reinterpret_cast<const uint8_t *>(raw.constData()), raw.size(), w, h,
					w && h ? pixels(x, y) : nullptr, m_fb.bytesPerLine() / 4)) {
			fail(m_host + " sent a picture this viewer can’t show.");
			return false;
		}
		break;
	}
	case ENC_CURSOR: { /* x, y: its hotspot */
		const qsizetype mask = qsizetype((w + 7) / 8) * h;
		if (w > 256 || h > 256) {
			fail(m_host + " sent a pointer too big.");
			return false;
		}
		if (!need(qsizetype(w) * h * 4 + mask)) {
			return false;
		}
		QImage shape(std::max(w, 1), std::max(h, 1), QImage::Format_ARGB32);
		shape.fill(Qt::transparent);
		const qsizetype bits = m_pos + qsizetype(w) * h * 4;
		for (int j = 0; j < h; j++) {
			for (int i = 0; i < w; i++) {
				const quint32 px = qFromLittleEndian<quint32>(m_in.constData() + m_pos + (qsizetype(j) * w + i) * 4);
				const bool on = static_cast<quint8>(m_in[bits + j * ((w + 7) / 8) + i / 8]) & (0x80 >> (i % 8));
				shape.setPixel(i, j, on ? (0xFF000000u | px) : 0);
			}
		}
		m_pos = bits + mask;
		if (cursorShape && w > 0 && h > 0) {
			cursorShape(shape, QPoint(x, y));
		}
		break;
	}
	case ENC_DESKTOPSIZE:
		if (w <= 0 || h <= 0 || w > MAX_SIDE || h > MAX_SIDE) {
			fail(m_host + "’s screen has no size.");
			return false;
		}
		m_fb = QImage(w, h, QImage::Format_RGB32);
		m_fb.fill(Qt::black);
		m_damage = m_fb.rect();
		if (resized) {
			resized();
		}
		break;
	default:
		fail(QString("%1 sent a picture in a form this viewer doesn’t know (%2).").arg(m_host).arg(enc));
		return false;
	}
	if (enc != ENC_CURSOR && enc != ENC_DESKTOPSIZE) {
		m_damage = m_damage.united(r);
	}
	if (--m_rectsLeft == 0) {
		if (updated && !m_damage.isEmpty()) {
			updated(m_damage);
		}
		requestUpdate(enc != ENC_DESKTOPSIZE);
	}
	return true;
}

bool RfbClient::inflateZrle(const QByteArray &in, QByteArray *out) {
	if (!m_zlibOpen) {
		if (inflateInit(&m_zlib) != Z_OK) {
			return false;
		}
		m_zlibOpen = true;
	}
	m_zlib.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(in.constData()));
	m_zlib.avail_in = static_cast<uInt>(in.size());
	out->clear();
	char chunk[1 << 16];
	while (m_zlib.avail_in > 0) {
		m_zlib.next_out = reinterpret_cast<Bytef *>(chunk);
		m_zlib.avail_out = sizeof(chunk);
		const int r = inflate(&m_zlib, Z_SYNC_FLUSH);
		if (r != Z_OK && r != Z_BUF_ERROR && r != Z_STREAM_END) {
			return false;
		}
		out->append(chunk, sizeof(chunk) - m_zlib.avail_out);
		if (out->size() > (256 << 20)) {
			return false;
		}
		if (r == Z_BUF_ERROR && m_zlib.avail_out != 0) {
			break;
		}
	}
	return true;
}

void RfbClient::pointer(QPoint at, int buttons) {
	if (m_state != State::Running) {
		return;
	}
	QByteArray out(6, 0);
	out[0] = 5;
	out[1] = char(buttons);
	qToBigEndian<quint16>(std::clamp(at.x(), 0, std::max(0, m_fb.width() - 1)), out.data() + 2);
	qToBigEndian<quint16>(std::clamp(at.y(), 0, std::max(0, m_fb.height() - 1)), out.data() + 4);
	m_socket.write(out);
}

void RfbClient::key(quint32 keysym, bool down) {
	if (m_state != State::Running || keysym == 0) {
		return;
	}
	QByteArray out(8, 0);
	out[0] = 4;
	out[1] = down ? 1 : 0;
	qToBigEndian<quint32>(keysym, out.data() + 4);
	m_socket.write(out);
}

} // namespace screenshare
