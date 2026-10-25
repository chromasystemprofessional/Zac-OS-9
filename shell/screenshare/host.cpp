/*
 * zacos9-screenshare-host: lets other ZacOS computers see this screen,
 * for the Screen Sharing control panel (shell/panels/screensharing.cpp).
 *
 * wayvnc captures the main screen and takes the remote mouse and
 * keyboard; it listens only on a socket of ours (mode 0700), with TLS and
 * the owner's login password (PAM: run as the owner, pam_unix can check
 * no one else's). This program alone listens on the network, on port
 * 5900: it asks the owner before letting each computer in ("Ask before
 * someone views my screen"), then passes the bytes through untouched. It
 * also announces the computer over Bonjour as _rfb._tcp, with zacos=1 so
 * other ZacOS computers can tell it from any VNC server, and answers the
 * panel on host.sock (common.h).
 *
 * The sound (common.h), on the next port: what this computer plays (the
 * default output's monitor, from parec), to the computers the owner let
 * in, once they give the owner's password (PAM, as zacos9-lock checks
 * it). Late samples are dropped rather than queued: the sound keeps up
 * with the screen.
 *
 * ZACOS9_WAYVNC, ZACOS9_PAREC and ZACOS9_AVAHI_PUBLISH ("none" for no
 * Bonjour) stand in for tests; ZACOS9_SCREENSHARE_PORT moves the port.
 * Built with ZACOS9_SCREENSHARE_TEST, "sesame" is the password and PAM
 * isn't asked.
 */
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <csignal>
#include <map>
#include <security/pam_appl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "alert.h"
#include "common.h"
#include "platinumshell.h"

using namespace screenshare;

/* How much one side may have queued before we stop reading the other:
 * the sender then waits in the kernel instead of in our memory. */
static constexpr qint64 QUEUE_LIMIT = 4 << 20;
static constexpr int ASK_TIMEOUT_MS = 30000;
/* About a fifth of a second of sound: more queued than that and the
 * network is behind, so what parec gives is dropped instead. */
static constexpr qint64 SOUND_QUEUE_LIMIT = AUDIO_RATE * AUDIO_CHANNELS * 2 / 5;
static constexpr int SOUND_HELLO_TIMEOUT_MS = 15000;

static void dieWithParent(QProcess *p) {
	p->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGTERM); });
}

/* As afpclient.cpp's: QProcess::waitForFinished() deadlocks here. */
static bool waitForProcess(QProcess *p, int timeoutMs) {
	bool done = false;
	QObject::connect(p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	QElapsedTimer clock;
	clock.start();
	while (!done && clock.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
	}
	if (!done) {
		p->kill();
	}
	return done && p->exitStatus() == QProcess::NormalExit && p->exitCode() == 0;
}

static void waitFor(const std::function<bool()> &ready, int timeoutMs) {
	QElapsedTimer clock;
	clock.start();
	while (!ready() && clock.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 100);
	}
}

/* A self-signed certificate, made once; the panel shows its fingerprint
 * so the other computer can check it's talking to this one. */
static bool ensureCertificate() {
	if (QFile::exists(certFile()) && QFile::exists(keyFile())) {
		return true;
	}
	const QString dir = QFileInfo(certFile()).absolutePath();
	QDir().mkpath(dir);
	QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
	QProcess p;
	p.setProcessChannelMode(QProcess::ForwardedErrorChannel);
	p.start("openssl", { "req", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
		"-nodes", "-days", "3650", "-subj", "/CN=" + computerName(),
		"-keyout", keyFile(), "-out", certFile() });
	if (!waitForProcess(&p, 20000)) {
		QFile::remove(keyFile());
		QFile::remove(certFile());
		return false;
	}
	QFile::setPermissions(keyFile(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	return true;
}

/* The screen with the menu bar, from the compositor's list of screens
 * (compositor/src/prefs.c); empty to let wayvnc take the first. */
static QString mainOutput() {
	QFile f(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/zacos9-outputs-" +
		qEnvironmentVariable("WAYLAND_DISPLAY"));
	QString first;
	if (f.open(QIODevice::ReadOnly)) {
		for (const QByteArray &raw : f.readAll().split('\n')) {
			const QStringList w = QString::fromUtf8(raw).split(' ');
			if (w.value(0) != "output" || w.size() < 2) {
				continue;
			}
			if (first.isEmpty()) {
				first = w[1];
			}
			if (w.size() >= 2 && w.last() == "1" && w.value(w.size() - 2) == "main") {
				return w[1];
			}
		}
	}
	return first;
}

#ifndef ZACOS9_SCREENSHARE_TEST
static int conversation(int n, const struct pam_message **messages, struct pam_response **responses,
		void *data) {
	auto *r = static_cast<pam_response *>(calloc(static_cast<size_t>(n), sizeof(pam_response)));
	if (!r) {
		return PAM_BUF_ERR;
	}
	for (int i = 0; i < n; i++) {
		if (messages[i]->msg_style == PAM_PROMPT_ECHO_OFF || messages[i]->msg_style == PAM_PROMPT_ECHO_ON) {
			r[i].resp = strdup(static_cast<const char *>(data));
			if (!r[i].resp) {
				for (int j = 0; j < i; j++) {
					free(r[j].resp);
				}
				free(r);
				return PAM_BUF_ERR;
			}
		}
	}
	*responses = r;
	return PAM_SUCCESS;
}
#endif

/* The owner's password, as the lock screen checks it (pam_unix can check
 * only our own without root). Slow on a wrong one: run it off the main
 * thread. */
static bool passwordRight(const QByteArray &user, const QByteArray &password) {
#ifdef ZACOS9_SCREENSHARE_TEST
	return user == ownerName().toLocal8Bit() && password == "sesame";
#else
	if (user != ownerName().toLocal8Bit()) {
		return false;
	}
	const char *service = QFile::exists("/etc/pam.d/zacos9-screenshare") ||
		QFile::exists("/usr/lib/pam.d/zacos9-screenshare") ? "zacos9-screenshare" : "login";
	QByteArray secret = password; /* PAM's conversation wants it writable */
	struct pam_conv conv = { conversation, secret.data() };
	pam_handle_t *pam = nullptr;
	bool ok = false;
	if (pam_start(service, user.constData(), &conv, &pam) == PAM_SUCCESS) {
		const int result = pam_authenticate(pam, 0);
		pam_end(pam, result);
		ok = result == PAM_SUCCESS;
	}
	return ok;
#endif
}

/* An IPv4 peer as a.b.c.d, not ::ffff:a.b.c.d. */
static QString peerAddress(const QAbstractSocket *s) {
	const QHostAddress peer = s->peerAddress();
	bool v4 = false;
	const quint32 ip4 = peer.toIPv4Address(&v4);
	return v4 ? QHostAddress(ip4).toString() : peer.toString();
}

/* The other computer's name over mDNS, else empty. */
static QString peerName(const QString &address) {
	QProcess p;
	p.start("avahi-resolve", { "-a", address });
	if (!waitForProcess(&p, 2000)) {
		return QString();
	}
	QString name = QString::fromUtf8(p.readAllStandardOutput()).trimmed().section('\t', 1);
	if (name.endsWith(".local")) {
		name.chop(6);
	}
	return name;
}

struct Connection {
	int id = 0;
	QTcpSocket *tcp = nullptr;
	QLocalSocket *vnc = nullptr;
	QString address, name;
	qint64 since = 0;
};

/* A viewer listening to this computer's sound. */
struct Listener {
	QSslSocket *tls = nullptr;
	QProcess *rec = nullptr; /* parec, once the password is right */
	QString address;
	bool checking = false;
};

class Host : public QObject {
public:
	Host() {
		connect(&m_sound, &QSslServer::pendingConnectionAvailable, this, [this] { acceptListener(); });
	}

	bool start(QString *error) {
		m_settings = readSettings();
		m_name = computerName();
		if (!ensureCertificate()) {
			*error = "The certificate couldn’t be made (is openssl installed?).";
			return false;
		}
		if (!startWayvnc(error)) {
			return false;
		}
		const quint16 port = qEnvironmentVariableIsSet("ZACOS9_SCREENSHARE_PORT")
			? static_cast<quint16>(qEnvironmentVariableIntValue("ZACOS9_SCREENSHARE_PORT")) : PORT;
		connect(&m_tcp, &QTcpServer::newConnection, this, [this] { accept(); });
		if (!m_tcp.listen(QHostAddress::Any, port)) {
			*error = QString("Port %1 is in use: %2").arg(port).arg(m_tcp.errorString());
			return false;
		}
		publish();
		if (m_settings.sound && !startSound(error)) {
			return false;
		}
		return true;
	}

	void stop() {
		stopSound();
		for (auto it = m_conns.begin(); it != m_conns.end();) {
			Connection c = (it++)->second;
			drop(c.id);
		}
		m_tcp.close();
		stopProcess(&m_avahi);
		stopProcess(&m_wayvnc);
	}

	QStringList status() const {
		QStringList out{ "on" };
		for (const auto &[id, c] : m_conns) {
			out << QString("conn\t%1\t%2\t%3\t%4").arg(id).arg(c.address, c.name).arg(c.since);
		}
		return out;
	}

	QString disconnectClient(int id) {
		if (!m_conns.count(id)) {
			return "That computer is no longer connected.";
		}
		drop(id);
		return "ok";
	}

	/* The panel changed the settings or the computer's name. */
	bool reload(QString *error) {
		const Settings old = m_settings;
		m_settings = readSettings();
		if (m_settings.sound != old.sound) {
			stopSound();
			if (m_settings.sound && !startSound(error)) {
				return false;
			}
		}
		if (m_settings.control != old.control) {
			/* wayvnc takes -d only at start: everyone is disconnected. */
			for (auto it = m_conns.begin(); it != m_conns.end();) {
				Connection c = (it++)->second;
				drop(c.id);
			}
			stopProcess(&m_wayvnc);
			if (!startWayvnc(error)) {
				return false;
			}
		}
		const QString name = computerName();
		if (name != m_name) {
			m_name = name;
			stopProcess(&m_avahi);
			publish();
		}
		return true;
	}

private:
	bool startWayvnc(QString *error) {
		const QString dir = runtimeDir();
		const QString conf = dir + "/wayvnc.conf", vnc = dir + "/vnc.sock", ctl = dir + "/ctl.sock";
		QFile::remove(vnc);
		{
			QSaveFile f(conf);
			if (!f.open(QIODevice::WriteOnly)) {
				*error = "wayvnc’s settings couldn’t be written.";
				return false;
			}
			f.write(QString("enable_auth=true\nenable_pam=true\nprivate_key_file=%1\ncertificate_file=%2\n")
				.arg(keyFile(), certFile()).toUtf8());
			f.commit();
		}
		QStringList args{ "-C", conf, "-u", "-S", ctl };
		if (!m_settings.control) {
			args << "-d";
		}
		const QString output = mainOutput();
		if (!output.isEmpty()) {
			args << "-o" << output;
		}
		args << vnc;
		const QString program = qEnvironmentVariable("ZACOS9_WAYVNC", "wayvnc");
		m_wayvnc.setProcessChannelMode(QProcess::ForwardedChannels);
		dieWithParent(&m_wayvnc);
		m_stopping = false;
		QObject::disconnect(&m_wayvnc, &QProcess::finished, this, nullptr);
		m_wayvnc.start(program, args);
		waitFor([&] { return QFile::exists(vnc) || m_wayvnc.state() == QProcess::NotRunning; }, 8000);
		if (!QFile::exists(vnc)) {
			*error = m_wayvnc.error() == QProcess::FailedToStart
				? QString("wayvnc isn’t installed.")
				: QString("wayvnc didn’t start (see the session log).");
			stopProcess(&m_wayvnc);
			return false;
		}
		/* wayvnc gone (the screen went away, the session is ending): so
		 * is sharing. */
		connect(&m_wayvnc, &QProcess::finished, this, [this] {
			if (!m_stopping) {
				fprintf(stderr, "zacos9-screenshare-host: wayvnc stopped\n");
				QCoreApplication::exit(1);
			}
		});
		return true;
	}

	void publish() {
		const QString program = qEnvironmentVariable("ZACOS9_AVAHI_PUBLISH", "avahi-publish");
		if (program == "none") {
			return;
		}
		dieWithParent(&m_avahi);
		m_avahi.setProcessChannelMode(QProcess::ForwardedErrorChannel);
		m_avahi.setStandardOutputFile(QProcess::nullDevice());
		m_avahi.start(program, { "-s", m_name, "_rfb._tcp", QString::number(m_tcp.serverPort()), "zacos=1" });
	}

	void stopProcess(QProcess *p) {
		if (p == &m_wayvnc) {
			m_stopping = true;
		}
		if (p->state() != QProcess::NotRunning) {
			p->terminate();
			waitFor([p] { return p->state() == QProcess::NotRunning; }, 3000);
			if (p->state() != QProcess::NotRunning) {
				p->kill();
				waitFor([p] { return p->state() == QProcess::NotRunning; }, 1000);
			}
		}
	}

	/* ---- the sound ---- */

	bool startSound(QString *error) {
		QFile certPem(certFile()), keyPem(keyFile());
		if (!certPem.open(QIODevice::ReadOnly) || !keyPem.open(QIODevice::ReadOnly)) {
			*error = "The certificate couldn’t be read.";
			return false;
		}
		QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
		tls.setLocalCertificate(QSslCertificate(&certPem, QSsl::Pem));
		tls.setPrivateKey(QSslKey(&keyPem, QSsl::Ec, QSsl::Pem));
		tls.setPeerVerifyMode(QSslSocket::VerifyNone);
		m_sound.setSslConfiguration(tls);
		m_sound.setHandshakeTimeout(SOUND_HELLO_TIMEOUT_MS);
		const quint16 port = static_cast<quint16>(m_tcp.serverPort() + AUDIO_PORT_OFFSET);
		if (!m_sound.listen(QHostAddress::Any, port)) {
			*error = QString("Port %1 (for the sound) is in use: %2").arg(port).arg(m_sound.errorString());
			return false;
		}
		return true;
	}

	void stopSound() {
		/* A password being checked: its thread mustn't outlive us. */
		waitFor([this] { return m_checks == 0; }, 10000);
		while (!m_listeners.empty()) {
			dropListener(m_listeners.begin()->first);
		}
		m_sound.close();
	}

	void acceptListener() {
		while (QTcpSocket *s = m_sound.nextPendingConnection()) {
			auto *tls = static_cast<QSslSocket *>(s);
			tls->setParent(this);
			Listener l;
			l.tls = tls;
			l.address = peerAddress(tls);
			m_listeners[tls] = l;
			connect(tls, &QSslSocket::readyRead, this, [this, tls] { listenerHello(tls); });
			connect(tls, &QSslSocket::disconnected, this, [this, tls] { dropListener(tls); });
			connect(tls, &QSslSocket::errorOccurred, this, [this, tls] { dropListener(tls); });
			connect(tls, &QSslSocket::bytesWritten, this, [this, tls] { pumpSound(tls); });
			/* Said nothing in time: gone. */
			QTimer::singleShot(SOUND_HELLO_TIMEOUT_MS, tls, [this, tls] {
				auto it = m_listeners.find(tls);
				if (it != m_listeners.end() && !it->second.rec && !it->second.checking) {
					dropListener(tls);
				}
			});
			listenerHello(tls);
		}
	}

	bool viewing(const QString &address) const {
		for (const auto &[id, c] : m_conns) {
			if (c.address == address) {
				return true;
			}
		}
		return false;
	}

	void refuseListener(QSslSocket *tls, const QString &why) {
		tls->write(why.toUtf8() + '\n');
		tls->disconnectFromHost();
	}

	/* "ZACOS-SOUND 1", the name, the password: three lines. */
	void listenerHello(QSslSocket *tls) {
		auto it = m_listeners.find(tls);
		if (it == m_listeners.end() || it->second.rec || it->second.checking) {
			return;
		}
		Listener &l = it->second;
		if (tls->bytesAvailable() > 1024) {
			dropListener(tls);
			return;
		}
		const QByteArray peek = tls->peek(1024);
		if (peek.count('\n') < 3) {
			return;
		}
		QByteArray hello = tls->readLine().trimmed();
		const QByteArray user = tls->readLine().chopped(1);
		const QByteArray password = tls->readLine().chopped(1);
		if (hello != "ZACOS-SOUND 1") {
			refuseListener(tls, "Not a Screen Sharing sound request.");
			return;
		}
		if (!viewing(l.address)) {
			refuseListener(tls, "That computer isn’t viewing this screen.");
			return;
		}
		l.checking = true;
		m_checks++;
		QThread *check = QThread::create([this, tls, user, password] {
			const bool ok = passwordRight(user, password);
			QMetaObject::invokeMethod(this, [this, tls, ok] {
				m_checks--;
				passwordChecked(tls, ok);
			}, Qt::QueuedConnection);
		});
		connect(check, &QThread::finished, check, &QObject::deleteLater);
		check->start();
	}

	void passwordChecked(QSslSocket *tls, bool ok) {
		auto it = m_listeners.find(tls);
		if (it == m_listeners.end()) {
			return; /* went meanwhile */
		}
		Listener &l = it->second;
		l.checking = false;
		if (!ok) {
			refuseListener(tls, "The name or password isn’t right.");
			return;
		}
		if (!viewing(l.address)) {
			refuseListener(tls, "That computer isn’t viewing this screen.");
			return;
		}
		tls->write(QString("ok %1 %2\n").arg(AUDIO_RATE).arg(AUDIO_CHANNELS).toUtf8());
		tls->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		l.rec = new QProcess(this);
		dieWithParent(l.rec);
		l.rec->setProcessChannelMode(QProcess::ForwardedErrorChannel);
		connect(l.rec, &QProcess::readyReadStandardOutput, this, [this, tls] { pumpSound(tls); });
		connect(l.rec, &QProcess::finished, this, [this, tls] { dropListener(tls); });
		connect(l.rec, &QProcess::errorOccurred, this, [this, tls](QProcess::ProcessError e) {
			if (e == QProcess::FailedToStart) {
				dropListener(tls);
			}
		});
		l.rec->start(qEnvironmentVariable("ZACOS9_PAREC", "parec"), { "--raw", "--format=s16le",
			QString("--rate=%1").arg(AUDIO_RATE), QString("--channels=%1").arg(AUDIO_CHANNELS),
			"--latency-msec=20", "--client-name=Screen Sharing", "--device=@DEFAULT_MONITOR@" });
	}

	/* What parec has, unless the network is behind: then it's dropped,
	 * whole frames at a time. */
	void pumpSound(QSslSocket *tls) {
		auto it = m_listeners.find(tls);
		if (it == m_listeners.end() || !it->second.rec) {
			return;
		}
		QProcess *rec = it->second.rec;
		const int frame = AUDIO_CHANNELS * 2;
		const qint64 whole = rec->bytesAvailable() / frame * frame;
		if (whole == 0) {
			return;
		}
		const QByteArray samples = rec->read(whole);
		if (tls->bytesToWrite() < SOUND_QUEUE_LIMIT) {
			tls->write(samples);
		}
	}

	void dropListener(QSslSocket *tls) {
		auto it = m_listeners.find(tls);
		if (it == m_listeners.end()) {
			return;
		}
		Listener l = it->second;
		m_listeners.erase(it);
		tls->disconnect(this);
		if (l.rec) {
			l.rec->disconnect(this);
			l.rec->kill();
			connect(l.rec, &QProcess::finished, l.rec, &QObject::deleteLater);
			if (l.rec->state() == QProcess::NotRunning) {
				l.rec->deleteLater();
			}
		}
		tls->disconnectFromHost();
		tls->deleteLater();
	}

	/* The last screen connection from there gone: its sound goes too. */
	void dropListenersOf(const QString &address) {
		if (viewing(address)) {
			return;
		}
		std::vector<QSslSocket *> gone;
		for (const auto &[tls, l] : m_listeners) {
			if (l.address == address) {
				gone.push_back(tls);
			}
		}
		for (QSslSocket *tls : gone) {
			dropListener(tls);
		}
	}

	/* ---- the screen ---- */

	void accept() {
		while (QTcpSocket *tcp = m_tcp.nextPendingConnection()) {
			/* One question at a time: others are turned away meanwhile. */
			if (m_asking) {
				tcp->abort();
				tcp->deleteLater();
				continue;
			}
			const QString address = peerAddress(tcp);
			if (m_settings.ask) {
				m_asking = true;
				const QString name = peerName(address);
				const bool allowed = ask(address, name, tcp);
				m_asking = false;
				if (!allowed || tcp->state() != QAbstractSocket::ConnectedState) {
					tcp->abort();
					tcp->deleteLater();
					continue;
				}
				splice(tcp, address, name);
			} else {
				splice(tcp, address, QString());
			}
		}
	}

	bool ask(const QString &address, const QString &name, QTcpSocket *tcp) {
		const QString who = name.isEmpty() ? QString("A computer at %1").arg(address)
		                                   : QString("“%1” (%2)").arg(name, address);
		Alert alert(QString("%1 wants to %2 your screen.").arg(who,
				m_settings.control ? "see and control" : "see"),
			"Allow", "Deny");
		QTimer::singleShot(ASK_TIMEOUT_MS, &alert, [&alert] { alert.reject(); });
		/* Gave up waiting: no need to keep asking. */
		connect(tcp, &QTcpSocket::disconnected, &alert, [&alert] { alert.reject(); });
		return alert.exec() == QDialog::Accepted;
	}

	void splice(QTcpSocket *tcp, const QString &address, const QString &name) {
		Connection c;
		c.id = ++m_nextId;
		c.tcp = tcp;
		c.vnc = new QLocalSocket(this);
		c.address = address;
		c.name = name;
		c.since = QDateTime::currentSecsSinceEpoch();
		tcp->setParent(this);
		tcp->setReadBufferSize(1 << 20);
		tcp->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		c.vnc->setReadBufferSize(1 << 20);
		const int id = c.id;
		auto pump = [](QIODevice *from, QIODevice *to) {
			if (!to->isOpen()) {
				return; /* wayvnc's socket not yet connected */
			}
			while (from->bytesAvailable() > 0 && to->bytesToWrite() < QUEUE_LIMIT) {
				to->write(from->read(256 << 10));
			}
		};
		connect(tcp, &QTcpSocket::readyRead, c.vnc, [=] { pump(tcp, c.vnc); });
		connect(c.vnc, &QLocalSocket::bytesWritten, tcp, [=] { pump(tcp, c.vnc); });
		connect(c.vnc, &QLocalSocket::readyRead, tcp, [=] { pump(c.vnc, tcp); });
		connect(tcp, &QTcpSocket::bytesWritten, c.vnc, [=] { pump(c.vnc, tcp); });
		connect(tcp, &QTcpSocket::disconnected, this, [this, id] { drop(id); });
		connect(c.vnc, &QLocalSocket::disconnected, this, [this, id] { drop(id); });
		connect(c.vnc, &QLocalSocket::errorOccurred, this, [this, id] { drop(id); });
		m_conns[id] = c;
		c.vnc->connectToServer(runtimeDir() + "/vnc.sock");
		/* Bytes the viewer sent while we were asking. */
		connect(c.vnc, &QLocalSocket::connected, tcp, [=] { pump(tcp, c.vnc); });
	}

	void drop(int id) {
		auto it = m_conns.find(id);
		if (it == m_conns.end()) {
			return;
		}
		Connection c = it->second;
		m_conns.erase(it);
		c.tcp->disconnect(this);
		c.vnc->disconnect(this);
		/* Let what's queued go first: the server's last words. */
		c.tcp->disconnectFromHost();
		c.vnc->disconnectFromServer();
		c.tcp->deleteLater();
		c.vnc->deleteLater();
		dropListenersOf(c.address);
	}

	Settings m_settings;
	QString m_name;
	QTcpServer m_tcp;
	QSslServer m_sound;
	std::map<QSslSocket *, Listener> m_listeners;
	int m_checks = 0; /* passwords being checked */
	QProcess m_wayvnc, m_avahi;
	bool m_stopping = false, m_asking = false;
	std::map<int, Connection> m_conns;
	int m_nextId = 0;
};

static Host *host;

/* SIGTERM and SIGINT stop sharing as "quit" does (wayvnc and
 * avahi-publish go too), by way of a pipe: a signal handler may only
 * write(). */
static int signalPipe[2];

static void quitOnSignal(int) {
	const char c = 0;
	(void)!write(signalPipe[1], &c, 1);
}

int main(int argc, char *argv[]) {
	qputenv("QT_WAYLAND_DISABLE_WINDOWDECORATION", "1");
	QApplication app(argc, argv);
	QApplication::setApplicationName("Screen Sharing");
	QApplication::setQuitOnLastWindowClosed(false);
	QGuiApplication::setDesktopFileName("zacos9-screenshare-host");
	platinumShellInit();

	const QString dir = runtimeDir();
	QDir().mkpath(dir);
	chmod(dir.toLocal8Bit().constData(), 0700);
	if (hostRequest("status", nullptr, 1000)) {
		return 0; /* already sharing */
	}

	Host h;
	host = &h;
	QString error;
	if (!h.start(&error)) {
		fprintf(stderr, "zacos9-screenshare-host: %s\n", qPrintable(error));
		QFile err(dir + "/error");
		if (err.open(QIODevice::WriteOnly)) {
			err.write(error.toUtf8() + '\n');
		}
		h.stop();
		return 1;
	}
	QFile::remove(dir + "/error");

	QLocalServer control;
	QLocalServer::removeServer(hostSocket());
	control.setSocketOptions(QLocalServer::UserAccessOption);
	if (!control.listen(hostSocket())) {
		fprintf(stderr, "zacos9-screenshare-host: %s\n", qPrintable(control.errorString()));
		h.stop();
		return 1;
	}
	QObject::connect(&control, &QLocalServer::newConnection, [&control] {
		while (QLocalSocket *s = control.nextPendingConnection()) {
			QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
			QObject::connect(s, &QLocalSocket::readyRead, s, [s] {
				if (!s->canReadLine()) {
					return;
				}
				const QString line = QString::fromUtf8(s->readLine()).trimmed();
				const QString cmd = line.section(' ', 0, 0);
				QStringList reply;
				bool quit = false;
				if (cmd == "status") {
					reply = host->status();
				} else if (cmd == "disconnect") {
					reply << host->disconnectClient(line.section(' ', 1).toInt());
				} else if (cmd == "reload") {
					QString error;
					const bool ok = host->reload(&error);
					reply << (ok ? QString("ok") : error);
					quit = !ok; /* wayvnc couldn't come back */
				} else if (cmd == "quit") {
					reply << "ok";
					quit = true;
				} else {
					reply << "Unknown command.";
				}
				s->write(reply.join('\n').toUtf8() + '\n');
				s->disconnectFromServer();
				if (quit) {
					QCoreApplication::quit();
				}
			});
		}
	});
	if (pipe(signalPipe) == 0) {
		auto *notifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, &app);
		QObject::connect(notifier, &QSocketNotifier::activated, [] { QCoreApplication::quit(); });
		std::signal(SIGTERM, quitOnSignal);
		std::signal(SIGINT, quitOnSignal);
	}

	const int code = app.exec();
	h.stop();
	control.close();
	return code;
}
