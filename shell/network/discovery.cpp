#include "discovery.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QUdpSocket>
#include <QUuid>
#include <algorithm>

/* Avahi escapes any byte that isn't a plain label character as a
 * three-decimal-digit "\DDD" (its value), and a literal backslash or
 * dot as "\\" / "\.". */
static QString unescapeAvahi(const QString &s) {
	QString out;
	out.reserve(s.size());
	for (int i = 0; i < s.size(); i++) {
		if (s[i] != '\\' || i + 1 >= s.size()) {
			out += s[i];
			continue;
		}
		const QChar next = s[i + 1];
		if (next == '\\' || next == '.') {
			out += next;
			i++;
		} else if (next.isDigit() && i + 3 < s.size() &&
				s[i + 2].isDigit() && s[i + 3].isDigit()) {
			out += QChar(s.mid(i + 1, 3).toInt());
			i += 3;
		} else {
			out += s[i];
		}
	}
	return out;
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
	const QString &proto = f[2], &type = f[4], &address = f[7], &port = f[8];
	if (proto != "IPv4" || address.isEmpty()) {
		return false; /* one of each per family; IPv4 alone is enough to connect */
	}
	if (type == "_afpovertcp._tcp") {
		out->kind = DiscoveredServer::AFP;
	} else if (type == "_smb._tcp") {
		out->kind = DiscoveredServer::SMB;
	} else {
		return false;
	}
	out->name = unescapeAvahi(f[3]);
	const QString defaultPort = out->kind == DiscoveredServer::AFP ? "548" : "445";
	out->address = port == defaultPort ? address : address + ":" + port;
	return true;
}

std::vector<DiscoveredServer> bonjourServers(int timeoutMs) {
	std::vector<DiscoveredServer> out;
	QProcess p;
	/* -a: every service type, not just one — avahi-browse takes at
	 * most a single type argument (tested live: given two, it refuses
	 * with "Too many arguments" and nothing is browsed at all), so
	 * asking for both AFP and SMB in one pass means asking for
	 * everything and filtering client-side, which parseAvahiBrowseLine
	 * already does. -p: parseable (';'-separated); -r: resolve; -t:
	 * list what's cached and exit, rather than following the network
	 * forever (measured live against this project's own test server:
	 * under a second, since the daemon already has it cached); -k:
	 * without it, avahi-browse still substitutes a friendly name for
	 * the service type in the TYPE field even in parseable mode (tested
	 * live: "_afpovertcp._tcp" comes back as "Apple File Sharing"), so
	 * the exact-match in parseAvahiBrowseLine needs it. */
	p.start("avahi-browse", { "-k", "-a", "-p", "-r", "-t" });
	if (!p.waitForFinished(timeoutMs) && p.state() != QProcess::NotRunning) {
		p.kill();
		p.waitForFinished(1000);
	}
	const QString text = QString::fromUtf8(p.readAllStandardOutput());
	for (const QString &line : text.split('\n', Qt::SkipEmptyParts)) {
		DiscoveredServer s;
		if (parseAvahiBrowseLine(line, &s)) {
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
		"<soap:Body><wsd:Probe/></soap:Body>"
		"</soap:Envelope>")
		.arg(id)
		.toUtf8();
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
			if (!xml.contains("ProbeMatch")) {
				continue; /* some other WS-Discovery chatter, not an answer */
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

std::vector<DiscoveredServer> discoverServers(int timeoutMs) {
	std::vector<DiscoveredServer> out = bonjourServers(timeoutMs);
	/* A computer already found announcing _smb._tcp over Bonjour (a
	 * modern Mac, or ourselves) needn't also be probed for over
	 * WS-Discovery; only the addresses differ in practice, but comparing
	 * by address keeps a dual-stack host from showing up twice. */
	QSet<QString> smbAddresses;
	for (const DiscoveredServer &s : out) {
		if (s.kind == DiscoveredServer::SMB) {
			smbAddresses.insert(s.address);
		}
	}
	for (const DiscoveredServer &s : wsDiscoveryServers(timeoutMs)) {
		if (!smbAddresses.contains(s.address)) {
			out.push_back(s);
		}
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
