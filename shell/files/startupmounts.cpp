#include "startupmounts.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

#include "autostart.h"
#include "process.h"

static QString configBase() {
	QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
	if (base.isEmpty()) {
		base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
	}
	return base;
}

QString startupMountsFile() {
	const QString env = qEnvironmentVariable("ZACOS9_NETWORK_MOUNTS");
	if (!env.isEmpty()) {
		return env;
	}
	return configBase() + "/zacos9/network-mounts.json";
}

QString startupMountsAutostartDir() {
	const QString env = qEnvironmentVariable("ZACOS9_NETWORK_AUTOSTART_DIR");
	if (!env.isEmpty()) {
		return env;
	}
	return configBase() + "/autostart";
}

static QString diskName(const StartupMount &mount) {
	return mount.kind == StartupMount::AFP ? mount.volume : mount.share;
}

QString startupMountId(const StartupMount &mount) {
	QByteArray key = mount.kind == StartupMount::AFP ? "afp" : "smb";
	key += '\0';
	key += mount.server.toLower().toUtf8();
	key += '\0';
	key += diskName(mount).toUtf8();
	key += '\0';
	key += mount.user.toUtf8();
	key += '\0';
	key += mount.guest ? '1' : '0';
	const QByteArray hash = QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex().left(16);
	return "zacos9-network-" + QString::fromLatin1(hash) + ".desktop";
}

QString startupMountAutostartFile(const StartupMount &mount) {
	return startupMountsAutostartDir() + "/" + startupMountId(mount);
}

static bool sameMount(const StartupMount &a, const StartupMount &b) {
	return a.kind == b.kind &&
		a.server.compare(b.server, Qt::CaseInsensitive) == 0 &&
		a.volume == b.volume && a.share == b.share &&
		a.user == b.user && a.guest == b.guest;
}

std::vector<StartupMount> startupMounts() {
	std::vector<StartupMount> out;
	QFile file(startupMountsFile());
	if (!file.open(QIODevice::ReadOnly)) {
		return out;
	}
	const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	for (const QJsonValue &value : doc.object().value("mounts").toArray()) {
		const QJsonObject object = value.toObject();
		StartupMount mount;
		const QString kind = object.value("kind").toString();
		if (kind == "afp") {
			mount.kind = StartupMount::AFP;
		} else if (kind == "smb") {
			mount.kind = StartupMount::SMB;
		} else {
			continue;
		}
		mount.server = object.value("server").toString().trimmed();
		mount.volume = object.value("volume").toString();
		mount.share = object.value("share").toString();
		mount.user = object.value("user").toString();
		mount.guest = object.value("guest").toBool(true);
		mount.cleartext = object.value("cleartext").toBool(false);
		if (mount.server.isEmpty() || diskName(mount).isEmpty()) {
			continue;
		}
		out.push_back(mount);
	}
	return out;
}

static bool entryEnabled(const StartupMount &mount) {
	const QString id = startupMountId(mount);
	for (const AutostartEntry &entry : autostartEntries()) {
		if (entry.id == id) {
			return entry.enabled;
		}
	}
	return false;
}

bool startupMountExists(const StartupMount &mount) {
	if (!entryEnabled(mount)) {
		return false;
	}
	for (const StartupMount &saved : startupMounts()) {
		if (sameMount(saved, mount)) {
			return true;
		}
	}
	return false;
}

static bool writeMounts(const std::vector<StartupMount> &mounts, QString *error) {
	const QString path = startupMountsFile();
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		if (error) {
			*error = "The saved mounts folder couldn’t be made.";
		}
		return false;
	}
	QJsonArray array;
	for (const StartupMount &mount : mounts) {
		QJsonObject object;
		object.insert("kind", mount.kind == StartupMount::AFP ? "afp" : "smb");
		object.insert("server", mount.server);
		object.insert("volume", mount.volume);
		object.insert("share", mount.share);
		object.insert("user", mount.user);
		object.insert("guest", mount.guest);
		object.insert("cleartext", mount.cleartext);
		array.append(object);
	}
	QJsonObject root;
	root.insert("version", 1);
	root.insert("mounts", array);
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error) {
			*error = "The saved mounts file couldn’t be written.";
		}
		return false;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
	if (!file.commit()) {
		if (error) {
			*error = "The saved mounts file couldn’t be saved.";
		}
		return false;
	}
	QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
	return true;
}

static bool writeAutostart(const StartupMount &mount, QString *error) {
	const QString path = startupMountAutostartFile(mount);
	if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
		if (error) {
			*error = "The Startup Items folder couldn’t be made.";
		}
		return false;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		if (error) {
			*error = "The Startup Items entry couldn’t be written.";
		}
		return false;
	}
	QString program = qEnvironmentVariable("ZACOS9_NETMOUNT");
	if (program.isEmpty()) {
		program = QStandardPaths::findExecutable("zacos9-netmount");
	}
	if (program.isEmpty()) {
		const QString sibling = QCoreApplication::applicationDirPath() + "/zacos9-netmount";
		if (QFileInfo(sibling).isExecutable()) {
			program = sibling;
		}
	}
	if (program.isEmpty()) {
		program = "zacos9-netmount";
	}
	if (program.contains(' ')) {
		program = "'" + program + "'";
	}
	const QString name = (diskName(mount) + " on " + mount.server)
		.replace('\n', ' ').replace('\r', ' ');
	file.write(QString("[Desktop Entry]\n"
		"Type=Application\n"
		"Name=%1\n"
		"Comment=Mounts this network disk when you log in\n"
		"Exec=%2 --mount-id %3\n"
		"Icon=network-workgroup\n"
		"OnlyShowIn=ZacOS9;\n"
		"NoDisplay=true\n"
		"X-ZacOS9-NetworkMount=true\n").arg(name, program, startupMountId(mount)).toUtf8());
	if (!file.commit()) {
		if (error) {
			*error = "The Startup Items entry couldn’t be saved.";
		}
		return false;
	}
	return true;
}

static void removeAutostart(const StartupMount &mount) {
	QFile::remove(startupMountAutostartFile(mount));
}

static QString secretTool() {
	const QString env = qEnvironmentVariable("ZACOS9_SECRET_TOOL");
	return env.isEmpty() ? QStandardPaths::findExecutable("secret-tool") : env;
}

bool startupMountKeychainAvailable() {
	return !secretTool().isEmpty();
}

static QStringList secretAttributes(const StartupMount &mount) {
	QStringList args = { "service", "zacos9-network-mount", "server", mount.server };
	args << (mount.kind == StartupMount::AFP ? "volume" : "share") << diskName(mount);
	if (!mount.user.isEmpty()) {
		args << "user" << mount.user;
	}
	return args;
}

static QString secretLabel(const StartupMount &mount) {
	return QStringLiteral("Network disk “%1” on “%2”").arg(diskName(mount), mount.server);
}

static bool storeSecret(const StartupMount &mount, const QString &password, QString *error) {
	const QString tool = secretTool();
	if (tool.isEmpty()) {
		if (error) {
			*error = "The keychain isn’t available.";
		}
		return false;
	}
	QStringList args = { "store", "--label", secretLabel(mount) };
	args << secretAttributes(mount);
	const ProcessResult result = runProcess(tool, args, { password.toUtf8(), 15000 });
	if (!result.ok()) {
		if (error) {
			*error = result.started
				? "The password couldn’t be saved in the keychain."
				: "The keychain helper couldn’t be run.";
		}
		return false;
	}
	return true;
}

static void clearSecret(const StartupMount &mount) {
	const QString tool = secretTool();
	if (tool.isEmpty() || mount.guest) {
		return;
	}
	QStringList args = { "clear" };
	args << secretAttributes(mount);
	runProcess(tool, args, { {}, 5000 });
}

QString startupMountPassword(const StartupMount &mount) {
	if (mount.guest) {
		return QString();
	}
	const QString tool = secretTool();
	if (tool.isEmpty()) {
		return QString();
	}
	QStringList args = { "lookup" };
	args << secretAttributes(mount);
	const ProcessResult result = runProcess(tool, args, { {}, 5000 });
	if (!result.ok()) {
		return QString();
	}
	QByteArray secret = result.out;
	if (secret.endsWith('\n')) {
		secret.chop(1);
	}
	return QString::fromUtf8(secret);
}

bool saveStartupMount(const StartupMount &mount, const QString &password, QString *error) {
	if (mount.server.trimmed().isEmpty() || diskName(mount).trimmed().isEmpty()) {
		if (error) {
			*error = "The disk has no server or name to save.";
		}
		return false;
	}
	if (!mount.guest && !storeSecret(mount, password, error)) {
		return false;
	}
	std::vector<StartupMount> mounts = startupMounts();
	mounts.erase(std::remove_if(mounts.begin(), mounts.end(),
		[&mount](const StartupMount &saved) { return sameMount(saved, mount); }), mounts.end());
	mounts.push_back(mount);
	if (!writeMounts(mounts, error)) {
		clearSecret(mount);
		return false;
	}
	if (!writeAutostart(mount, error)) {
		mounts.erase(std::remove_if(mounts.begin(), mounts.end(),
			[&mount](const StartupMount &saved) { return sameMount(saved, mount); }), mounts.end());
		writeMounts(mounts, nullptr);
		clearSecret(mount);
		return false;
	}
	return true;
}

bool forgetStartupMount(const StartupMount &mount, QString *error) {
	std::vector<StartupMount> mounts = startupMounts();
	const size_t before = mounts.size();
	mounts.erase(std::remove_if(mounts.begin(), mounts.end(),
		[&mount](const StartupMount &saved) { return sameMount(saved, mount); }), mounts.end());
	if (mounts.size() == before) {
		removeAutostart(mount);
		return true;
	}
	if (!writeMounts(mounts, error)) {
		return false;
	}
	clearSecret(mount);
	removeAutostart(mount);
	return true;
}

bool forgetStartupMountById(const QString &id, QString *error) {
	for (const StartupMount &mount : startupMounts()) {
		if (startupMountId(mount) == id) {
			return forgetStartupMount(mount, error);
		}
	}
	return false;
}
