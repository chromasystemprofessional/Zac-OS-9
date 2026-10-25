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
 * ZACOS9_WAYVNC and ZACOS9_AVAHI_PUBLISH ("none" for no Bonjour) stand in
 * for tests; ZACOS9_SCREENSHARE_PORT moves the port.
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
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <csignal>
#include <map>
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

class Host : public QObject {
public:
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
		return true;
	}

	void stop() {
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

	void accept() {
		while (QTcpSocket *tcp = m_tcp.nextPendingConnection()) {
			/* One question at a time: others are turned away meanwhile. */
			if (m_asking) {
				tcp->abort();
				tcp->deleteLater();
				continue;
			}
			QHostAddress peer = tcp->peerAddress();
			bool v4 = false;
			const quint32 ip4 = peer.toIPv4Address(&v4);
			const QString address = v4 ? QHostAddress(ip4).toString() : peer.toString();
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
	}

	Settings m_settings;
	QString m_name;
	QTcpServer m_tcp;
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
