#include "mediaeject.h"

#include <cassert>

static void expect(MediaKind kind, bool mounted, bool ejectable, bool loopImage,
		EjectAction action, bool safeToRemove = false) {
	const EjectPlan plan = mediaEjectPlan(kind, mounted, ejectable, loopImage);
	assert(plan.action == action);
	assert(plan.safeToRemove == safeToRemove);
}

int main() {
	/* A mounted image's filesystem goes first, then its loop device. */
	expect(MediaKind::LoopImage, true, false, true,
		EjectAction::UnmountThenLoopDelete);
	expect(MediaKind::LoopImage, false, false, true,
		EjectAction::UnmountThenLoopDelete);
	/* Even if the kind says disk, a loop backing image is an image. */
	expect(MediaKind::Disk, true, false, true,
		EjectAction::UnmountThenLoopDelete);

	/* Data discs: unmount, then open the tray. */
	expect(MediaKind::OpticalData, true, true, false,
		EjectAction::UnmountThenDriveEject);
	expect(MediaKind::OpticalVideo, true, true, false,
		EjectAction::UnmountThenDriveEject);
	expect(MediaKind::OpticalData, false, true, false,
		EjectAction::DriveEjectOnly);
	expect(MediaKind::OpticalData, true, false, false,
		EjectAction::UnmountOnly);
	expect(MediaKind::OpticalData, false, false, false, EjectAction::None);

	/* Audio and blank discs have no filesystem; the drive ejects them. */
	expect(MediaKind::OpticalAudio, true, true, false,
		EjectAction::DriveEjectOnly);
	expect(MediaKind::OpticalBlank, false, true, false,
		EjectAction::DriveEjectOnly);
	expect(MediaKind::OpticalAudio, true, false, false,
		EjectAction::UnmountOnly);

	/* A floppy has no motorised eject; after unmounting it is safe. */
	expect(MediaKind::Floppy, true, false, false,
		EjectAction::UnmountOnly, true);
	expect(MediaKind::Floppy, true, true, false,
		EjectAction::UnmountOnly, true);

	/* Zip/Jaz are removable cartridges like a small drive. */
	expect(MediaKind::Zip, true, true, false,
		EjectAction::UnmountThenDriveEject);
	expect(MediaKind::Jaz, false, true, false, EjectAction::DriveEjectOnly);
	expect(MediaKind::Zip, false, false, false, EjectAction::None);

	/* Ordinary storage: unmount, and eject only when the drive can. */
	expect(MediaKind::Usb, true, false, false, EjectAction::UnmountOnly);
	expect(MediaKind::Disk, true, false, false, EjectAction::UnmountOnly);
	expect(MediaKind::Usb, true, true, false,
		EjectAction::UnmountThenDriveEject);
	return 0;
}
