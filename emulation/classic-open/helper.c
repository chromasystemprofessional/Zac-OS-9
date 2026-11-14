/*
 * Classic Open Helper: open applications requested by zacos9-classic.
 *
 * The host writes one small request file per application into
 * Unix:zacos9-classic:requests. This helper runs from the Mac's
 * System Folder:Startup Items, reads each request, checks that the target is
 * an application on the shared Unix volume, launches it, writes an
 * acknowledgement into Unix:zacos9-classic:acks and deletes the request.
 *
 * Request and acknowledgement format, version 1:
 *
 *   version=1
 *   id=<letters, digits and hyphens>
 *   path=<colon-separated path below the Unix volume>
 *
 *   version=1
 *   id=<same id>
 *   status=ok | status=error
 *   code=<Mac error, for status=error>
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
	REQUEST_MAX = 512,
	ID_MAX = 64,
	PATH_MAX = 200,
	SCAN_TICKS = 60,
	ROOT_DIR_ID = 2,	/* fsRtDirID */
	DIR_ATTRIBUTE = 0x10
};

static GrafPort helper_port;
static MenuHandle helper_menu;
static short unix_vref;
static long requests_dir;
static long acks_dir;
static int ready_written;
static unsigned long last_scan;
static char last_id[ID_MAX];
static long queue_dir;
static unsigned long last_sequence;
static int sequence_loaded;

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

/* The directory ID of a folder in another folder. The ExtFS volume does
   not resolve multi-component FSMakeFSSpec paths here, so each path
   component is looked up on its own. */
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

/* Resolve a colon-separated request path below the Unix volume root. */
static OSErr resolve_app(short vref, const char *path, FSSpec *out) {
	const char *part = path;
	long parent = ROOT_DIR_ID;
	for (;;) {
		const char *colon = strchr(part, ':');
		size_t length = colon ? (size_t)(colon - part) : strlen(part);
		Str255 name;
		char component[64];
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

static int valid_id(const char *id) {
	size_t i, length = strlen(id);
	if (length == 0 || length >= ID_MAX) {
		return 0;
	}
	for (i = 0; i < length; i++) {
		char c = id[i];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '-')) {
			return 0;
		}
	}
	return 1;
}

static int valid_path(const char *path) {
	size_t i, length = strlen(path);
	if (length == 0 || length > PATH_MAX || path[0] == ':' || path[length - 1] == ':') {
		return 0;
	}
	if (strstr(path, "::") != NULL || strstr(path, "..") != NULL ||
			strchr(path, '/') != NULL) {
		return 0;
	}
	for (i = 0; i < length; i++) {
		unsigned char c = (unsigned char)path[i];
		if (c < 0x20 || c > 0x7e) {
			return 0;
		}
	}
	return 1;
}

static int value_of(const char *text, const char *key, char *out, size_t capacity) {
	size_t key_length = strlen(key);
	const char *line = text;
	while (*line) {
		const char *end = line;
		while (*end && *end != '\n' && *end != '\r') {
			end++;
		}
		if ((size_t)(end - line) > key_length &&
				!memcmp(line, key, key_length) && line[key_length] == '=') {
			const char *value = line + key_length + 1;
			size_t length = (size_t)(end - value);
			if (length >= capacity) {
				return 0;
			}
			memcpy(out, value, length);
			out[length] = 0;
			return 1;
		}
		if (*end == 0) {
			break;
		}
		line = end;
		while (*line == '\n' || *line == '\r') {
			line++;
		}
	}
	return 0;
}

static void write_ack(const char *id, OSErr error) {
	Str255 name;
	short ref;
	long length;
	char text[160];
	OSErr created;
	if (acks_dir == 0) {
		return;
	}
	snprintf(text, sizeof(text), "version=1\nid=%s\nstatus=%s\ncode=%d\n",
		id, error == noErr ? "ok" : "error", (int)error);
	snprintf((char *)name + 1, sizeof(name) - 1, "ack-%s", id);
	name[0] = (unsigned char)strlen((const char *)name + 1);
	created = HCreate(unix_vref, acks_dir, name, 'ZcOH', 'TEXT');
	if (created != noErr && created != dupFNErr) {
		return;
	}
	if (HOpenDF(unix_vref, acks_dir, name, fsRdWrPerm, &ref) != noErr) {
		return;
	}
	SetEOF(ref, 0);
	length = (long)strlen(text);
	FSWrite(ref, &length, text);
	FSClose(ref);
	FlushVol(NULL, unix_vref);
}

static void write_ready(void) {
	Str255 name;
	short ref;
	long length;
	OSErr created;
	static const char text[] = "version=1\nstatus=ready\n";
	if (ready_written || acks_dir == 0) {
		return;
	}
	make_pstring(name, "helper-ready");
	created = HCreate(unix_vref, acks_dir, name, 'ZcOH', 'TEXT');
	if (created != noErr && created != dupFNErr) {
		return;
	}
	if (HOpenDF(unix_vref, acks_dir, name, fsRdWrPerm, &ref) != noErr) {
		return;
	}
	SetEOF(ref, 0);
	length = (long)strlen(text);
	FSWrite(ref, &length, text);
	FSClose(ref);
	FlushVol(NULL, unix_vref);
	ready_written = 1;
}

static OSErr open_request(const FSSpec *spec) {
	char text[REQUEST_MAX];
	char version[8], id[ID_MAX], path[PATH_MAX + 1];
	short ref;
	long length = sizeof(text) - 1;
	OSErr error;
	FSSpec app;
	FInfo info;
	LaunchParamBlockRec launch;
	error = FSpOpenDF((FSSpecPtr)spec, fsRdPerm, &ref);
	if (error != noErr) {
		return error;
	}
	error = FSRead(ref, &length, text);
	FSClose(ref);
	if (error != noErr && error != eofErr) {
		return error;
	}
	if (length <= 0) {
		return paramErr;
	}
	text[length] = 0;
	if (!value_of(text, "version", version, sizeof(version)) ||
			!value_of(text, "id", id, sizeof(id)) ||
			!value_of(text, "path", path, sizeof(path))) {
		return paramErr;
	}
	if (strcmp(version, "1") != 0 || !valid_id(id) || !valid_path(path)) {
		return paramErr;
	}
	if (strcmp(id, last_id) == 0) {
		return noErr;
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
	}
	write_ack(id, error);
	memcpy(last_id, id, sizeof(last_id) - 1);
	last_id[sizeof(last_id) - 1] = 0;
	return error;
}

static unsigned long read_sequence(long queue) {
	FSSpec spec;
	Str255 name;
	short ref;
	long length;
	char text[32];
	unsigned long value = 0;
	long i;
	make_pstring(name, "sequence");
	if (FSMakeFSSpec(unix_vref, queue, name, &spec) != noErr) {
		return 0;
	}
	if (FSpOpenDF(&spec, fsRdPerm, &ref) != noErr) {
		return 0;
	}
	length = sizeof(text) - 1;
	if (FSRead(ref, &length, text) != noErr && length <= 0) {
		FSClose(ref);
		return 0;
	}
	FSClose(ref);
	text[length] = 0;
	for (i = 0; i < length && text[i] >= '0' && text[i] <= '9'; i++) {
		value = value * 10 + (unsigned long)(text[i] - '0');
	}
	return value;
}

/* Every request has a new name; the guest's File Manager caches a name
   after it is used, so a name is never reused. */
static int try_sequence(unsigned long sequence) {
	char text[48];
	Str255 name;
	FSSpec spec;
	snprintf(text, sizeof(text), "request.%lu.req", sequence);
	make_pstring(name, text);
	if (FSMakeFSSpec(unix_vref, requests_dir, name, &spec) != noErr) {
		return 0;
	}
	open_request(&spec);
	HDelete(unix_vref, requests_dir, name);
	return 1;
}

static void scan_requests(void) {
	if (unix_vref == 0) {
		unix_vref = find_unix_volume();
	}
	if (unix_vref == 0) {
		return;
	}
	if (requests_dir == 0) {
		if (folder_in(unix_vref, ROOT_DIR_ID, "zacos9-classic", &queue_dir) != noErr ||
				folder_in(unix_vref, queue_dir, "requests", &requests_dir) != noErr) {
			requests_dir = 0;
			return;
		}
	}
	if (acks_dir == 0) {
		if (queue_dir != 0 &&
				folder_in(unix_vref, queue_dir, "acks", &acks_dir) == noErr) {
			write_ready();
		}
	}
	if (!sequence_loaded) {
		last_sequence = read_sequence(queue_dir);
		sequence_loaded = 1;
	}
	if (last_sequence > 0 && try_sequence(last_sequence)) {
		return;
	}
	{
		unsigned long sequence;
		for (sequence = last_sequence + 1; sequence <= last_sequence + 64; sequence++) {
			if (try_sequence(sequence)) {
				last_sequence = sequence;
				return;
			}
		}
	}
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
	if (unix_vref != 0) {
		long root_dir = 0;
		if (folder_in(unix_vref, ROOT_DIR_ID, "zacos9-classic", &root_dir) == noErr) {
			if (folder_in(unix_vref, root_dir, "acks", &acks_dir) != noErr) {
				acks_dir = 0;
			} else {
				write_ready();
			}
		}
	}
	last_scan = TickCount();
	for (;;) {
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
		if (TickCount() - last_scan >= SCAN_TICKS) {
			last_scan = TickCount();
			scan_requests();
		}
	}
	if (helper_menu) {
		DeleteMenu(128);
		DisposeMenu(helper_menu);
	}
	return 0;
}
