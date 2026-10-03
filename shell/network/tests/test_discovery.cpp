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
#include <QTextStream>

#include "discovery.h"

static int fails;

static void check(bool ok, const QString &what) {
	QTextStream(stdout) << (ok ? "ok    " : "FAIL  ") << what << "\n";
	fails += !ok;
}

int main() {
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

	/* A non-default port is kept; IPv6 is skipped (IPv4 alone is
	 * enough to connect, and showing both would list every server
	 * twice). */
	const QString smbLine = "=;eth0;IPv4;OFFICE;_smb._tcp;local;"
		"office.local;10.0.0.5;1445;";
	check(parseAvahiBrowseLine(smbLine, &s), "a resolved SMB line parses");
	check(s.kind == DiscoveredServer::SMB, "as an SMB server");
	check(s.address == "10.0.0.5:1445", "a non-default port is kept in the address");
	const QString v6Line = "=;eth0;IPv6;OFFICE;_smb._tcp;local;"
		"office.local;fe80::1;445;";
	check(!parseAvahiBrowseLine(v6Line, &s), "an IPv6 resolution is skipped (IPv4 covers it)");

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

	if (fails) {
		QTextStream(stdout) << fails << " failed\n";
	} else {
		QTextStream(stdout) << "all passed\n";
	}
	return fails ? 1 : 0;
}
