/*
 * Parsing avahi-browse and WS-Discovery replies. Against recorded text,
 * not a live network: a server may or may not be announcing anything
 * when this runs, and a WS-Discovery probe needs a second computer on
 * the LAN to answer at all (see docs/network.md for what could and
 * couldn't be verified live). The lines below are real ones, captured
 * from this project's own AFP test server and copied in here, not
 * invented, so the format this depends on is the format avahi-browse
 * actually produces, not a guess at it.
 */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

#include "discovery.h"

static int fails;

static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	/* An unresolved line ("+"): found, not yet resolved — nothing to
	 * connect to yet, so it's skipped, not shown as a broken entry. */
	DiscoveredServer s;
	check(!parseAvahiBrowseLine("+;eth0;IPv4;Platinum Test;_afpovertcp._tcp;local", &s),
		"an unresolved \"+\" line is skipped");

	/* Real output (avahi-browse -k -p -r -t _afpovertcp._tcp) against
	 * this project's own Netatalk test server. */
	const QString afpLine =
		"=;eth0;IPv4;Platinum\\032Test;_afpovertcp._tcp;local;"
		"CHROMASYSTEMPRO-SERVER.local;192.168.2.110;548;";
	check(parseAvahiBrowseLine(afpLine, &s), "a resolved AFP line parses");
	check(s.kind == DiscoveredServer::AFP, "as an AFP server");
	check(s.name == "Platinum Test", "its escaped name is unescaped (\\032 is a space)");
	check(s.address == "192.168.2.110", "and the default port (548) isn't appended");

	/* The bug this project actually hit: avahi-browse substitutes a
	 * friendly name ("Apple File Sharing") for the service type in the
	 * TYPE field even with -p, unless -k is also given. Confirming this
	 * line is rejected is what proves bonjourServers() still passes -k:
	 * drop that flag and this is the one check here that starts failing. */
	const QString friendlyLine =
		"=;eth0;IPv4;Platinum\\032Test;Apple File Sharing;local;"
		"CHROMASYSTEMPRO-SERVER.local;192.168.2.110;548;";
	check(!parseAvahiBrowseLine(friendlyLine, &s),
		"a line with the friendly type name (no -k) is not mistaken for AFP");

	/* A non-default port is kept. An IPv6 resolution gives the host
	 * name; parseAvahiBrowse keeps it only for a server with no IPv4. */
	const QString smbLine = "=;eth0;IPv4;OFFICE;_smb._tcp;local;"
		"office.local;10.0.0.5;1445;";
	check(parseAvahiBrowseLine(smbLine, &s), "a resolved SMB line parses");
	check(s.kind == DiscoveredServer::SMB, "as an SMB server");
	check(s.address == "10.0.0.5:1445", "a non-default port is kept in the address");
	const QString v6Line = "=;eth0;IPv6;OFFICE;_smb._tcp;local;"
		"office.local;fe80::1;445;";
	check(parseAvahiBrowseLine(v6Line, &s) && s.address == "office.local" && s.byHostName,
		"an IPv6 resolution gives the server's host name");

	/* Seen live (2026-10-10): two ZacOS computers announced their AFP
	 * service over IPv6 alone for a while, though their .local names
	 * resolved over IPv4, and the Network Browser didn't list them. */
	const QString both =
		"=;wlp3s0;IPv4;MacBook\\032Pro;_afpovertcp._tcp;local;macbook-pro.local;192.168.2.43;548\n"
		"=;wlp3s0;IPv6;MacBook\\032Pro;_afpovertcp._tcp;local;macbook-pro.local;fd28::1;548\n"
		"=;wlp3s0;IPv6;ChromaSystem\\032Pro\\032Server;_afpovertcp._tcp;local;adams-zacos-9.local;fd28::2;548\n";
	const std::vector<DiscoveredServer> servers = parseAvahiBrowse(both);
	check(servers.size() == 2, "each server once");
	bool mac = false, chroma = false;
	for (const DiscoveredServer &d : servers) {
		mac |= d.name == "MacBook Pro" && d.address == "192.168.2.43";
		chroma |= d.name == "ChromaSystem Pro Server" && d.address == "adams-zacos-9.local";
	}
	const QString curly = "=;wlp3s0;IPv4;Adam\\226\\128\\153s\\032Computer;_afpovertcp._tcp;local;"
		"adams-zacos-10.local;192.168.2.111;548;";
	check(parseAvahiBrowseLine(curly, &s) && s.name == QString::fromUtf8("Adam\u2019s Computer"),
		"a name's UTF-8 escapes are read as UTF-8 (Adam’s, not Adamâs)");
	check(mac, "a server with IPv4 by its IPv4 address");
	check(chroma, "a server announced over IPv6 alone by its host name");

	const QString otherType = "=;eth0;IPv4;Printer;_ipp._tcp;local;p.local;10.0.0.9;631;";
	check(!parseAvahiBrowseLine(otherType, &s), "an unrelated service type is ignored");

	const QString tooFewFields = "=;eth0;IPv4;Half";
	check(!parseAvahiBrowseLine(tooFewFields, &s), "a malformed line doesn't crash, just fails");

	/* WS-Discovery ProbeMatch bodies: Microsoft's extension sometimes
	 * includes the computer's own name inline. */
	check(wsDiscoveryComputerName(
		"<wsdp:ComputerName>DESKTOP-ABC123</wsdp:ComputerName>") == "DESKTOP-ABC123",
		"a ComputerName element is read out");
	check(wsDiscoveryComputerName("<soap:Envelope><soap:Body>no name here"
		"</soap:Body></soap:Envelope>").isEmpty(),
		"no ComputerName means an empty name, not a crash");

	QTemporaryDir fakeAvahi;
	const QString fakePath = fakeAvahi.path() + "/avahi-browse";
	QFile fake(fakePath);
	const QByteArray script =
		"#!/bin/sh\n"
		"printf '%s\\n' \"$*\" >> \"$ZACOS9_TEST_AVAHI_ARGS\"\n"
		"case \"$*\" in\n"
		"  *\"_afpovertcp._tcp\") sleep 0.6; printf '%s\\n' "
		"\"=;eth0;IPv4;Delayed\\\\032AFP;_afpovertcp._tcp;local;delayed.local;192.0.2.10;548;\" ;;\n"
		"  *\"_smb._tcp\") sleep 0.6; printf '%s\\n' "
		"\"=;eth0;IPv4;Delayed\\\\032SMB;_smb._tcp;local;delayed.local;192.0.2.11;445;\" ;;\n"
		"  *) exit 2 ;;\n"
		"esac\n";
	check(fake.open(QIODevice::WriteOnly) &&
		fake.write(script) == script.size(),
		"create a delayed fake avahi-browse command");
	fake.close();
	const QFileDevice::Permissions executablePermissions =
		QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
	check(fake.setPermissions(executablePermissions),
		"make the fake avahi-browse executable");
	const QByteArray oldPath = qgetenv("PATH");
	const QByteArray oldArgsPath = qgetenv("ZACOS9_TEST_AVAHI_ARGS");
	const QString argsPath = fakeAvahi.path() + "/arguments";
	qputenv("PATH", (fakeAvahi.path().toUtf8() + ':' + oldPath));
	qputenv("ZACOS9_TEST_AVAHI_ARGS", argsPath.toUtf8());
	const std::vector<DiscoveredServer> delayed = bonjourServers(1500);
	qputenv("PATH", oldPath);
	if (oldArgsPath.isNull()) {
		qunsetenv("ZACOS9_TEST_AVAHI_ARGS");
	} else {
		qputenv("ZACOS9_TEST_AVAHI_ARGS", oldArgsPath);
	}
	check(delayed.size() == 2,
		"parallel service-specific browsing resolves AFP and SMB within one scan budget");
	bool delayedAfp = false, delayedSmb = false;
	for (const DiscoveredServer &server : delayed) {
		delayedAfp |= server.kind == DiscoveredServer::AFP &&
			server.name == "Delayed AFP" && server.address == "192.0.2.10";
		delayedSmb |= server.kind == DiscoveredServer::SMB &&
			server.name == "Delayed SMB" && server.address == "192.0.2.11";
	}
	check(delayedAfp && delayedSmb,
		"late resolved records are retained for both Bonjour service types");
	QFile argsFile(argsPath);
	QStringList recordedArgs;
	if (argsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
		recordedArgs = QString::fromUtf8(argsFile.readAll()).split('\n', Qt::SkipEmptyParts);
	}
	bool queriedAfp = false, queriedSmb = false, argsValid = recordedArgs.size() == 2;
	for (const QString &args : recordedArgs) {
		queriedAfp |= args.endsWith("_afpovertcp._tcp");
		queriedSmb |= args.endsWith("_smb._tcp");
		argsValid &= args.contains("-k") && args.contains("-p") &&
			args.contains("-r") && args.contains("-t") &&
			!(args.contains("_afpovertcp._tcp") && args.contains("_smb._tcp"));
	}
	check(argsValid && queriedAfp && queriedSmb,
		"each avahi-browse process requests one supported service type with parseable resolution");

	/* Merging: a ZacOS computer with both servers on (AFP and SMB over
	 * Bonjour, and wsdd2 answering WS-Discovery without a ComputerName,
	 * as seen live), a Windows PC that gives its name, and one that
	 * doesn't and isn't on Bonjour. */
	auto server = [](DiscoveredServer::Kind kind, const QString &name, const QString &address) {
		DiscoveredServer d;
		d.kind = kind;
		d.name = name;
		d.address = address;
		return d;
	};
	const std::vector<DiscoveredServer> merged = mergeServers(
		{ server(DiscoveredServer::AFP, "iMac 2013", "192.168.2.111"),
			server(DiscoveredServer::SMB, "iMac 2013", "192.168.2.111"),
			server(DiscoveredServer::SMB, "Laptop", "192.168.2.130") },
		{ server(DiscoveredServer::SMB, "192.168.2.111", "192.168.2.111"),
			server(DiscoveredServer::SMB, "DESKTOP-PC", "192.168.2.120"),
			server(DiscoveredServer::SMB, "192.168.2.130", "192.168.2.130"),
			server(DiscoveredServer::SMB, "192.168.2.140", "192.168.2.140") });
	QStringList shown;
	for (const DiscoveredServer &d : merged) {
		shown << QString("%1 %2 %3").arg(d.kind == DiscoveredServer::AFP ? "afp" : "smb", d.name, d.address);
	}
	check(shown == QStringList({ "smb 192.168.2.140 192.168.2.140", "smb DESKTOP-PC 192.168.2.120",
			"afp iMac 2013 192.168.2.111", "smb Laptop 192.168.2.130" }),
		"a computer sharing over AFP has no Windows entry; others keep theirs (" + shown.join(", ") + ")");
	const std::vector<DiscoveredServer> otherPort = mergeServers(
		{ server(DiscoveredServer::AFP, "Studio", "192.168.2.150:5548") },
		{ server(DiscoveredServer::SMB, "192.168.2.150", "192.168.2.150") });
	check(otherPort.size() == 1 && otherPort[0].kind == DiscoveredServer::AFP,
		"AFP on another port still stands for its computer");
	const std::vector<DiscoveredServer> renamed = mergeServers(
		{ server(DiscoveredServer::SMB, "NAS", "192.168.2.160:4450") },
		{ server(DiscoveredServer::SMB, "192.168.2.161", "192.168.2.161"),
			server(DiscoveredServer::SMB, "192.168.2.160", "192.168.2.160") });
	int nas = 0, byAddress = 0;
	for (const DiscoveredServer &d : renamed) {
		nas += d.name == "NAS";
		byAddress += d.name == "192.168.2.160";
	}
	check(renamed.size() == 2 && nas == 1 && byAddress == 0,
		"a nameless Windows reply takes Bonjour's name for its address, and is then one entry with it");
	/* ProbeMatch Types: a Brother printer's, as it answered live, and
	 * wsdd's for a computer. */
	check(!wsDiscoveryIsComputer("<wsd:ProbeMatch><wsd:Types>wsdp:Device wprt:PrintDeviceType "
			"wscn:ScanDeviceType</wsd:Types></wsd:ProbeMatch>"),
		"a printer's reply isn't a computer");
	check(wsDiscoveryIsComputer("<wsd:ProbeMatch><wsd:Types>wsdp:Device pub:Computer</wsd:Types></wsd:ProbeMatch>"),
		"a computer's reply is");
	check(!wsDiscoveryIsComputer("<wsd:ProbeMatch></wsd:ProbeMatch>"), "a reply without Types isn't");
	check(parseAvahiResolve("192.168.2.30\t74\\05838\\058b7.local\n").value("192.168.2.30") == "74:38:b7",
		"avahi-resolve's escapes are undone");
	/* AppleTalk: zacos9-afp appletalk-servers on this LAN (a Power Mac
	 * 7500 on Mac OS 8, and this computer's own Netatalk). */
	const std::vector<DiscoveredServer> atalk = parseAppleTalkServers(
		"ChromaSystem Pro Server\t65280.163:128\nPowerMac 7500\t65463.88:249\n");
	check(atalk.size() == 2 && atalk[1].name == "PowerMac 7500" &&
			atalk[1].address == "appletalk:PowerMac 7500" && atalk[1].appleTalk() &&
			atalk[1].kind == DiscoveredServer::AFP,
		"AppleTalk servers are AFP servers at appletalk:NAME");
	const std::vector<DiscoveredServer> withAtalk = mergeServers(
		{ server(DiscoveredServer::AFP, "ChromaSystem Pro Server", "192.168.2.110") }, {}, atalk);
	check(withAtalk.size() == 2 && !withAtalk[0].appleTalk() && withAtalk[1].appleTalk() &&
			withAtalk[1].name == "PowerMac 7500",
		"a server also on TCP/IP is listed once, for TCP/IP; one only on AppleTalk is listed");

	/* Real avahi-resolve -a output, from this computer's LAN. */
	const QHash<QString, QString> hosts = parseAvahiResolve(
		"192.168.2.111\tadams-zacos-9.local\n192.168.2.1\tmynetwork.home\nFailed to resolve address '192.168.2.9': Timeout reached\n");
	check(hosts.value("192.168.2.111") == "adams-zacos-9" && hosts.value("192.168.2.1") == "mynetwork.home" &&
			hosts.size() == 2,
		"avahi-resolve's names, without .local, and its failures skipped");

	if (fails) {
		QTextStream(stdout) << fails << " failed\n";
	} else {
		QTextStream(stdout) << "all passed\n";
	}
	return fails ? 1 : 0;
}
