/* The Extensions Manager's curated catalog, on fixed module lists. */

#include <QCoreApplication>
#include <cstdio>

#include "extensioncatalog.h"

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
	check(entries.size() == 14, "one entry per feature, not per module");
	for (size_t i = 1; i < entries.size(); i++) {
		check(entries[i - 1].name.compare(entries[i].name, Qt::CaseInsensitive) < 0,
			"entries are sorted by name");
	}

	const ExtensionEntry *appletalk = find(entries, "appletalk");
	check(appletalk && appletalk->startup, "a modules-load.d module marks its entry as startup");
	check(sound && !sound->startup, "hardware-detected modules are not startup entries");

	check(curatedExtensions({ "snd-usb-audio" }).size() == 1 &&
		curatedExtensions({ "snd-usb-audio" })[0].key == "usb-sound",
		"dashes and underscores in module names are the same");
	check(curatedExtensions({ "rtw88_8822be" }).size() == 1, "prefix patterns match");
	check(curatedExtensions({ "crc16", "libata", "snd" }).empty(),
		"a machine with only helpers has no curated entries");
	check(curatedExtensions({}).empty(), "nothing loaded, nothing listed");

	check(extensionEntry("wifi").name == "Wi-Fi" && extensionEntry("nope").key.isEmpty(),
		"entries can be looked up by key");
	check(extensionModuleIcon("btusb") == PL_ICON_EXT_BLUETOOTH &&
		extensionModuleIcon("crc16") == PL_ICON_EXT_GENERIC,
		"Show All rows get their feature's icon, or the generic piece");

	if (failures) {
		std::fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	std::printf("extension catalog: all checks passed\n");
	return 0;
}
