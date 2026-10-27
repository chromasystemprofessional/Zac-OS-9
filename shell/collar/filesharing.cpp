#include "filesharing.h"
#include "conf.h"

#include <QFile>
#include <QProcess>
#include <QRegularExpression>

#include "sharingclient.h"

void parseSharingConf(const QByteArray &conf, bool *afp, bool *smb) {
	const KeyValueFile file = KeyValueFile::parse(conf);
	*afp = file.yes("afp");
	*smb = file.yes("smb");
}

QByteArray sharingConfWith(const QByteArray &conf, bool afp, bool smb) {
	KeyValueFile file = KeyValueFile::parse(conf);
	file.setYes("afp", afp);
	file.setYes("smb", smb);
	return file.text();
}

/* "[::ffff:192.168.1.5]:51234" or "192.168.1.5:51234" -> 192.168.1.5 */
static QString hostOf(const QString &endpoint) {
	QString host = endpoint.section(':', 0, -2);
	if (host.startsWith('[') && host.endsWith(']')) {
		host = host.mid(1, host.size() - 2);
	}
	if (host.startsWith("::ffff:")) {
		host = host.mid(7);
	}
	return host;
}

QList<SharingVisitor> parseVisitors(const QString &ss) {
	// Recv-Q Send-Q Local:Port Peer:Port [Process]
	QList<SharingVisitor> out;
	for (const QString &line : ss.split('\n', Qt::SkipEmptyParts)) {
		const QStringList f = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
		if (f.size() < 4) {
			continue;
		}
		const QString port = f[2].section(':', -1);
		const QString kind = port == "548" ? "Mac" : port == "445" ? "Windows" : QString();
		if (kind.isEmpty()) {
			continue;
		}
		const SharingVisitor v{ kind, hostOf(f[3]) };
		if (!out.contains(v)) {
			out << v; /* a Windows visitor may hold several connections */
		}
	}
	return out;
}

SharingStatus::SharingStatus(QObject *parent) : QObject(parent) {}

void SharingStatus::report(bool wasConfigured, bool wasAfp, bool wasSmb, const QList<SharingVisitor> &was) {
	if ((wasConfigured != configured || wasAfp != afp || wasSmb != smb || was != visitors) && changed) {
		changed();
	}
}

void SharingStatus::refresh() {
	const bool wasConfigured = configured, wasAfp = afp, wasSmb = smb;
	QFile f(sharingConfFile("sharing.conf"));
	configured = f.open(QIODevice::ReadOnly);
	if (configured) {
		parseSharingConf(f.readAll(), &afp, &smb);
	} else {
		afp = smb = false;
	}
	if (!on()) {
		const QList<SharingVisitor> was = visitors;
		visitors.clear();
		report(wasConfigured, wasAfp, wasSmb, was);
		return;
	}
	if (m_ss) {
		return; /* the last look is still going */
	}
	m_ss = new QProcess(this);
	connect(m_ss, &QProcess::finished, this, [this, wasConfigured, wasAfp, wasSmb] {
		const QList<SharingVisitor> was = visitors;
		visitors = parseVisitors(QString::fromLocal8Bit(m_ss->readAllStandardOutput()));
		m_ss->deleteLater();
		m_ss = nullptr;
		report(wasConfigured, wasAfp, wasSmb, was);
	});
	connect(m_ss, &QProcess::errorOccurred, this, [this, wasConfigured, wasAfp, wasSmb](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			m_ss->deleteLater();
			m_ss = nullptr;
			report(wasConfigured, wasAfp, wasSmb, visitors);
		}
	});
	const QString program = qEnvironmentVariable("ZACOS9_SS", "ss");
	m_ss->start(program, { "-Htn", "state", "established", "( sport = :548 or sport = :445 )" });
}

void SharingStatus::setOn(bool wantOn, bool lastAfp, bool lastSmb) {
	QFile f(sharingConfFile("sharing.conf"));
	if (busy || !f.open(QIODevice::ReadOnly)) {
		return;
	}
	const QByteArray conf = f.readAll();
	f.close();
	bool newAfp = false, newSmb = false;
	if (wantOn) {
		newAfp = lastAfp || !lastSmb;
		newSmb = lastSmb;
	}
	busy = true;
	if (changed) {
		changed();
	}
	bool ok;
	QString err;
	runSharingHelper({ "apply" }, sharingConfWith(conf, newAfp, newSmb), &ok, &err);
	busy = false;
	if (!ok && failed) {
		failed(err.isEmpty() ? QString("File sharing couldn't be turned %1.").arg(wantOn ? "on" : "off")
			: err.section('\n', -1));
	}
	refresh();
	if (changed) {
		changed();
	}
}
