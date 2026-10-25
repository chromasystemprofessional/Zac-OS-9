#pragma once

/*
 * The other computer's sound, for zacos9-screenshare-viewer: its own
 * connection to zacos9-screenshare-host, on the screen's port plus one
 * (common.h has the protocol). TLS with the certificate the screen's
 * connection was checked against, the same name and password, then the
 * samples, played here by pacat. Late samples are dropped rather than
 * queued, so the sound keeps up with the screen.
 *
 * ZACOS9_PACAT stands in for pacat in tests.
 */

#include <QObject>
#include <QProcess>
#include <QSslSocket>
#include <functional>

#include "common.h"

namespace screenshare {

class SoundClient : public QObject {
public:
	SoundClient();
	~SoundClient() override;

	std::function<void()> playing;
	/* Not (or no longer) playing; `why` is empty when close() was asked. */
	std::function<void(const QString &why)> stopped;

	/* `fingerprint`: the screen's certificate's, which this one must match. */
	void open(const QString &host, quint16 port, const QString &fingerprint, const QString &user,
		const QString &password);
	void close();
	bool isOpen() const { return m_state != State::Closed; }

private:
	enum class State { Closed, Hello, Playing };
	void readable();
	void stop(const QString &why);

	QSslSocket m_socket;
	QProcess m_play;
	State m_state = State::Closed;
	QString m_fingerprint;
	QByteArray m_hello;
	int m_frame = AUDIO_CHANNELS * 2; /* bytes: a sample for each channel */
};

} // namespace screenshare
