#include "extensioncatalog.h"

#include <algorithm>

namespace {

struct CatalogEntry {
	const char *key;
	const char *name;
	pl_icon_kind icon;
	const char *description;
	/* Modules that mean this feature is present: exact names, or a
	 * prefix ending in '*'. Helper libraries are left out on purpose. */
	std::vector<const char *> modules;
};

/* Ordered so that a more specific entry claims a module before a broader
 * one (HDMI Sound before Built-in Sound). */
const std::vector<CatalogEntry> &catalog() {
	static const std::vector<CatalogEntry> entries = {
		/* Displays */
		{ "intel-graphics", "Intel Graphics", PL_ICON_EXT_DISPLAY,
			"Drives the built-in Intel graphics: the screen, external displays and "
			"hardware-accelerated drawing.", { "i915", "xe" } },
		{ "amd-graphics", "AMD Radeon Graphics", PL_ICON_EXT_DISPLAY,
			"Drives AMD Radeon graphics: the screen, external displays and "
			"hardware-accelerated drawing.", { "amdgpu", "radeon" } },
		{ "nvidia-graphics", "NVIDIA Graphics", PL_ICON_EXT_DISPLAY,
			"Drives NVIDIA graphics: the screen, external displays and "
			"hardware-accelerated drawing.", { "nouveau", "nvidia", "nvidia_drm" } },
		{ "graphics-switching", "Graphics Switching", PL_ICON_EXT_DISPLAY,
			"Switches between built-in and discrete graphics on computers that have "
			"both.", { "apple_gmux" } },
		{ "display-brightness", "Display Brightness", PL_ICON_EXT_DISPLAY,
			"Lets the brightness keys and Monitors control the built-in screen's "
			"backlight.", { "video", "apple_bl" } },

		/* Sound */
		{ "hdmi-sound", "HDMI Sound", PL_ICON_EXT_AUDIO,
			"Plays sound through a TV or display connected by HDMI or DisplayPort.",
			{ "snd_hda_codec_hdmi" } },
		{ "built-in-sound", "Built-in Sound", PL_ICON_EXT_AUDIO,
			"Plays sound through the built-in speakers and headphone jack, and records "
			"from the built-in microphone.", { "snd_hda_intel", "snd_hda_codec_*" } },
		{ "usb-sound", "USB Sound", PL_ICON_EXT_AUDIO,
			"Plays and records sound with USB speakers, headsets and audio "
			"interfaces.", { "snd_usb_audio" } },
		{ "midi", "MIDI", PL_ICON_EXT_AUDIO,
			"Connects music software to MIDI keyboards and synthesizers.",
			{ "snd_seq", "snd_seq_midi", "snd_rawmidi" } },
		{ "system-beep", "System Beep", PL_ICON_EXT_AUDIO,
			"The computer's simple built-in beeper.", { "pcspkr" } },

		/* Networking */
		{ "ethernet", "Ethernet", PL_ICON_EXT_ETHERNET,
			"Connects to wired networks through the Ethernet port.",
			{ "tg3", "sky2", "e1000e", "e1000", "igb", "r8169", "bnx2", "b44",
				"sungem", "atl1c", "alx", "bcmgenet" } },
		{ "usb-ethernet", "USB Ethernet Adapters", PL_ICON_EXT_ETHERNET,
			"Connects to wired networks through USB and Thunderbolt Ethernet adapters.",
			{ "r8152", "ax88179_178a", "asix", "cdc_ether", "cdc_ncm" } },
		{ "wifi", "Wi-Fi", PL_ICON_EXT_WIFI,
			"Connects to wireless networks.",
			{ "wl", "brcmfmac", "brcmsmac", "b43", "b43legacy", "iwlwifi", "ath9k",
				"ath10k_pci", "ath11k_pci", "mt7921e", "rtw88_*", "rtw89_*" } },
		{ "bluetooth", "Bluetooth", PL_ICON_EXT_BLUETOOTH,
			"Connects wireless keyboards, mice, trackpads, headphones and other "
			"Bluetooth devices.", { "bluetooth", "btusb", "hci_uart" } },
		{ "appletalk", "AppleTalk", PL_ICON_EXT_APPLETALK,
			"Talks to classic Macintosh computers and printers over AppleTalk.",
			{ "appletalk" } },
		{ "firewall", "Firewall", PL_ICON_EXT_SECURITY,
			"Filters network connections according to the firewall's rules.",
			{ "ip_tables", "ip6_tables", "nf_tables", "nf_conntrack" } },
		{ "network-sharing", "Network File Sharing", PL_ICON_EXT_ETHERNET,
			"Mounts shared folders from Windows (SMB) and NFS file servers.",
			{ "cifs", "nfs", "nfsv4" } },

		/* Input */
		{ "keyboard-function-keys", "Keyboard Function Keys", PL_ICON_EXT_INPUT,
			"Makes the fn key, media keys and Option and Command keys work on "
			"Macintosh keyboards.", { "hid_apple" } },
		{ "keyboard-mouse", "Keyboard & Mouse", PL_ICON_EXT_INPUT,
			"Works with USB and wireless keyboards, mice and game controllers.",
			{ "usbhid", "hid_generic", "hid_multitouch", "hid_logitech_dj" } },
		{ "trackpad", "Trackpad", PL_ICON_EXT_INPUT,
			"Works with built-in and wireless multi-touch trackpads.",
			{ "bcm5974", "applespi", "hid_magicmouse" } },
		{ "infrared-remote", "Infrared Remote", PL_ICON_EXT_INPUT,
			"Receives presses from an infrared remote control.", { "appleir" } },

		/* Storage */
		{ "internal-disks", "Internal Disks", PL_ICON_DISK,
			"Reads and writes the built-in hard disk or solid-state drive.",
			{ "ahci", "ata_piix", "pata_*" } },
		{ "nvme", "Flash Storage", PL_ICON_DISK,
			"Reads and writes built-in high-speed flash (NVMe) storage.", { "nvme" } },
		{ "usb-storage", "USB Disks", PL_ICON_DISK,
			"Reads and writes USB flash drives and external disks.",
			{ "usb_storage", "uas" } },
		{ "sd-card", "SD Card Reader", PL_ICON_DISK,
			"Reads and writes SD and SDXC memory cards.",
			{ "sdhci_pci", "rtsx_pci_sdmmc", "rtsx_usb_sdmmc" } },
		{ "optical-drive", "CD & DVD Drive", PL_ICON_DISK,
			"Reads CDs and DVDs, and writes discs on drives that can.", { "sr_mod" } },
		{ "linux-disks", "Linux Disks (ext4)", PL_ICON_DISK,
			"Reads and writes disks formatted for Linux, including the startup disk.",
			{ "ext4", "btrfs", "xfs" } },
		{ "windows-disks", "Windows & Camera Disks", PL_ICON_DISK,
			"Reads and writes disks formatted for Windows, cameras and most USB "
			"flash drives (FAT, exFAT and NTFS).", { "vfat", "exfat", "ntfs3" } },
		{ "mac-disks", "Mac OS Disks (HFS+)", PL_ICON_DISK,
			"Reads disks formatted for classic Mac OS and Mac OS X.",
			{ "hfsplus", "hfs" } },

		/* Connections */
		{ "usb", "USB", PL_ICON_EXT_USB,
			"Runs the USB ports, for everything from keyboards to disks.",
			{ "xhci_pci", "ehci_pci", "ohci_pci", "uhci_hcd" } },
		{ "thunderbolt", "Thunderbolt", PL_ICON_EXT_USB,
			"Runs the Thunderbolt ports and the devices connected to them.",
			{ "thunderbolt" } },
		{ "firewire", "FireWire", PL_ICON_EXT_USB,
			"Runs the FireWire ports, for older disks and cameras.",
			{ "firewire_ohci" } },
		{ "printing", "Printing", PL_ICON_EXT_PRINTMONITOR,
			"Sends documents to printers connected directly to this computer.",
			{ "usblp", "lp" } },
		{ "device-charging", "Fast Charging for Phones", PL_ICON_EXT_POWER,
			"Charges a connected phone or tablet faster over USB.",
			{ "apple_mfi_fastcharge" } },

		/* Camera */
		{ "camera", "Built-in Camera", PL_ICON_EXT_CAMERA,
			"Works with the built-in camera and USB webcams for video calls and "
			"photos.", { "uvcvideo", "facetimehd" } },

		/* Power and sensors */
		{ "fans-sensors", "Fans & Sensors", PL_ICON_EXT_SENSOR,
			"Reads the computer's temperature sensors and controls its fans and "
			"keyboard backlight.", { "applesmc" } },
		{ "light-sensor", "Ambient Light Sensor", PL_ICON_EXT_SENSOR,
			"Measures the room's light, so the screen can adjust its brightness.",
			{ "acpi_als" } },
		{ "processor-temperature", "Processor Temperature", PL_ICON_EXT_SENSOR,
			"Watches the processor's temperature and keeps it from overheating.",
			{ "coretemp", "x86_pkg_temp_thermal", "k10temp" } },
		{ "energy-saver", "Processor Energy Saver", PL_ICON_EXT_POWER,
			"Lets the processor save power when it isn't busy.",
			{ "intel_rapl_msr", "intel_powerclamp", "intel_cstate", "amd_pstate" } },
		{ "power-button", "Power & Sleep Buttons", PL_ICON_EXT_POWER,
			"Responds to the power button and to closing the lid.", { "button" } },
		{ "battery", "Battery", PL_ICON_EXT_POWER,
			"Reports the battery's charge and whether the power adapter is connected.",
			{ "battery", "ac", "sbs" } },

		/* Processor */
		{ "encryption", "Encryption Acceleration", PL_ICON_EXT_SECURITY,
			"Uses the processor's built-in instructions to encrypt and check data "
			"faster.", { "aesni_intel", "ghash_clmulni_intel", "sha256_ssse3",
				"sha1_ssse3", "sha512_ssse3" } },
		{ "virtualization", "Virtualization", PL_ICON_EXT_CHIP,
			"Lets virtual machines use the processor directly, so they run at "
			"nearly full speed.", { "kvm_intel", "kvm_amd" } },
		{ "firmware-settings", "Firmware Settings", PL_ICON_EXT_CHIP,
			"Reads and saves the computer's firmware settings, such as the startup "
			"disk.", { "efivarfs" } },
	};
	return entries;
}

QString normalize(QString module) {
	return module.replace('-', '_');
}

bool matches(const char *pattern, const QString &module) {
	const QString p = QString::fromLatin1(pattern);
	return p.endsWith('*') ? module.startsWith(p.chopped(1)) : module == p;
}

const CatalogEntry *entryFor(const QString &module) {
	for (const CatalogEntry &entry : catalog()) {
		for (const char *pattern : entry.modules) {
			if (matches(pattern, module)) {
				return &entry;
			}
		}
	}
	return nullptr;
}

ExtensionEntry fromCatalog(const CatalogEntry &entry) {
	ExtensionEntry out;
	out.key = QString::fromLatin1(entry.key);
	out.name = QString::fromUtf8(entry.name);
	out.icon = entry.icon;
	out.description = QString::fromUtf8(entry.description);
	return out;
}

} // namespace

std::vector<ExtensionEntry> curatedExtensions(const QStringList &loaded,
		const QStringList &startup) {
	QStringList startupModules;
	for (const QString &module : startup) {
		startupModules << normalize(module);
	}
	std::vector<ExtensionEntry> out;
	for (const QString &raw : loaded) {
		const QString module = normalize(raw);
		const CatalogEntry *entry = entryFor(module);
		if (!entry) {
			continue;
		}
		auto it = std::find_if(out.begin(), out.end(), [entry](const ExtensionEntry &e) {
			return e.key == QLatin1String(entry->key);
		});
		if (it == out.end()) {
			out.push_back(fromCatalog(*entry));
			it = out.end() - 1;
		}
		if (!it->modules.contains(module)) {
			it->modules << module;
		}
		it->startup = it->startup || startupModules.contains(module);
	}
	for (ExtensionEntry &entry : out) {
		entry.modules.sort();
	}
	std::sort(out.begin(), out.end(), [](const ExtensionEntry &a, const ExtensionEntry &b) {
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});
	return out;
}

ExtensionEntry extensionEntry(const QString &key) {
	for (const CatalogEntry &entry : catalog()) {
		if (key == QLatin1String(entry.key)) {
			return fromCatalog(entry);
		}
	}
	return {};
}

pl_icon_kind extensionModuleIcon(const QString &module) {
	const CatalogEntry *entry = entryFor(normalize(module));
	return entry ? entry->icon : PL_ICON_EXT_GENERIC;
}
