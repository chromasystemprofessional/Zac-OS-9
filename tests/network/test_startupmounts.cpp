/* Saved network disks: the list, the keychain, and the Startup Items entry
 * that makes each one appear in System Folder ▸ Startup Items. */
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cstdio>

#include "autostart.h"
#include "startupmounts.h"

static int failures;

static void check(bool ok, const char *what) {
	printf("%s %s\n", ok ? "ok  " : "FAIL", what);
	failures += !ok;
}

static void writeScript(const QString &path, const QByteArray &body) {
	QFile file(path);
	file.open(QIODevice::WriteOnly);
	file.write(body);
	file.close();
	file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	QTemporaryDir temp;
	check(temp.isValid(), "a temporary home");
	const QString config = temp.path() + "/config";
	const QString xdg = temp.path() + "/xdg";
	const QString bin = temp.path() + "/bin";
	const QString secret = temp.path() + "/secret";
	const QString calls = temp.path() + "/calls";
	QDir().mkpath(config);
	QDir().mkpath(xdg);
	QDir().mkpath(bin);
	qputenv("XDG_CONFIG_HOME", config.toUtf8());
	qputenv("XDG_CONFIG_DIRS", xdg.toUtf8());
	qputenv("XDG_CURRENT_DESKTOP", "ZacOS9");
	qputenv("SECRET_FILE", secret.toUtf8());
	qputenv("SECRET_CALLS", calls.toUtf8());
	qputenv("PATH", (bin + ":" + qEnvironmentVariable("PATH")).toUtf8());

	writeScript(bin + "/zacos9-netmount", "#!/bin/sh\nexit 0\n");
	writeScript(bin + "/secret-tool",
		"#!/bin/sh\n"
		"case \"$1\" in\n"
		"store)\n"
		"  shift\n"
		"  while [ \"$1\" = --label ]; do shift 2; done\n"
		"  cat > \"$SECRET_FILE\"\n"
		"  printf '%s\\n' \"$*\" > \"$SECRET_CALLS\"\n"
		"  ;;\n"
		"lookup) cat \"$SECRET_FILE\" 2>/dev/null ;;\n"
		"clear) rm -f \"$SECRET_FILE\"; printf 'clear %s\\n' \"$*\" > \"$SECRET_CALLS\" ;;\n"
		"esac\n");
	qputenv("ZACOS9_SECRET_TOOL", (bin + "/secret-tool").toUtf8());
	qputenv("ZACOS9_NETMOUNT", (bin + "/zacos9-netmount").toUtf8());

	StartupMount afp;
	afp.kind = StartupMount::AFP;
	afp.server = "server.local";
	afp.volume = "Home";
	afp.user = "alice";
	afp.guest = false;

	check(startupMountKeychainAvailable(), "the keychain helper is found");
	check(saveStartupMount(afp, "s3cret"), "a registered AFP disk is saved");
	check(startupMountExists(afp), "its Startup Items entry is enabled");
	check(startupMounts().size() == 1, "the saved list has it");
	check(startupMountPassword(afp) == "s3cret", "its password comes back from the keychain");
	check(QFileInfo(startupMountAutostartFile(afp)).isFile(), "its Startup Items entry exists");
	QFile entry(startupMountAutostartFile(afp));
	entry.open(QIODevice::ReadOnly);
	const QByteArray text = entry.readAll();
	check(text.contains("X-ZacOS9-NetworkMount=true"), "the entry is marked as a network disk");
	bool networkFlag = false;
	for (const AutostartEntry &saved : autostartEntries()) {
		if (saved.id == startupMountId(afp)) {
			networkFlag = saved.networkMount;
		}
	}
	check(networkFlag, "Startup Items knows the entry is a network disk");
	check(text.contains("Exec=" + (bin + "/zacos9-netmount").toUtf8() + " --mount-id " +
			startupMountId(afp).toUtf8()),
		"the entry runs the mount helper for this disk");
	check(text.contains("Name=Home on server.local"), "the entry is named after the disk");
	const auto perms = QFileInfo(startupMountsFile()).permissions();
	check(!(perms & (QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther)),
		"the saved list is readable only by its owner");

	StartupMount smb;
	smb.kind = StartupMount::SMB;
	smb.server = "nas";
	smb.share = "Public";
	smb.guest = true;
	check(saveStartupMount(smb, QString()), "a guest SMB disk is saved");
	check(startupMounts().size() == 2, "both disks are listed");
	check(QFileInfo(startupMountAutostartFile(smb)).isFile(), "the SMB disk has its own entry");
	check(startupMountPassword(smb).isEmpty(), "a guest disk has no keychain password");

	check(forgetStartupMount(afp), "the AFP disk is forgotten");
	check(startupMounts().size() == 1 && !startupMountExists(afp),
		"only the SMB disk remains");
	check(!QFileInfo(startupMountAutostartFile(afp)).exists(),
		"its Startup Items entry is gone");
	check(startupMountPassword(afp).isEmpty(), "its keychain item is gone");

	check(forgetStartupMount(smb), "the SMB disk is forgotten");
	check(startupMounts().empty(), "no saved disks remain");
	check(!QFileInfo(startupMountAutostartFile(smb)).exists(),
		"the last Startup Items entry is gone");

	check(saveStartupMount(afp, "again"), "a disk can be saved again");
	check(forgetStartupMountById(startupMountId(afp)), "forgetting by ID removes the saved disk");
	check(startupMounts().empty() && !startupMountExists(afp),
		"forgetting by ID removes its Startup Items entry too");

	check(saveStartupMount(afp, "again"), "a disk saved before a Hidden entry");
	QFile hidden(startupMountAutostartFile(afp));
	hidden.open(QIODevice::Append);
	hidden.write("Hidden=true\n");
	hidden.close();
	check(!startupMountExists(afp), "a hidden Startup Items entry is not enabled");

	return failures ? 1 : 0;
}
