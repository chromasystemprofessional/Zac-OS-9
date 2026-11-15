#include "Processes.r"

/* Background-only guest agent: it must keep running while applications come
   forward. 2 MB for native hook state and small bookkeeping; window pixels
   live in GWorlds allocated from the system zone. */
resource 'SIZE' (-1) {
	reserved, acceptSuspendResumeEvents, reserved, canBackground,
	doesActivateOnFGSwitch, backgroundAndForeground, dontGetFrontClicks,
	ignoreChildDiedEvents, is32BitCompatible, notHighLevelEventAware,
	onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
	reserved, reserved, reserved,
	2048 * 1024, 2048 * 1024
};
