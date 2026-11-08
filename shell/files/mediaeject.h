#pragma once

#include "mediakind.h"

/*
 * What Put Away should do to a local volume, kept separate from GIO so the
 * decision table is testable: an optical drive's tray is ejected, a floppy
 * is only unmounted, and a mounted image's loop device is deleted after its
 * filesystem is gone.
 */

enum class EjectAction {
	None,
	UnmountOnly,
	UnmountThenDriveEject,
	DriveEjectOnly,
	UnmountThenLoopDelete,
};

struct EjectPlan {
	EjectAction action = EjectAction::None;
	/* A floppy has no motorised eject; after unmounting, it is safe to
	 * pull the disk out. */
	bool safeToRemove = false;
};

EjectPlan mediaEjectPlan(MediaKind kind, bool mounted, bool ejectable,
	bool loopImage);
