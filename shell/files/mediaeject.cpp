#include "mediaeject.h"

EjectPlan mediaEjectPlan(MediaKind kind, bool mounted, bool ejectable,
		bool loopImage) {
	if (loopImage || kind == MediaKind::LoopImage) {
		return { EjectAction::UnmountThenLoopDelete, false };
	}
	switch (kind) {
	case MediaKind::LoopImage:
		return { EjectAction::UnmountThenLoopDelete, false };
	case MediaKind::Floppy:
		return { EjectAction::UnmountOnly, true };
	case MediaKind::OpticalAudio:
	case MediaKind::OpticalBlank:
		/* A disc with no filesystem has nothing to unmount first; the
		 * drive's own eject also releases any GIO mount it has. */
		if (ejectable) {
			return { EjectAction::DriveEjectOnly, false };
		}
		return { mounted ? EjectAction::UnmountOnly : EjectAction::None, false };
	case MediaKind::OpticalData:
	case MediaKind::OpticalVideo:
	case MediaKind::Zip:
	case MediaKind::Jaz:
	case MediaKind::Usb:
	case MediaKind::Disk:
		if (ejectable) {
			return { mounted ? EjectAction::UnmountThenDriveEject
				: EjectAction::DriveEjectOnly, false };
		}
		return { mounted ? EjectAction::UnmountOnly : EjectAction::None, false };
	}
	return {};
}
