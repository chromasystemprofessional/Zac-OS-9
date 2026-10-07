#include "Dialogs.r"
#include "Processes.r"

resource 'ALRT' (128) {
	{ 100, 120, 230, 480 },
	128,
	{ OK, visible, sound1, OK, visible, sound1,
	  OK, visible, sound1, OK, visible, sound1 },
	centerMainScreen
};
resource 'DITL' (128) {
	{
		{ 88, 260, 110, 340 }, Button { enabled, "OK" };
		{ 15, 15, 75, 340 }, StaticText { disabled, "^0" };
	}
};
resource 'ALRT' (129) {
	{ 100, 120, 230, 480 },
	129,
	{ OK, visible, silent, OK, visible, silent,
	  OK, visible, silent, OK, visible, silent },
	centerMainScreen
};
resource 'DITL' (129) {
	{
		{ 88, 260, 110, 340 }, Button { enabled, "Cancel" };
		{ 88, 170, 110, 250 }, Button { enabled, "Close" };
		{ 15, 15, 75, 340 }, StaticText { disabled,
			"Close this test window? Choose Cancel to test a cancelled close request." };
	}
};
resource 'SIZE' (-1) {
	reserved, acceptSuspendResumeEvents, reserved, canBackground,
	doesActivateOnFGSwitch, backgroundAndForeground, dontGetFrontClicks,
	ignoreChildDiedEvents, is32BitCompatible, notHighLevelEventAware,
	onlyLocalHLEvents, notStationeryAware, dontUseTextEditServices,
	reserved, reserved, reserved,
	1024 * 1024, 1024 * 1024
};
