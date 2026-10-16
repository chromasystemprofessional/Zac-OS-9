#include "discovery.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QUdpSocket>
#include <QUuid>
#include <algorithm>
#include <array>
#include <cctype>

/* Avahi escapes any byte that isn't a plain label character as a
 * three-decimal-digit "\DDD" (its value), and a literal backslash or
 * dot as "\\" / "\.". The bytes are UTF-8: "Adam’s" comes as
 * "Adam\226\128\153s", one escape per byte of the apostrophe. */
static QString unescapeAvahi(const QString &s) {
	const QByteArray in = s.toUtf8();
	QByteArray out;
	out.reserve(in.size());
	for (int i = 0; i < in.size(); i++) {
		if (in[i] != '\\' || i + 1 >= in.size()) {
			out += in[i];
			continue;
		}
		const char next = in[i + 1];
		if (next == '\\' || next == '.') {
			out += next;
			i++;
		} else if (isdigit((unsigned char)next) && i + 3 < in.size() &&
				isdigit((unsigned char)in[i + 2]) && isdigit((unsigned char)in[i + 3])) {
			out += static_cast<char>(in.mid(i + 1, 3).toInt());
			i += 3;
		} else {
			out += in[i];
		}
	}
	return QString::fromUtf8(out);
}

bool parseAvahiBrowseLine(const QString &line, DiscoveredServer *out) {
	if (!line.startsWith("=;")) {
		return false; /* "+": found, not yet resolved; nothing to connect to yet */
	}
	const QStringList f = line.split(';');
	/* =;iface;proto;name;type;domain;host;address;port;txt... */
	if (f.size() < 9) {
		return false;
	}
	const QString &proto = f[2], &type = f[4], &host = f[6], &port = f[8];
	/* IPv4: its address. IPv6: its host name, which the system resolves
	 * (mdns4_minimal) - a bare IPv6 address would need brackets, and a
	 * link-local one its interface too. parseAvahiBrowse prefers IPv4. */
	const bool v4 = proto == "IPv4";
	const QString address = v4 ? f[7] : host;
	if ((!v4 && proto != "IPv6") || address.isEmpty()) {
		return false;
	}
	if (type == "_afpovertcp._tcp") {
		out->kind = DiscoveredServer::AFP;
	} else if (type == "_smb._tcp") {
		out->kind = DiscoveredServer::SMB;
	} else {
		return false;
	}
	out->name = unescapeAvahi(f[3]);
	out->byHostName = !v4;
	const QString defaultPort = out->kind == DiscoveredServer::AFP ? "548" : "445";
	out->address = port == defaultPort ? address : address + ":" + port;
	return true;
}

std::vector<DiscoveredServer> bonjourServers(int timeoutMs) {
	QProcess afp, smb;
	std::array<QProcess *, 2> processes{ &afp, &smb };
	const std::array<QString, 2> serviceTypes{
		"_afpovertcp._tcp", "_smb._tcp"
	};
	/* Query just the two service types we use, in parallel. A broad -a
	 * browse may resolve many unrelated records and take longer than the
	 * Network Browser's scan budget; it then exits before AFP's resolved
	 * "=" records arrive, leaving only unusable "+" records. */
	for (size_t i = 0; i < processes.size(); i++) {
		processes[i]->start("avahi-browse", {
			"-k", "-p", "-r", "-t", serviceTypes[i]
		});
	}
	QElapsedTimer clock;
	clock.start();
	auto running = [&] {
		return std::any_of(processes.begin(), processes.end(), [](QProcess *p) {
			return p->state() != QProcess::NotRunning;
		});
	};
	while (running() && clock.elapsed() < timeoutMs) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
			QEventLoop::WaitForMoreEvents, 50);
	}
	if (running()) {
		for (QProcess *p : processes) {
			if (p->state() != QProcess::NotRunning) {
				p->kill();
			}
		}
		clock.restart();
		while (running() && clock.elapsed() < 1000) {
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
				QEventLoop::WaitForMoreEvents, 50);
		}
	}
	QString output;
	for (QProcess *p : processes) {
		output += QString::fromUtf8(p->readAllStandardOutput());
	}
	return parseAvahiBrowse(output);
}

std::vector<DiscoveredServer> parseAvahiBrowse(const QString &text) {
	std::vector<DiscoveredServer> found;
	QSet<QString> withIPv4;
	for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
		DiscoveredServer s;
		if (parseAvahiBrowseLine(line, &s)) {
			found.push_back(s);
			if (!s.byHostName) {
				withIPv4.insert(QString::number(s.kind) + "/" + s.name);
			}
		}
	}
	std::vector<DiscoveredServer> out;
	for (const DiscoveredServer &s : found) {
		if (!s.byHostName || !withIPv4.contains(QString::number(s.kind) + "/" + s.name)) {
			out.push_back(s);
		}
	}
	return out;
}

/* WS-Discovery: https://schemas.xmlsoap.org/ws/2005/04/discovery.
 * Windows answers a multicast Probe with a unicast ProbeMatch straight
 * back to the sender; nothing here needs to join a multicast group. */
static QByteArray wsDiscoveryProbe() {
	const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
	return QStringLiteral(
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" "
		"xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" "
		"xmlns:wsd=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
		"<soap:Header>"
		"<wsa:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</wsa:To>"
		"<wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</wsa:Action>"
		"<wsa:MessageID>urn:uuid:%1</wsa:MessageID>"
		"</soap:Header>"
		/* Computers only: an empty Probe has printers and scanners
		 * answer too (seen live: two Brother printers, listed by
		 * address as if they were Windows computers). */
		"<soap:Body><wsd:Probe><wsd:Types "
		"xmlns:pub=\"http://schemas.microsoft.com/windows/pub/2005/07\">pub:Computer"
		"</wsd:Types></wsd:Probe></soap:Body>"
		"</soap:Envelope>")
		.arg(id)
		.toUtf8();
}

bool wsDiscoveryIsComputer(const QString &xml) {
	/* <wsd:Types>wsdp:Device pub:Computer</wsd:Types>; a device that
	 * ignores the Probe's Types still names its own here. */
	static const QRegularExpression types("<[\\w:]*Types>([^<]*)</[\\w:]*Types>");
	const auto m = types.match(xml);
	if (!m.hasMatch()) {
		return false;
	}
	for (const QString &t : m.captured(1).split(' ', Qt::SkipEmptyParts)) {
		if (t.section(':', -1) == "Computer") {
			return true;
		}
	}
	return false;
}

QString wsDiscoveryComputerName(const QString &xml) {
	/* Microsoft's WS-Discovery extension (what wsdd/wsdd2 speak) names
	 * the machine in a pub:Computer scope or a wsdp:ComputerName
	 * element, depending on the responder; either is a plain string
	 * with no nested markup, so a light regex is enough — this is a
	 * discovery hint for a list, not something acted on unparsed. */
	QRegularExpression computerName("<[\\w:]*ComputerName>([^<]+)</[\\w:]*ComputerName>");
	auto m = computerName.match(xml);
	if (m.hasMatch()) {
		return m.captured(1).trimmed();
	}
	return QString();
}

std::vector<DiscoveredServer> wsDiscoveryServers(int timeoutMs) {
	std::vector<DiscoveredServer> out;
	QUdpSocket socket;
	if (!socket.bind(QHostAddress::AnyIPv4, 0)) {
		return out;
	}
	socket.writeDatagram(wsDiscoveryProbe(), QHostAddress("239.255.255.250"), 3702);

	QSet<QString> seen;
	QElapsedTimer clock;
	clock.start();
	while (clock.elapsed() < timeoutMs) {
		if (!socket.waitForReadyRead(std::max(1, timeoutMs - static_cast<int>(clock.elapsed())))) {
			continue;
		}
		while (socket.hasPendingDatagrams()) {
			QByteArray buf(static_cast<int>(socket.pendingDatagramSize()), Qt::Uninitialized);
			QHostAddress sender;
			const qint64 n = socket.readDatagram(buf.data(), buf.size(), &sender);
			if (n < 0) {
				continue;
			}
			const QString addr = sender.toString();
			if (seen.contains(addr)) {
				continue;
			}
			seen.insert(addr);
			const QString xml = QString::fromUtf8(buf);
			if (!xml.contains("ProbeMatch") || !wsDiscoveryIsComputer(xml)) {
				continue; /* other WS-Discovery chatter, or not a computer */
			}
			DiscoveredServer s;
			s.kind = DiscoveredServer::SMB;
			s.address = addr;
			const QString name = wsDiscoveryComputerName(xml);
			s.name = name.isEmpty() ? addr : name;
			out.push_back(s);
		}
	}
	return out;
}

/* "host:port" -> "host". IPv4 addresses and .local names only here,
 * never a bare IPv6 address (see parseAvahiBrowseLine). */
static QString hostOf(const QString &address) {
	return address.section(':', 0, 0);
}

std::vector<DiscoveredServer> mergeServers(const std::vector<DiscoveredServer> &bonjour,
		const std::vector<DiscoveredServer> &windows) {
	/* Bonjour's names by address, and the computers sharing over AFP. */
	QHash<QString, QString> names;
	QSet<QString> afpHosts;
	for (const DiscoveredServer &s : bonjour) {
		if (!s.byHostName) {
			names.insert(hostOf(s.address), s.name);
		}
		if (s.kind == DiscoveredServer::AFP) {
			afpHosts.insert(hostOf(s.address));
		}
	}
	std::vector<DiscoveredServer> out;
	for (const DiscoveredServer &s : bonjour) {
		if (s.kind == DiscoveredServer::SMB && afpHosts.contains(hostOf(s.address))) {
			continue;
		}
		out.push_back(s);
	}
	/* A computer already found announcing _smb._tcp over Bonjour (a
	 * modern Mac, or ourselves) needn't also be probed for over
	 * WS-Discovery; only the addresses differ in practice, but comparing
	 * by address keeps a dual-stack host from showing up twice. */
	QSet<QString> smbAddresses;
	for (const DiscoveredServer &s : bonjour) {
		if (s.kind == DiscoveredServer::SMB) {
			smbAddresses.insert(s.address);
		}
	}
	for (DiscoveredServer s : windows) {
		if (smbAddresses.contains(s.address) || afpHosts.contains(hostOf(s.address))) {
			continue;
		}
		if (s.name == s.address && names.contains(hostOf(s.address))) {
			s.name = names.value(hostOf(s.address));
		}
		out.push_back(s);
	}
	std::sort(out.begin(), out.end(), [](const DiscoveredServer &a, const DiscoveredServer &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	/* The same server, announced on more than one interface (eth0 and
	 * lo both carried this project's own Netatalk test server live,
	 * each with its own address) or answering a WS-Discovery probe
	 * more than once: shown once, by name — a duplicate entry for the
	 * same computer is confusing, and the first address found connects
	 * to the same place as any other. */
	std::vector<DiscoveredServer> unique;
	QSet<QString> seenNames;
	for (const DiscoveredServer &s : out) {
		const QString key = QString::number(s.kind) + "/" + s.name.toCaseFolded();
		if (seenNames.contains(key)) {
			continue;
		}
		seenNames.insert(key);
		unique.push_back(s);
	}
	return unique;
}

QHash<QString, QString> parseAvahiResolve(const QString &text) {
	QHash<QString, QString> hosts;
	for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
		const QString address = line.section('\t', 0, 0).trimmed();
		QString host = unescapeAvahi(line.section('\t', 1).trimmed());
		if (host.endsWith(".local")) {
			host.chop(6);
		}
		if (!address.isEmpty() && !host.isEmpty()) {
			hosts.insert(address, host);
		}
	}
	return hosts;
}

std::vector<DiscoveredServer> discoverServers(int timeoutMs) {
	std::vector<DiscoveredServer> servers = mergeServers(bonjourServers(timeoutMs),
		wsDiscoveryServers(timeoutMs));
	QStringList nameless;
	for (const DiscoveredServer &s : servers) {
		if (s.name == s.address) {
			nameless << s.address;
		}
	}
	if (nameless.isEmpty()) {
		return servers;
	}
	/* One query for all of them, a second at most: those that don't
	 * answer keep their address. */
	QProcess resolve;
	resolve.start("avahi-resolve", QStringList{ "-a" } + nameless);
	QElapsedTimer clock;
	clock.start();
	while (resolve.state() != QProcess::NotRunning && clock.elapsed() < 1000) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
			QEventLoop::WaitForMoreEvents, 50);
	}
	if (resolve.state() != QProcess::NotRunning) {
		resolve.kill();
		clock.restart();
		while (resolve.state() != QProcess::NotRunning && clock.elapsed() < 1000) {
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents |
				QEventLoop::WaitForMoreEvents, 50);
		}
	}
	const QHash<QString, QString> hosts = parseAvahiResolve(QString::fromUtf8(resolve.readAllStandardOutput()));
	for (DiscoveredServer &s : servers) {
		if (s.name == s.address && hosts.contains(s.address)) {
			s.name = hosts.value(s.address);
		}
	}
	std::sort(servers.begin(), servers.end(), [](const DiscoveredServer &a, const DiscoveredServer &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return servers;
}
