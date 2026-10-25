#include "sound.h"

#include <QSslCertificate>
#include <QSslConfiguration>
#include <sys/prctl.h>
#include <csignal>

#include "common.h"

namespace screenshare {

/* More than this waiting for pacat, and what comes is dropped: about a
 * fifth of a second. */
static constexpr qint64 PLAY_QUEUE_LIMIT = AUDIO_RATE * AUDIO_CHANNELS * 2 / 5;

SoundClient::SoundClient() {
	m_play.setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
	m_play.setProcessChannelMode(QProcess::ForwardedChannels);
	connect(&m_play, &QProcess::finished, this, [this] {
		if (m_state == State::Playing) {
			stop("The sound couldn’t be played here.");
		}
	});
	connect(&m_play, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			stop("The sound couldn’t be played here (is pacat installed?).");
		}
	});
	connect(&m_socket, &QSslSocket::readyRead, this, [this] { readable(); });
	connect(&m_socket, &QSslSocket::disconnected, this, [this] {
		readable(); /* its last words: why not */
		stop(m_state == State::Playing ? "The other computer stopped sending its sound."
		                               : "The other computer didn’t send its sound.");
	});
	connect(&m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
		if (e == QAbstractSocket::RemoteHostClosedError) {
			return; /* disconnected() says */
		}
		stop(e == QAbstractSocket::ConnectionRefusedError ? "The other computer doesn’t share its sound."
		                                                   : m_socket.errorString());
	});
}

SoundClient::~SoundClient() {
	m_state = State::Closed;
	m_socket.abort();
	if (m_play.state() != QProcess::NotRunning) {
		m_play.kill();
	}
}

void SoundClient::open(const QString &host, quint16 port, const QString &fingerprint, const QString &user,
		const QString &password) {
	close();
	m_state = State::Hello;
	m_fingerprint = fingerprint;
	m_hello = "ZACOS-SOUND 1\n" + user.toUtf8() + '\n' + password.toUtf8() + '\n';
	/* Checked by its fingerprint, as the screen's was. */
	QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
	tls.setPeerVerifyMode(QSslSocket::VerifyNone);
	m_socket.setSslConfiguration(tls);
	QObject::disconnect(&m_socket, &QSslSocket::encrypted, this, nullptr);
	connect(&m_socket, &QSslSocket::encrypted, this, [this] {
		if (m_fingerprint.isEmpty() || derFingerprint(m_socket.peerCertificate().toDer()) != m_fingerprint) {
			stop("The sound came with a different certificate from the screen’s.");
			return;
		}
		m_socket.write(m_hello);
		m_hello.fill(0);
		m_hello.clear();
	});
	m_socket.connectToHostEncrypted(host, port);
	m_socket.setSocketOption(QAbstractSocket::LowDelayOption, 1);
}

void SoundClient::close() {
	if (m_state == State::Closed) {
		return;
	}
	stop(QString());
}

void SoundClient::stop(const QString &why) {
	if (m_state == State::Closed) {
		return;
	}
	m_state = State::Closed;
	m_hello.fill(0);
	m_hello.clear();
	m_socket.abort();
	if (m_play.state() != QProcess::NotRunning) {
		m_play.kill();
	}
	if (stopped) {
		stopped(why);
	}
}

void SoundClient::readable() {
	if (m_state == State::Hello) {
		if (!m_socket.canReadLine()) {
			return;
		}
		const QString line = QString::fromUtf8(m_socket.readLine()).trimmed();
		const QStringList w = line.split(' ');
		if (w.value(0) != "ok" || w.value(1).toInt() <= 0 || w.value(2).toInt() <= 0) {
			stop(line.isEmpty() ? QString("The other computer didn’t send its sound.") : line);
			return;
		}
		m_state = State::Playing;
		m_frame = w[2].toInt() * 2;
		m_play.start(qEnvironmentVariable("ZACOS9_PACAT", "pacat"), { "--playback", "--raw",
			"--format=s16le", "--rate=" + w[1], "--channels=" + w[2], "--latency-msec=60",
			"--client-name=Screen Sharing", "--stream-name=" + m_socket.peerName() });
		if (playing) {
			playing();
		}
	}
	if (m_state != State::Playing) {
		return;
	}
	/* Whole frames only: a half one would swap left and right. */
	const qint64 whole = m_socket.bytesAvailable() / m_frame * m_frame;
	if (whole == 0) {
		return;
	}
	const QByteArray samples = m_socket.read(whole);
	if (m_play.state() == QProcess::Running && m_play.bytesToWrite() < PLAY_QUEUE_LIMIT) {
		m_play.write(samples);
	}
}

} // namespace screenshare
