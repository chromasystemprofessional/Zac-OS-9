#pragma once

/*
 * Finding servers to connect to, the way the Network Browser's list
 * fills in: Bonjour (modern Macs, and our own server once File Sharing
 * announces it) through avahi, and Windows computers through our own
 * WS-Discovery probe (Debian ships no query tool: wsdd2 only answers
 * other computers' probes, it has no "ask the network" mode of its own),
 * and Macs that share only over AppleTalk (Mac OS 8.6 and earlier)
 * through `zacos9-afp appletalk-servers` (NBP).
 */

#include <QHash>
#include <QString>
#include <vector>

struct DiscoveredServer {
	enum Kind { AFP, SMB };
	Kind kind = AFP;
	QString name; /* a person-readable name: the announced one, or the address */
	QString address; /* host or "host:port", ready for zacos9-afp or smb:// */
	bool byHostName = false; /* announced over IPv6 alone: address is its .local name */

	/* Reached over AppleTalk: address is "appletalk:NAME". */
	bool appleTalk() const { return address.startsWith(QLatin1String("appletalk:")); }

	bool operator==(const DiscoveredServer &o) const {
		return kind == o.kind && address == o.address;
	}
};

/* Bonjour: _afpovertcp._tcp and _smb._tcp instances on the LAN, through
 * concurrent, service-specific `avahi-browse -k -p -r -t` queries (parseable,
 * resolved, one-shot). A shared timeout bounds both queries; unrelated
 * service resolution cannot delay their results. Empty if avahi-browse isn't
 * installed or nothing answers. */
std::vector<DiscoveredServer> bonjourServers(int timeoutMs = 3000);

/* Windows computers: a WS-Discovery Probe for pub:Computer multicast to 239.255.255.250,
 * collecting the ProbeMatch replies that come back (addressed straight
 * to the probing socket, not multicast, per the spec). Named by the
 * reply's ComputerName when one is given, else its source address. */
std::vector<DiscoveredServer> wsDiscoveryServers(int timeoutMs = 3000);

/* Parses one avahi-browse -p -r line (the resolved "=" kind); returns
 * false if it's an unresolved "+" line or malformed. Exposed for
 * testing: the escaping and field order are what a real build depends
 * on actually matching avahi's own output. */
bool parseAvahiBrowseLine(const QString &line, DiscoveredServer *out);
/* Every server in avahi-browse's parseable output: by its IPv4 address,
 * or - announced over IPv6 alone, which a server's avahi can do for a
 * while even when it has an IPv4 address - by its .local host name. */
std::vector<DiscoveredServer> parseAvahiBrowse(const QString &text);

/* Is this ProbeMatch from a computer (pub:Computer among its Types),
 * not a printer or scanner? */
bool wsDiscoveryIsComputer(const QString &xml);

/* Pulls ComputerName (if present) and the sender's own name out of a
 * WS-Discovery ProbeMatch body; exposed for testing against a recorded
 * reply without a live Windows computer to probe. */
QString wsDiscoveryComputerName(const QString &xml);

/* `zacos9-afp appletalk-servers` output (NAME<TAB>NET.NODE:SOCKET lines)
 * as AFP servers at "appletalk:NAME": by name, since a Mac's AppleTalk
 * address can change from one start to the next. */
std::vector<DiscoveredServer> parseAppleTalkServers(const QString &text);

/* Bonjour's, WS-Discovery's and AppleTalk's lists as one, sorted by name: each
 * computer once per kind, and no Windows entry for a computer that also
 * shares over AFP (a Mac, or ZacOS with both on: its AFP entry is the
 * one to use, and Connect to Server still reaches its Windows sharing).
 * A WS-Discovery reply without a ComputerName takes the name Bonjour
 * announced for that address, if any. A server found over AppleTalk
 * and also over TCP/IP (Mac OS 9, ZacOS) is listed once, for TCP/IP:
 * it's much faster. */
std::vector<DiscoveredServer> mergeServers(const std::vector<DiscoveredServer> &bonjour,
	const std::vector<DiscoveredServer> &windows,
	const std::vector<DiscoveredServer> &appleTalk = {});

/* `avahi-resolve -a ADDRESS...` output ("address<TAB>host" lines) as
 * address -> host, without ".local". */
QHash<QString, QString> parseAvahiResolve(const QString &text);

/* Both, found and merged (mergeServers); Windows computers still known
 * only by address are named by reverse mDNS (avahi-resolve -a), if they
 * answer it. What the Network Browser window actually shows. */
std::vector<DiscoveredServer> discoverServers(int timeoutMs = 3000);
