/*
 * The curated extension catalog: familiar names, icons and plain
 * descriptions for the kernel modules people would recognize, one entry per
 * feature. Shared by the Extensions Manager (shell/finder/extensioncatalog),
 * the startup parade (boot/zacos9-parade, compositor/src/startup.c) and the
 * boot splash's icon sheet (boot/make-splash.c).
 */
#include "extensions.h"

#include <string.h>

/* Ordered so that a more specific entry claims a module before a broader
 * one (HDMI Sound before Built-in Sound). Modules are exact names, or a
 * prefix ending in '*'. Helper libraries are left out on purpose. */
const struct pl_extension pl_extensions[] = {
	/* Displays */
	{ "intel-graphics", "Intel Graphics", PL_ICON_EXT_DISPLAY,
		"Drives the built-in Intel graphics: the screen, external displays and "
		"hardware-accelerated drawing.", (const char *const[]){ "i915", "xe", NULL } },
	{ "amd-graphics", "AMD Radeon Graphics", PL_ICON_EXT_DISPLAY,
		"Drives AMD Radeon graphics: the screen, external displays and "
		"hardware-accelerated drawing.", (const char *const[]){ "amdgpu", "radeon", NULL } },
	{ "nvidia-graphics", "NVIDIA Graphics", PL_ICON_EXT_DISPLAY,
		"Drives NVIDIA graphics: the screen, external displays and "
		"hardware-accelerated drawing.", (const char *const[]){ "nouveau", "nvidia", "nvidia_drm", NULL } },
	{ "graphics-switching", "Graphics Switching", PL_ICON_EXT_DISPLAY,
		"Switches between built-in and discrete graphics on computers that have "
		"both.", (const char *const[]){ "apple_gmux", NULL } },
	{ "display-brightness", "Display Brightness", PL_ICON_EXT_DISPLAY,
		"Lets the brightness keys and Monitors control the built-in screen's "
		"backlight.", (const char *const[]){ "video", "apple_bl", NULL } },

	/* Sound */
	{ "hdmi-sound", "HDMI Sound", PL_ICON_EXT_AUDIO,
		"Plays sound through a TV or display connected by HDMI or DisplayPort.",
		(const char *const[]){ "snd_hda_codec_hdmi", NULL } },
	{ "built-in-sound", "Built-in Sound", PL_ICON_EXT_AUDIO,
		"Plays sound through the built-in speakers and headphone jack, and records "
		"from the built-in microphone.", (const char *const[]){ "snd_hda_intel", "snd_hda_codec_*", NULL } },
	{ "usb-sound", "USB Sound", PL_ICON_EXT_AUDIO,
		"Plays and records sound with USB speakers, headsets and audio "
		"interfaces.", (const char *const[]){ "snd_usb_audio", NULL } },
	{ "midi", "MIDI", PL_ICON_EXT_AUDIO,
		"Connects music software to MIDI keyboards and synthesizers.",
		(const char *const[]){ "snd_seq", "snd_seq_midi", "snd_rawmidi", NULL } },
	{ "system-beep", "System Beep", PL_ICON_EXT_AUDIO,
		"The computer's simple built-in beeper.", (const char *const[]){ "pcspkr", NULL } },

	/* Networking */
	{ "ethernet", "Ethernet", PL_ICON_EXT_ETHERNET,
		"Connects to wired networks through the Ethernet port.",
		(const char *const[]){ "tg3", "sky2", "e1000e", "e1000", "igb", "r8169", "bnx2", "b44",
			"sungem", "atl1c", "alx", "bcmgenet", NULL } },
	{ "usb-ethernet", "USB Ethernet Adapters", PL_ICON_EXT_ETHERNET,
		"Connects to wired networks through USB and Thunderbolt Ethernet adapters.",
		(const char *const[]){ "r8152", "ax88179_178a", "asix", "cdc_ether", "cdc_ncm", NULL } },
	{ "wifi", "Wi-Fi", PL_ICON_EXT_WIFI,
		"Connects to wireless networks.",
		(const char *const[]){ "wl", "brcmfmac", "brcmsmac", "b43", "b43legacy", "iwlwifi", "ath9k",
			"ath10k_pci", "ath11k_pci", "mt7921e", "rtw88_*", "rtw89_*", NULL } },
	{ "bluetooth", "Bluetooth", PL_ICON_EXT_BLUETOOTH,
		"Connects wireless keyboards, mice, trackpads, headphones and other "
		"Bluetooth devices.", (const char *const[]){ "bluetooth", "btusb", "hci_uart", NULL } },
	{ "appletalk", "AppleTalk", PL_ICON_EXT_APPLETALK,
		"Talks to classic Macintosh computers and printers over AppleTalk.",
		(const char *const[]){ "appletalk", NULL } },
	{ "firewall", "Firewall", PL_ICON_EXT_SECURITY,
		"Filters network connections according to the firewall's rules.",
		(const char *const[]){ "ip_tables", "ip6_tables", "nf_tables", "nf_conntrack", NULL } },
	{ "network-sharing", "Network File Sharing", PL_ICON_EXT_ETHERNET,
		"Mounts shared folders from Windows (SMB) and NFS file servers.",
		(const char *const[]){ "cifs", "nfs", "nfsv4", NULL } },

	/* Input */
	{ "keyboard-function-keys", "Keyboard Function Keys", PL_ICON_EXT_INPUT,
		"Makes the fn key, media keys and Option and Command keys work on "
		"Macintosh keyboards.", (const char *const[]){ "hid_apple", NULL } },
	{ "keyboard-mouse", "Keyboard & Mouse", PL_ICON_EXT_INPUT,
		"Works with USB and wireless keyboards, mice and game controllers.",
		(const char *const[]){ "usbhid", "hid_generic", "hid_multitouch", "hid_logitech_dj", NULL } },
	{ "trackpad", "Trackpad", PL_ICON_EXT_INPUT,
		"Works with built-in and wireless multi-touch trackpads.",
		(const char *const[]){ "bcm5974", "applespi", "hid_magicmouse", NULL } },
	{ "infrared-remote", "Infrared Remote", PL_ICON_EXT_INPUT,
		"Receives presses from an infrared remote control.", (const char *const[]){ "appleir", NULL } },

	/* Storage */
	{ "internal-disks", "Internal Disks", PL_ICON_DISK,
		"Reads and writes the built-in hard disk or solid-state drive.",
		(const char *const[]){ "ahci", "ata_piix", "pata_*", NULL } },
	{ "nvme", "Flash Storage", PL_ICON_DISK,
		"Reads and writes built-in high-speed flash (NVMe) storage.", (const char *const[]){ "nvme", NULL } },
	{ "usb-storage", "USB Disks", PL_ICON_DISK,
		"Reads and writes USB flash drives and external disks.",
		(const char *const[]){ "usb_storage", "uas", NULL } },
	{ "sd-card", "SD Card Reader", PL_ICON_DISK,
		"Reads and writes SD and SDXC memory cards.",
		(const char *const[]){ "sdhci_pci", "rtsx_pci_sdmmc", "rtsx_usb_sdmmc", NULL } },
	{ "optical-drive", "CD & DVD Drive", PL_ICON_DISK,
		"Reads CDs and DVDs, and writes discs on drives that can.", (const char *const[]){ "sr_mod", NULL } },
	{ "linux-disks", "Linux Disks (ext4)", PL_ICON_DISK,
		"Reads and writes disks formatted for Linux, including the startup disk.",
		(const char *const[]){ "ext4", "btrfs", "xfs", NULL } },
	{ "windows-disks", "Windows & Camera Disks", PL_ICON_DISK,
		"Reads and writes disks formatted for Windows, cameras and most USB "
		"flash drives (FAT, exFAT and NTFS).", (const char *const[]){ "vfat", "exfat", "ntfs3", NULL } },
	{ "mac-disks", "Mac OS Disks (HFS+)", PL_ICON_DISK,
		"Reads disks formatted for classic Mac OS and Mac OS X.",
		(const char *const[]){ "hfsplus", "hfs", NULL } },

	/* Connections */
	{ "usb", "USB", PL_ICON_EXT_USB,
		"Runs the USB ports, for everything from keyboards to disks.",
		(const char *const[]){ "xhci_pci", "ehci_pci", "ohci_pci", "uhci_hcd", NULL } },
	{ "thunderbolt", "Thunderbolt", PL_ICON_EXT_USB,
		"Runs the Thunderbolt ports and the devices connected to them.",
		(const char *const[]){ "thunderbolt", NULL } },
	{ "firewire", "FireWire", PL_ICON_EXT_USB,
		"Runs the FireWire ports, for older disks and cameras.",
		(const char *const[]){ "firewire_ohci", NULL } },
	{ "printing", "Printing", PL_ICON_EXT_PRINTMONITOR,
		"Sends documents to printers connected directly to this computer.",
		(const char *const[]){ "usblp", "lp", NULL } },
	{ "device-charging", "Fast Charging for Phones", PL_ICON_EXT_POWER,
		"Charges a connected phone or tablet faster over USB.",
		(const char *const[]){ "apple_mfi_fastcharge", NULL } },

	/* Camera */
	{ "camera", "Built-in Camera", PL_ICON_EXT_CAMERA,
		"Works with the built-in camera and USB webcams for video calls and "
		"photos.", (const char *const[]){ "uvcvideo", "facetimehd", NULL } },

	/* Power and sensors */
	{ "fans-sensors", "Fans & Sensors", PL_ICON_EXT_SENSOR,
		"Reads the computer's temperature sensors and controls its fans and "
		"keyboard backlight.", (const char *const[]){ "applesmc", NULL } },
	{ "light-sensor", "Ambient Light Sensor", PL_ICON_EXT_SENSOR,
		"Measures the room's light, so the screen can adjust its brightness.",
		(const char *const[]){ "acpi_als", NULL } },
	{ "processor-temperature", "Processor Temperature", PL_ICON_EXT_SENSOR,
		"Watches the processor's temperature and keeps it from overheating.",
		(const char *const[]){ "coretemp", "x86_pkg_temp_thermal", "k10temp", NULL } },
	{ "energy-saver", "Processor Energy Saver", PL_ICON_EXT_POWER,
		"Lets the processor save power when it isn't busy.",
		(const char *const[]){ "intel_rapl_msr", "intel_powerclamp", "intel_cstate", "amd_pstate", NULL } },
	{ "power-button", "Power & Sleep Buttons", PL_ICON_EXT_POWER,
		"Responds to the power button and to closing the lid.", (const char *const[]){ "button", NULL } },
	{ "battery", "Battery", PL_ICON_EXT_POWER,
		"Reports the battery's charge and whether the power adapter is connected.",
		(const char *const[]){ "battery", "ac", "sbs", NULL } },

	/* Processor */
	{ "encryption", "Encryption Acceleration", PL_ICON_EXT_SECURITY,
		"Uses the processor's built-in instructions to encrypt and check data "
		"faster.", (const char *const[]){ "aesni_intel", "ghash_clmulni_intel", "sha256_ssse3",
			"sha1_ssse3", "sha512_ssse3", NULL } },
	{ "virtualization", "Virtualization", PL_ICON_EXT_CHIP,
		"Lets virtual machines use the processor directly, so they run at "
		"nearly full speed.", (const char *const[]){ "kvm_intel", "kvm_amd", NULL } },
	{ "firmware-settings", "Firmware Settings", PL_ICON_EXT_CHIP,
		"Reads and saves the computer's firmware settings, such as the startup "
		"disk.", (const char *const[]){ "efivarfs", NULL } },
};
const int pl_n_extensions = (int)(sizeof(pl_extensions) / sizeof(pl_extensions[0]));

static bool matches(const char *pattern, const char *module) {
	const size_t n = strlen(pattern);
	if (n && pattern[n - 1] == '*') {
		return strncmp(pattern, module, n - 1) == 0;
	}
	return strcmp(pattern, module) == 0;
}

const struct pl_extension *pl_extension_for_module(const char *module) {
	char name[64];
	size_t i = 0;
	for (; module[i] && i < sizeof(name) - 1; i++) {
		name[i] = module[i] == '-' ? '_' : module[i];
	}
	name[i] = 0;
	for (int e = 0; e < pl_n_extensions; e++) {
		for (const char *const *m = pl_extensions[e].modules; *m; m++) {
			if (matches(*m, name)) {
				return &pl_extensions[e];
			}
		}
	}
	return NULL;
}

const struct pl_extension *pl_extension_find(const char *key) {
	for (int e = 0; key && e < pl_n_extensions; e++) {
		if (strcmp(pl_extensions[e].key, key) == 0) {
			return &pl_extensions[e];
		}
	}
	return NULL;
}
