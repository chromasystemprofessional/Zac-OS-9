/* The Extensions Manager's curated catalog, on fixed module lists. */

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "extensioncatalog.h"
#include "extensions.h"

static int failures = 0;

static void check(bool ok, const char *what) {
	if (!ok) {
		std::fprintf(stderr, "FAIL: %s\n", what);
		failures++;
	}
}

static const ExtensionEntry *find(const std::vector<ExtensionEntry> &entries, const char *key) {
	for (const ExtensionEntry &entry : entries) {
		if (entry.key == QLatin1String(key)) {
			return &entry;
		}
	}
	return nullptr;
}

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);

	/* An iMac14,1's modules, abridged: helpers mixed in with features. */
	const QStringList loaded = {
		"snd_hda_codec_hdmi", "snd_hda_codec_cirrus", "snd_hda_codec_generic", "snd_hda_intel",
		"snd_hda_core", "snd_pcm", "i915", "drm_kms_helper", "drm", "btusb", "btbcm",
		"bluetooth", "wl", "cfg80211", "tg3", "libphy", "hid_apple", "usbhid", "hid",
		"applesmc", "crc32c_intel", "libata", "ahci", "ext4", "jbd2", "uvcvideo",
		"xhci_pci", "xhci_hcd", "usbcore", "appletalk", "psnap", "llc",
	};
	const std::vector<ExtensionEntry> entries = curatedExtensions(loaded, { "appletalk" });

	const ExtensionEntry *sound = find(entries, "built-in-sound");
	check(sound && sound->name == "Built-in Sound" && sound->icon == PL_ICON_EXT_AUDIO,
		"Cirrus and generic codecs with the controller are Built-in Sound");
	check(sound && sound->modules == QStringList({ "snd_hda_codec_cirrus",
		"snd_hda_codec_generic", "snd_hda_intel" }), "Built-in Sound lists its modules, sorted");
	const ExtensionEntry *hdmi = find(entries, "hdmi-sound");
	check(hdmi && hdmi->modules == QStringList({ "snd_hda_codec_hdmi" }),
		"the HDMI codec is claimed by HDMI Sound, not Built-in Sound");
	const ExtensionEntry *bluetooth = find(entries, "bluetooth");
	check(bluetooth && bluetooth->modules == QStringList({ "bluetooth", "btusb" }),
		"Bluetooth groups its feature modules into one entry");
	check(find(entries, "wifi") && find(entries, "intel-graphics") && find(entries, "camera") &&
		find(entries, "fans-sensors") && find(entries, "usb") && find(entries, "ethernet"),
		"the familiar hardware all has entries");

	for (const ExtensionEntry &entry : entries) {
		for (const QString &module : entry.modules) {
			check(!QStringList({ "snd_pcm", "snd_hda_core", "drm", "drm_kms_helper", "btbcm",
				"cfg80211", "libphy", "hid", "crc32c_intel", "libata", "jbd2", "xhci_hcd",
				"usbcore", "psnap", "llc" }).contains(module),
				"helper libraries are left out of the curated list");
		}
		check(!entry.name.contains('_') && !entry.description.isEmpty(),
			"every entry has a friendly name and a description");
	}
	check(entries.size() == size_t(pl_n_extensions),
		"every catalog entry is an extension, not only the loaded ones");
	for (size_t i = 1; i < entries.size(); i++) {
		check(entries[i - 1].name.compare(entries[i].name, Qt::CaseInsensitive) < 0,
			"entries are sorted by name");
	}

	const ExtensionEntry *appletalk = find(entries, "appletalk");
	check(appletalk && appletalk->startup, "a modules-load.d module marks its entry as startup");
	check(sound && !sound->startup, "hardware-detected modules are not startup entries");

	const std::vector<ExtensionEntry> usb = curatedExtensions({ "snd-usb-audio" });
	const ExtensionEntry *usbSound = find(usb, "usb-sound");
	check(usbSound && usbSound->modules == QStringList({ "snd_usb_audio" }),
		"dashes and underscores in module names are the same");
	const std::vector<ExtensionEntry> rtwEntries = curatedExtensions({ "rtw88_8822be" });
	const ExtensionEntry *rtw = find(rtwEntries, "wifi");
	check(rtw && rtw->modules.contains("rtw88_8822be"), "prefix patterns match");
	bool anyModules = false;
	for (const ExtensionEntry &entry : curatedExtensions({ "crc16", "libata", "snd" })) {
		anyModules = anyModules || !entry.modules.isEmpty();
	}
	check(!anyModules, "a machine with only helpers has no extension modules");
	for (const ExtensionEntry &entry : curatedExtensions({})) {
		check(entry.modules.isEmpty(), "nothing loaded, no entry names a module");
	}

	/* What is turned off is a blacklist, not what the kernel has loaded:
	 * the folders follow the configuration, and a running module keeps
	 * working until the restart. */
	const QStringList bluetoothModules = { "bluetooth", "btusb" };
	const std::vector<ExtensionEntry> disabledRunning =
		disabledExtensions(bluetoothModules, {}, bluetoothModules);
	const ExtensionEntry *offBluetooth = find(disabledRunning, "bluetooth");
	check(offBluetooth && offBluetooth->modules == QStringList({ "bluetooth", "btusb" }),
		"a blacklisted extension is in Extensions (Disabled), still running");
	const std::vector<ExtensionEntry> onWithBlacklist =
		curatedExtensions(bluetoothModules, {}, bluetoothModules);
	check(!find(onWithBlacklist, "bluetooth"), "and no longer in Extensions");
	const std::vector<ExtensionEntry> disabledStopped = disabledExtensions({}, {}, bluetoothModules);
	check(find(disabledStopped, "bluetooth"), "a blacklisted extension stays off after a restart");
	const std::vector<ExtensionEntry> halfBlacklisted =
		disabledExtensions({ "btusb", "bluetooth" }, {}, { "bluetooth" });
	check(!find(halfBlacklisted, "bluetooth"), "half a blacklist is not turned off");
	const std::vector<ExtensionEntry> halfOn =
		curatedExtensions({ "btusb", "bluetooth" }, {}, { "bluetooth" });
	check(find(halfOn, "bluetooth"), "so the extension is still in Extensions");
	const std::vector<ExtensionEntry> patternOff =
		disabledExtensions({ "rtw88_8822be" }, {}, { "rtw88-8822be" });
	const ExtensionEntry *wildcard = find(patternOff, "wifi");
	check(wildcard && wildcard->modules == QStringList({ "rtw88_8822be" }),
		"a blacklisted module of a pattern entry turns that entry off");
	const std::vector<ExtensionEntry> bare = curatedExtensions({});
	check(find(bare, "wifi"), "hardware this computer lacks is not turned off");
	check(disabledExtensions({}).empty(), "with nothing blacklisted, nothing is disabled");
	const std::vector<ExtensionEntry> disabledStartupEntries =
		disabledExtensions({}, { "appletalk" }, { "appletalk" });
	const ExtensionEntry *disabledStartup = find(disabledStartupEntries, "appletalk");
	check(disabledStartup && disabledStartup->startup,
		"a modules-load.d entry is marked as starting next time");

	/* The modules the helper is asked to turn an extension off with. */
	check(extensionToggleModules("bluetooth", false, { "btusb" }) ==
		QStringList({ "bluetooth", "btusb", "hci_uart" }),
		"turning off names the loaded modules and the catalog's plain names");
	const QStringList wifiOff = extensionToggleModules("wifi", false, {});
	check(wifiOff.contains("iwlwifi") && !wifiOff.join(' ').contains('*'),
		"a pattern is never passed to the helper as a module name");
	check(extensionToggleModules("bluetooth", true, { "btusb" }, { "bluetooth", "btusb" }) ==
		QStringList({ "bluetooth", "btusb" }),
		"turning on names the blacklisted modules, so their lines go");
	check(extensionToggleModules("nope", false, {}).isEmpty(),
		"an unknown key names nothing");

	check(extensionEntry("wifi").name == "Wi-Fi" && extensionEntry("nope").key.isEmpty(),
		"entries can be looked up by key");
	check(extensionModuleIcon("btusb") == PL_ICON_EXT_BLUETOOTH &&
		extensionModuleIcon("crc16") == PL_ICON_EXT_GENERIC,
		"Show All rows get their feature's icon, or the generic piece");

	/* The modprobe.d file the helper writes when an extension is off. */
	QTemporaryDir scratch;
	const QString blacklistFile = scratch.path() + "/zacos9-extensions.conf";
	check(blacklistedModules(blacklistFile).isEmpty(),
		"no blacklist file turns nothing off");
	{
		QFile file(blacklistFile);
		check(file.open(QIODevice::WriteOnly | QIODevice::Truncate),
			"the blacklist file can be written");
		file.write("# turned off in the Extensions Manager\n"
			"blacklist btusb\n"
			"  blacklist  snd-hda-intel  \n"
			"options snd_hda_intel power_save=1\n"
			"blacklist btusb\n"
			"blacklist\n");
	}
	check(blacklistedModules(blacklistFile) == QStringList({ "btusb", "snd_hda_intel" }),
		"the blacklist names each module once, dashes as underscores, sorted");
	check(blacklistedModules("") == blacklistedModules(blacklistPath()),
		"an empty path reads the helper's own file");

	qputenv("ZACOS9_MODPROBE_CONF", blacklistFile.toUtf8());
	check(blacklistPath() == blacklistFile, "the blacklist path can be pointed at a test file");
	qunsetenv("ZACOS9_MODPROBE_CONF");
	check(blacklistPath() == "/etc/modprobe.d/zacos9-extensions.conf",
		"and is the helper's file otherwise");

	if (failures) {
		std::fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	std::printf("extension catalog: all checks passed\n");
	return 0;
}
