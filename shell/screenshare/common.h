#pragma once

/*
 * What the Screen Sharing panel and zacos9-screenshare-host share: where
 * things live, the settings, and the host's control socket.
 *
 *   $XDG_CONFIG_HOME/zacos9/screensharing.conf   ask=yes control=yes sound=yes
 *   $XDG_CONFIG_HOME/zacos9/screensharing/       cert.pem, key.pem (TLS)
 *   $XDG_RUNTIME_DIR/zacos9-screenshare/         host.sock (the panel's),
 *                                                vnc.sock and ctl.sock
 *                                                (wayvnc's), wayvnc.conf
 *
 * host.sock takes one line and answers, then closes:
 *   status          "on" then, per connection, "conn\tID\tADDRESS\tNAME\tSINCE"
 *                   (SINCE in seconds since the epoch)
 *   disconnect ID   "ok", or an error line
 *   reload          rereads the settings (restarts wayvnc and Bonjour)
 *   quit            stops sharing
 *
 * The sound (sound=yes) goes on its own connection, on the next port
 * (AUDIO_PORT): wayvnc's TLS runs from end to end through the host, so
 * nothing can be added to that stream. TLS with the same certificate,
 * then the viewer says
 *   "ZACOS-SOUND 1\n" NAME "\n" PASSWORD "\n"
 * (the owner's, as for the screen), and the host answers one line:
 *   "ok RATE CHANNELS\n"   then 16-bit little-endian samples, until either
 *                          side closes
 *   anything else         why not (no sound shared, wrong password, or
 *                          that computer isn't viewing the screen)
 * Only a computer the owner has let in (a live screen connection from
 * the same address) is answered.
 */

#include <QString>
#include <QStringList>
#include <vector>

namespace screenshare {

constexpr quint16 PORT = 5900;
constexpr quint16 AUDIO_PORT_OFFSET = 1; /* the sound's port: the screen's, plus this */
constexpr int AUDIO_RATE = 48000, AUDIO_CHANNELS = 2;

struct Settings {
	bool ask = true;     /* ask before someone views the screen */
	bool control = true; /* let them use the mouse and keyboard */
	bool sound = true;   /* let them hear this computer's sound */
};

QString configDir();  /* $XDG_CONFIG_HOME/zacos9 */
QString runtimeDir(); /* $XDG_RUNTIME_DIR/zacos9-screenshare */
QString certFile();
QString keyFile();
QString hostSocket();

Settings readSettings();
bool writeSettings(const Settings &s);

/* The name others see: File Sharing's Computer Name, else Mac OS 9's
 * "Owner's Computer". */
QString computerName();
QString ownerName();

/* SHA-256 of the certificate (DER), as "AB12 CD34 …"; empty without one. */
QString certFingerprint(const QString &pemFile);
QString derFingerprint(const QByteArray &der);
/* Certificates the viewer has been shown and accepted, by host. */
QString knownHostsFile(); /* $XDG_CONFIG_HOME/zacos9/screensharing/known-hosts */

/* "Start when I log in": an XDG autostart entry for the host. */
QString autostartFile();

bool startsAtLogin();
bool setStartsAtLogin(bool on, const QString &hostProgram);

/* Another ZacOS computer sharing its screen, as Bonjour announces it. */
struct SharedScreen {
	QString name;    /* its Computer Name */
	QString address; /* IPv4 address, else its .local name */
	quint16 port = PORT;
	QString target() const { return port == PORT ? address : QString("%1:%2").arg(address).arg(port); }
};

/* `avahi-browse -k -p -r -t _rfb._tcp` output: the ZacOS computers among
 * the VNC servers (zacos=1 in their TXT record), each once, by IPv4
 * address where there is one; not `ownName` (this computer). */
std::vector<SharedScreen> parseScreenBrowse(const QString &text, const QString &ownName = QString());

/* Sends one command to the running host and returns its answer's lines;
 * false if no host answered. Keeps the windows drawn while it waits. */
bool hostRequest(const QString &command, QStringList *reply, int timeoutMs = 3000);

} // namespace screenshare
