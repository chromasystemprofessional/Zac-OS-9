#pragma once

/*
 * Shutting down or restarting the computer from outside a login session's
 * Special menu: the login window (shell/greeter/) and the installer
 * (shell/installer/). `systemctl poweroff|reboot`, which asks logind:
 * /usr/bin's, so on everyone's PATH (/usr/sbin/reboot isn't on a user's,
 * issue #38). The login window's own user may (polkit,
 * share/polkit/50-zacos9-power.rules); a program that holds the
 * shutdown off (an update installing) is still waited for.
 *
 * Returns an empty string once logind has taken it, else why not, in a
 * sentence (logind's own reason when it gives one). The windows keep
 * being drawn while it waits.
 */

#include <QString>

enum class Power { ShutDown, Restart };

QString systemPower(Power what);
