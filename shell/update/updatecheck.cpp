#include "updatecheck.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

static const char DEFAULT_REPO[] = "chromasystemprofessional/Zac-OS-9";

QString updateRepo() {
	const QString env = qEnvironmentVariable("ZACOS9_UPDATE_REPO");
	return env.isEmpty() ? QString::fromLatin1(DEFAULT_REPO) : env;
}

QString updateReleaseUrl() {
	const QString env = qEnvironmentVariable("ZACOS9_UPDATE_URL");
	return env.isEmpty() ? "https://api.github.com/repos/" + updateRepo() + "/releases/latest" : env;
}

Release parseRelease(const QByteArray &json, const QString &arch) {
	Release r;
	const QJsonObject o = QJsonDocument::fromJson(json).object();
	QString tag = o.value("tag_name").toString();
	if (tag.startsWith('v') || tag.startsWith('V')) {
		tag.remove(0, 1);
	}
	/* Only what Debian can call a version reaches dpkg and the window. */
	static const QRegularExpression versionRe("^[0-9][A-Za-z0-9.+~-]*$");
	if (!versionRe.match(tag).hasMatch()) {
		return r;
	}
	r.version = tag;
	r.name = o.value("name").toString();
	if (r.name.isEmpty()) {
		r.name = o.value("tag_name").toString();
	}
	r.notes = o.value("body").toString();
	const QString suffix = "_" + arch + ".deb";
	for (const QJsonValue &v : o.value("assets").toArray()) {
		const QJsonObject a = v.toObject();
		const QString name = a.value("name").toString();
		if (!name.startsWith("zacos9_") || !name.endsWith(suffix)) {
			continue;
		}
		const QString url = a.value("browser_download_url").toString();
		if (!url.startsWith("https://")) {
			continue;
		}
		r.debName = name;
		r.debUrl = url;
		const QString digest = a.value("digest").toString();
		if (digest.startsWith("sha256:")) {
			r.sha256 = digest.mid(7).toLower();
		}
		r.valid = true;
		break;
	}
	return r;
}

std::vector<DebianUpdate> parseAptSimulation(const QString &out) {
	/* "Inst libfoo1 [1.2-1] (1.2-2 Debian-Security:13/stable-security [amd64])"
	 * "Inst linux-image-6.12.9-amd64 (6.12.9-1 Debian:13.1/stable [amd64])" */
	static const QRegularExpression inst(R"(^Inst (\S+)(?: \[([^\]]+)\])? \((\S+))");
	std::vector<DebianUpdate> updates;
	for (const QString &line : out.split('\n')) {
		const auto m = inst.match(line);
		if (m.hasMatch()) {
			updates.push_back({ m.captured(1), m.captured(2), m.captured(3) });
		}
	}
	return updates;
}

/* A short local command (dpkg's own), run to the end while the event loop
 * keeps going: never QProcess::waitForFinished(), which has deadlocked in
 * this desktop's Qt/Wayland programs (see CLAUDE.md). */
static QString runQuiet(const QString &program, const QStringList &args, int *code = nullptr) {
	QProcess p;
	bool done = false;
	QObject::connect(&p, &QProcess::finished, [&done] { done = true; });
	QObject::connect(&p, &QProcess::errorOccurred, [&done](QProcess::ProcessError e) {
		if (e == QProcess::FailedToStart) {
			done = true;
		}
	});
	p.start(program, args);
	p.closeWriteChannel();
	while (!done) {
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents,
			100);
	}
	if (code) {
		*code = p.error() != QProcess::FailedToStart && p.exitStatus() == QProcess::NormalExit
			? p.exitCode() : -1;
	}
	return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

bool versionNewer(const QString &a, const QString &b) {
	if (a.isEmpty()) {
		return false;
	}
	if (b.isEmpty()) {
		return true;
	}
	int code = -1;
	runQuiet("dpkg", { "--compare-versions", a, "gt", b }, &code);
	return code == 0;
}

QString installedVersion(const QString &package) {
	int code = -1;
	const QString out = runQuiet("dpkg-query", { "-W", "-f=${Status}\t${Version}", package }, &code);
	if (code != 0 || !out.startsWith("install ok installed\t")) {
		return {};
	}
	return out.section('\t', 1);
}

QString debArchitecture() {
	const QString arch = runQuiet("dpkg", { "--print-architecture" });
	return arch.isEmpty() ? QStringLiteral("amd64") : arch;
}
