/*
 * setos.efi: tells a dual-GPU MacBook Pro's firmware that macOS is starting,
 * so it leaves the Intel graphics on, then starts ZacOS's own boot loader.
 *
 * Apple's firmware hides the Intel GPU from anything but macOS. macOS asks
 * for it through Apple's "set OS" protocol before the GPUs are set up;
 * Linux's own EFI stub makes the same call, but too late on the
 * MacBookPro11,3. This runs first, from its own firmware boot entry.
 *
 * Whatever happens with the call, it goes on to start \EFI\ZacOS9\shimx64.efi
 * on the same disk: it never leaves the computer stuck. See docs/hardware.md.
 */
#include <efi.h>
#include <efilib.h>

/* The protocol, as Linux's EFI stub declares it (drivers/firmware/efi/libstub/x86-stub.c). */
static EFI_GUID apple_set_os_guid = { 0xc5c5da95, 0x7d5c, 0x45e6, { 0xb2, 0xf1, 0x3f, 0xd5, 0x2b, 0xb1, 0x00, 0x77 } };

typedef struct {
	UINT64 version;
	EFI_STATUS (EFIAPI *set_os_version)(const CHAR8 *version);
	EFI_STATUS (EFIAPI *set_os_vendor)(const CHAR8 *vendor);
} APPLE_SET_OS_PROTOCOL;

static CHAR16 next_loader[] = L"\\EFI\\ZacOS9\\shimx64.efi";

static void set_os(void) {
	APPLE_SET_OS_PROTOCOL *proto = NULL;
	EFI_STATUS status = uefi_call_wrapper(BS->LocateProtocol, 3, &apple_set_os_guid, NULL, (void **)&proto);
	if (EFI_ERROR(status) || !proto) {
		Print(L"setos: no Apple set_os protocol (%r)\n", status);
		return;
	}
	/* The order macOS and Linux use: vendor (version 2 and later), then version. */
	if (proto->version >= 2) {
		status = uefi_call_wrapper(proto->set_os_vendor, 1, (const CHAR8 *)"Apple Inc.");
		if (EFI_ERROR(status)) {
			Print(L"setos: set_os_vendor failed (%r)\n", status);
		}
	}
	if (proto->version > 0) {
		status = uefi_call_wrapper(proto->set_os_version, 1, (const CHAR8 *)"Mac OS X 10.9");
		if (EFI_ERROR(status)) {
			Print(L"setos: set_os_version failed (%r)\n", status);
		}
	}
}

/* Not EFIAPI: gnu-efi's start-up code calls it the System V way. */
EFI_STATUS efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *system_table) {
	InitializeLib(image, system_table);
	set_os();

	/* The boot loader on the disk this program was started from. */
	EFI_LOADED_IMAGE *self = NULL;
	EFI_STATUS status = uefi_call_wrapper(BS->HandleProtocol, 3, image, &LoadedImageProtocol, (void **)&self);
	if (EFI_ERROR(status)) {
		Print(L"setos: can't find its own disk (%r)\n", status);
		return status;
	}
	EFI_DEVICE_PATH *path = FileDevicePath(self->DeviceHandle, next_loader);
	EFI_HANDLE next = NULL;
	status = uefi_call_wrapper(BS->LoadImage, 6, FALSE, image, path, NULL, 0, &next);
	if (EFI_ERROR(status)) {
		Print(L"setos: can't load %s (%r)\n", next_loader, status);
		uefi_call_wrapper(BS->Stall, 1, 5 * 1000 * 1000);
		return status; /* back to the firmware, which tries the next boot entry */
	}
	return uefi_call_wrapper(BS->StartImage, 3, next, NULL, NULL);
}
