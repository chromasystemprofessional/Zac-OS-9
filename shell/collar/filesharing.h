#pragma once

/*
 * File Sharing for The Collar (Mac OS 9's File Sharing Control Strip
 * module): whether it is on, who is connected, and turning it on or off.
 * Nothing here needs root: the settings are /etc/zacos9/sharing.conf,
 * which everyone may read, and the visitors are the established
 * connections to the AFP and SMB ports (ss). Only turning it on or off
 * runs zacos9-sharing-helper, as the File Sharing panel does.
 */

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class QProcess;

struct SharingVisitor {
	QString kind;    /* "Mac" (AFP) or "Windows" (SMB) */
	QString address; /* where from */
	bool operator==(const SharingVisitor &o) const { return kind == o.kind && address == o.address; }
};

/* sharing.conf's afp= and smb=. */
void parseSharingConf(const QByteArray &conf, bool *afp, bool *smb);
/* sharing.conf with afp= and smb= changed, everything else kept. */
QByteArray sharingConfWith(const QByteArray &conf, bool afp, bool smb);
/* `ss -Htn state established` output: who is connected to ports 548 and 445. */
QList<SharingVisitor> parseVisitors(const QString &ss);

class SharingStatus : public QObject {
public:
	explicit SharingStatus(QObject *parent = nullptr);
	/* Set up once in the File Sharing panel (sharing.conf exists). */
	bool configured = false;
	bool afp = false, smb = false;
	QList<SharingVisitor> visitors;
	bool busy = false;
	std::function<void()> changed;
	std::function<void(const QString &)> failed;

	bool on() const { return afp || smb; }
	void refresh();
	/* Off: both servers stop. On: the ones on before (else Mac sharing,
	 * which needs no Windows password). Needs the panel's setup first. */
	void setOn(bool on, bool lastAfp, bool lastSmb);

private:
	QProcess *m_ss = nullptr;
	void report(bool wasConfigured, bool wasAfp, bool wasSmb, const QList<SharingVisitor> &was);
};
