/*
 * Classic Open Helper: open applications requested by zacos9-classic.
 *
 * The patched emulator exposes a narrow guest bridge opcode. This helper
 * polls that bridge from Startup Items. The emulator reads the host request
 * queue and writes the acknowledgement, so the guest needs no network or
 * ExtFS request view.
 *
 * Bridge block layout (pointer passed in A0):
 *   +0 command (1 poll, 2 acknowledge)
 *   +4 result
 *   +8 request id
 *  +12 path (200 bytes including the terminator, colon-separated below the
 *      Unix volume)
 * +212 acknowledge status (0 ok, 1 error)
 * +216 acknowledge error code
 *
 * Built with Retro68; see README.md. This is original code: no Apple
 * software is used or shipped.
 */

#include <Quickdraw.h>
#include <Dialogs.h>
#include <Events.h>
#include <Menus.h>
#include <Files.h>
#include <Processes.h>
#include <Errors.h>
#include <stdio.h>
#include <string.h>

enum {
	PATH_MAX = 200,
	SCAN_TICKS = 30,
	ROOT_DIR_ID = 2,	/* fsRtDirID */
	BRIDGE_OPCODE = 0x18000038UL	/* OP_CLASSIC_BRIDGE (53) + 3 */
};

typedef struct {
	UInt32 command;
	UInt32 result;
	UInt32 id;
	char path[PATH_MAX];
	UInt32 status;
	UInt32 code;
} BridgeBlock;

static GrafPort helper_port;
static MenuHandle helper_menu;
static short unix_vref;
static unsigned long last_scan;

#ifdef HELPER_DEBUG
static void debug_note(const char *text, long a, long b) {
	Str255 name;
	short ref;
	long length;
	char line[160];
	OSErr created;
	if (unix_vref == 0) {
		return;
	}
	snprintf(line, sizeof(line), "%s a=%ld b=%ld\n", text, a, b);
	snprintf((char *)name + 1, sizeof(name) - 1, "ClassicOpenDebug.txt");
	name[0] = (unsigned char)strlen((const char *)name + 1);
	created = HCreate(unix_vref, ROOT_DIR_ID, name, 'ZcOH', 'TEXT');
	if (created != noErr && created != dupFNErr) {
		return;
	}
	if (HOpenDF(unix_vref, ROOT_DIR_ID, name, fsRdWrPerm, &ref) != noErr) {
		return;
	}
	SetFPos(ref, fsFromLEOF, 0);
	length = (long)strlen(line);
	FSWrite(ref, &length, line);
	FSClose(ref);
	FlushVol(NULL, unix_vref);
}
#else
static void debug_note(const char *text, long a, long b) {
	(void)text;
	(void)a;
	(void)b;
}
#endif

static void make_pstring(Str255 out, const char *text) {
	size_t length = strlen(text);
	if (length > 255) {
		length = 255;
	}
	out[0] = (unsigned char)length;
	memcpy(out + 1, text, length);
}

/* The Mac sees the host home folder as a volume named "Unix". */
static short find_unix_volume(void) {
	HParamBlockRec pb;
	Str255 name;
	short index;
	for (index = 1; index <= 64; index++) {
		memset(&pb, 0, sizeof(pb));
		pb.volumeParam.ioNamePtr = name;
		pb.volumeParam.ioVolIndex = index;
		if (PBHGetVInfoSync(&pb) != noErr) {
			break;
		}
		if (name[0] == 4 && memcmp(name + 1, "Unix", 4) == 0) {
			return pb.volumeParam.ioVRefNum;
		}
	}
	return 0;
}

/* The ExtFS volume does not resolve multi-component FSMakeFSSpec paths here,
   so each path component is looked up on its own. */
static OSErr folder_in(short vref, long parent, const char *name, long *dir_id) {
	FSSpec spec;
	CInfoPBRec pb;
	Str255 p;
	OSErr error;
	make_pstring(p, name);
	error = FSMakeFSSpec(vref, parent, p, &spec);
	if (error != noErr) {
		return error;
	}
	memset(&pb, 0, sizeof(pb));
	pb.hFileInfo.ioVRefNum = spec.vRefNum;
	pb.hFileInfo.ioDirID = spec.parID;
	pb.hFileInfo.ioNamePtr = spec.name;
	pb.hFileInfo.ioFDirIndex = 0;
	error = PBGetCatInfoSync(&pb);
	if (error != noErr) {
		return error;
	}
	*dir_id = pb.dirInfo.ioDrDirID;
	return noErr;
}

static OSErr resolve_app(short vref, const char *path, FSSpec *out) {
	const char *part = path;
	long parent = ROOT_DIR_ID;
	for (;;) {
		const char *colon = strchr(part, ':');
		size_t length = colon ? (size_t)(colon - part) : strlen(part);
		Str255 name;
		char component[256];
		OSErr error;
		if (length == 0 || length >= sizeof(component)) {
			return paramErr;
		}
		memcpy(component, part, length);
		component[length] = 0;
		make_pstring(name, component);
		if (!colon) {
			return FSMakeFSSpec(vref, parent, name, out);
		}
		error = folder_in(vref, parent, component, &parent);
		if (error != noErr) {
			return error;
		}
		part = colon + 1;
	}
}

static OSErr open_app(const char *path) {
	FSSpec app;
	FInfo info;
	LaunchParamBlockRec launch;
	OSErr error;
	if (unix_vref == 0) {
		unix_vref = find_unix_volume();
	}
	if (unix_vref == 0) {
		return fnfErr;
	}
	error = resolve_app(unix_vref, path, &app);
	if (error == noErr) {
		error = FSpGetFInfo(&app, &info);
	}
	if (error == noErr && info.fdType != 'APPL') {
		error = paramErr;
	}
	if (error == noErr) {
		memset(&launch, 0, sizeof(launch));
		launch.launchBlockID = extendedBlock;
		launch.launchEPBLength = extendedBlockLen;
		launch.launchControlFlags = launchContinue;
		launch.launchAppSpec = &app;
		error = LaunchApplication(&launch);
		if (error == noErr) {
			SetFrontProcess(&launch.launchProcessSN);
		}
	}
	return error;
}

static UInt32 bridge_call(BridgeBlock *block) {
	register UInt32 d0 asm("r8") = 0;
	register UInt32 a0 asm("r16") = (UInt32)block;
	asm volatile(".long %c2" : "+r"(d0), "+r"(a0) : "i"(BRIDGE_OPCODE) : "memory");
	return d0;
}

static void poll_bridge(void) {
	BridgeBlock block;
	OSErr error;
	memset(&block, 0, sizeof(block));
	block.command = 1;
	if (bridge_call(&block) != 1) {
		return;
	}
	debug_note("request", (long)block.id, (long)strlen(block.path));
	error = open_app(block.path);
	block.command = 2;
	block.status = error == noErr ? 0 : 1;
	block.code = (UInt32)error;
	bridge_call(&block);
	debug_note("opened", (long)error, (long)block.id);
}

int main(void) {
	EventRecord event;
	static const unsigned char menu_title[] = "\pClassic Open";
	static const unsigned char menu_items[] = "\pQuit/Q";
	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(NULL);
	OpenPort(&helper_port);
	SetPort(&helper_port);
	InitCursor();
	helper_menu = NewMenu(128, menu_title);
	if (helper_menu) {
		AppendMenu(helper_menu, menu_items);
		InsertMenu(helper_menu, 0);
		DrawMenuBar();
	}
	unix_vref = find_unix_volume();
	debug_note("helper start", (long)unix_vref, 0);
	last_scan = TickCount();
	for (;;) {
		if (TickCount() - last_scan >= SCAN_TICKS) {
			last_scan = TickCount();
			if (unix_vref == 0) {
				unix_vref = find_unix_volume();
			}
			poll_bridge();
		}
		if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
			if (event.what == mouseDown) {
				WindowPtr window;
				if (FindWindow(event.where, &window) == inMenuBar) {
					long command = MenuSelect(event.where);
					if ((command >> 16) == 128 && (command & 0xffff) == 1) {
						break;
					}
					HiliteMenu(0);
				}
			} else if ((event.what == keyDown || event.what == autoKey) &&
					(event.modifiers & cmdKey)) {
				char key = event.message & charCodeMask;
				if (key == 'q' || key == 'Q') {
					break;
				}
			}
		}
	}
	if (helper_menu) {
		DeleteMenu(128);
		DisposeMenu(helper_menu);
	}
	return 0;
}
