#pragma once

/*
 * Finding servers to connect to, the way the Network Browser's list
 * fills in: Bonjour (modern Macs, and our own server once File Sharing
 * announces it) through avahi, and Windows computers through our own
 * WS-Discovery probe (Debian ships no query tool: wsdd2 only answers
 * other computers' probes, it has no "ask the network" mode of its own).
 */

#include <QString>
#include <vector>

struct DiscoveredServer {
	enum Kind { AFP, SMB };
	Kind kind = AFP;
	QString name; /* a person-readable name: the announced one, or the address */
	QString address; /* host or "host:port", ready for platinum-afp or smb:// */

	bool operator==(const DiscoveredServer &o) const {
		return kind == o.kind && address == o.address;
	}
};

/* Bonjour: every _afpovertcp._tcp and _smb._tcp instance on the LAN,
 * through `avahi-browse -p -r -t` (parseable, resolved, one-shot —
 * avahi's own daemon keeps the cache current, so a few seconds of
 * listening is enough; this is simpler and no less robust than linking
 * avahi-client for the same information). Empty, not an error, if
 * avahi-browse isn't installed or nothing answers. */
std::vector<DiscoveredServer> bonjourServers(int timeoutMs = 3000);

/* Windows computers: a WS-Discovery Probe multicast to 239.255.255.250,
 * collecting the ProbeMatch replies that come back (addressed straight
 * to the probing socket, not multicast, per the spec). Named by the
 * reply's ComputerName when one is given, else its source address. */
std::vector<DiscoveredServer> wsDiscoveryServers(int timeoutMs = 3000);

/* Parses one avahi-browse -p -r line (the resolved "=" kind); returns
 * false if it's an unresolved "+" line or malformed. Exposed for
 * testing: the escaping and field order are what a real build depends
 * on actually matching avahi's own output. */
bool parseAvahiBrowseLine(const QString &line, DiscoveredServer *out);

/* Pulls ComputerName (if present) and the sender's own name out of a
 * WS-Discovery ProbeMatch body; exposed for testing against a recorded
 * reply without a live Windows computer to probe. */
QString wsDiscoveryComputerName(const QString &xml);

/* Both, merged and de-duplicated, sorted by name. What the Network
 * Browser window actually shows. */
std::vector<DiscoveredServer> discoverServers(int timeoutMs = 3000);
