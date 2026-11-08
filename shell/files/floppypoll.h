#pragma once

#include <QString>
#include <functional>

/*
 * PC floppy drives cannot report media changes through UDisks: it
 * deliberately assumes a floppy has media so it never spins the drive.
 * This poller watches the drive's change line with the floppy ioctls and
 * only reads a sector when the generation changes. `changed` runs on the
 * GUI thread with the drive's /dev path and whether a disk is present.
 */
void floppyPollStart(std::function<void(const QString &device, bool present)> changed);
