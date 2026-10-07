#include <Quickdraw.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Processes.h>
#include <LowMem.h>
#include <Files.h>
#include <Errors.h>
#include <MixedMode.h>
#include <OSUtils.h>
#include <stdio.h>
#include <string.h>

#include "surface.h"

enum { MAX_PROCESSES = 128, MAX_WINDOWS = 128 };
enum { RECT_PROC_INFO = kPascalStackBased | STACK_ROUTINE_PARAMETER(1, kFourByteCode) };
/* PEF exported-symbol class; absent from the Multiversal CFM declarations. */
enum { NATIVE_TVECTOR_SYMBOL = 2 };

static UniversalProcPtr original_paint, original_erase;
static UniversalProcPtr paint_callback, erase_callback;
static unsigned long intercepted, fixture_rectangles;
static int hooked, in_callback;
static OSErr observation_error;
static const char *observation_reason = "none";
typedef struct {
	unsigned long entry, toc;
} NativeVector;
_Static_assert(sizeof(NativeVector) == 8, "Classic PPC transition vectors must be eight bytes");
typedef pascal void (*NativeRectangle)(const Rect *);
typedef pascal void (*NativeDisposeWindow)(WindowPtr);
typedef pascal void (*NativeText)(Ptr, short, short);
typedef pascal void (*NativeLine)(short, short);
typedef pascal void (*NativeBits)(BitMap *, BitMap *, const Rect *, const Rect *, short, RgnHandle);
typedef pascal void (*NativeScroll)(Rect *, short, short, RgnHandle);
static NativeVector *native_paint, *native_erase, *native_dispose;
static NativeVector saved_paint, saved_erase, saved_dispose;
static ConnectionID interface_connection;
static int native_hooked, interface_open;
static unsigned long fixture_windows;
static Surface candidates[2];
static WindowPtr candidate_windows[2];
static ProcessSerialNumber candidate_processes[2];
static unsigned long candidate_calls[2], disposed_windows;
static THz helper_zone;
enum { TEXT_HOOK, FRAME_HOOK, LINE_HOOK, BITS_HOOK, SCROLL_HOOK, EXTRA_HOOKS };
static NativeVector *extra_vectors[EXTRA_HOOKS];
static NativeVector saved_extra[EXTRA_HOOKS];
static unsigned long extra_calls[EXTRA_HOOKS];
static RgnHandle scroll_update;
static GrafPtr replay_original;
static THz replay_zone;
static int replay_id = -1;

static pascal void native_observe_paint(const Rect *rect);
static pascal void native_observe_erase(const Rect *rect);
static pascal void native_observe_dispose(WindowPtr window);
static pascal void capture_text(Ptr text, short first, short count);
static pascal void capture_frame(const Rect *rect);
static pascal void capture_line(short h, short v);
static pascal void capture_bits(BitMap *source, BitMap *destination,
	const Rect *source_rect, const Rect *destination_rect, short mode, RgnHandle mask);
static pascal void capture_scroll(Rect *rect, short dh, short dv, RgnHandle update);
static const char *extra_names[EXTRA_HOOKS] = {
	"DrawText", "FrameRect", "LineTo", "CopyBits", "ScrollRect"
};

static const NativeVector *extra_callback(unsigned int hook) {
	switch (hook) {
		case TEXT_HOOK: return (const NativeVector *)capture_text;
		case FRAME_HOOK: return (const NativeVector *)capture_frame;
		case LINE_HOOK: return (const NativeVector *)capture_line;
		case BITS_HOOK: return (const NativeVector *)capture_bits;
		case SCROLL_HOOK: return (const NativeVector *)capture_scroll;
	}
	return NULL;
}

static int same_process(const ProcessSerialNumber *a, const ProcessSerialNumber *b) {
	return a->highLongOfPSN == b->highLongOfPSN &&
		a->lowLongOfPSN == b->lowLongOfPSN;
}

static void observe_rectangle(GrafVerb verb, const Rect *rect, int replay) {
	ProcessSerialNumber process;
	ProcessInfoRec info;
	OSErr error;
	intercepted++;
	error = GetCurrentProcess(&process);
	if (error != noErr) {
		observation_error = error;
		return;
	}
	memset(&info, 0, sizeof(info));
	info.processInfoLength = sizeof(info);
	error = GetProcessInformation(&process, &info);
	if (error != noErr) {
		observation_error = error;
		return;
	}
	if (info.processSignature == 'ZcDF') {
		WindowPeek window = LMGetWindowList();
		GrafPtr port;
		WindowPeek target = NULL;
		unsigned int count;
		unsigned long found = 0;
		unsigned int id;
		Str255 title;
		THz previous_zone;
		int width, height;
		fixture_rectangles++;
		GetPort(&port);
		for (count = 0; window && count < MAX_WINDOWS; count++) {
			if ((GrafPtr)window == port) {
				target = window;
			}
			found++;
			window = window->nextWindow;
		}
		if (window) {
			observation_error = paramErr;
		} else if (found > fixture_windows) {
			fixture_windows = found;
		}
		if (!replay || !target || observation_error != noErr) {
			return;
		}
		GetWTitle((WindowPtr)target, title);
		for (id = 0; id < 2; id++) {
			static const unsigned char titles[2][17] = {
				"\pClassic Probe A", "\pClassic Probe B"
			};
			if (title[0] == titles[id][0] && !memcmp(title, titles[id], title[0] + 1)) {
				break;
			}
		}
		if (id == 2) {
			return;
		}
		/* Ignore unrelated dialogs; never read a target color port as GrafPort. */
		if ((port->portBits.rowBytes & 0xC000) == 0xC000) {
			observation_error = paramErr;
			observation_reason = "color port";
			return;
		}
		if (port->grafProcs) {
			observation_error = paramErr;
			observation_reason = "custom procedures";
			return;
		}
		width = port->portRect.right;
		height = port->portRect.bottom;
		if (port->portRect.left || port->portRect.top ||
				width < 64 || width > 640 || height < 64 || height > 384) {
			observation_error = paramErr;
			observation_reason = "origin/dimensions";
			return;
		}
		previous_zone = GetZone();
		SetZone(helper_zone);
		if (candidate_windows[id] != (WindowPtr)target ||
				!same_process(&candidate_processes[id], &process) ||
				candidates[id].width != width || candidates[id].height != height) {
			memset(candidates[id].pixels, 0, candidates[id].stride * 384);
			candidates[id].width = width;
			candidates[id].height = height;
			SetRect(&candidates[id].port.portRect, 0, 0, width, height);
			candidates[id].port.portBits.bounds = candidates[id].port.portRect;
			RectRgn(candidates[id].port.visRgn, &candidates[id].port.portRect);
			error = MemError();
			if (error == noErr) {
				RectRgn(candidates[id].port.clipRgn, &candidates[id].port.portRect);
				error = MemError();
			}
			if (error != noErr) {
				observation_error = error;
				SetZone(previous_zone);
				return;
			}
			candidate_windows[id] = (WindowPtr)target;
			candidate_processes[id] = process;
			candidate_calls[id] = 0;
		}
		SetPort(&candidates[id].port);
		{
			GrafPtr destination = &candidates[id].port;
			destination->fgColor = port->fgColor;
			destination->bkColor = port->bkColor;
			destination->bkPat = port->bkPat;
			destination->fillPat = port->fillPat;
			destination->pnLoc = port->pnLoc;
			destination->pnSize = port->pnSize;
			destination->pnMode = port->pnMode;
			destination->pnPat = port->pnPat;
			destination->pnVis = port->pnVis;
			destination->txFont = port->txFont;
			destination->txFace = port->txFace;
			destination->txMode = port->txMode;
			destination->txSize = port->txSize;
			destination->spExtra = port->spExtra;
			CopyRgn(port->clipRgn, destination->clipRgn);
			error = MemError();
			if (error != noErr) {
				observation_error = error;
				SetPort(port);
				SetZone(previous_zone);
				return;
			}
		}
		if (replay == 2) {
			replay_original = port;
			replay_zone = previous_zone;
			replay_id = id;
			return;
		}
		StdRect(verb, rect);
		candidate_calls[id]++;
		SetPort(port);
		SetZone(previous_zone);
	}
}

static int begin_replay(void) {
	if (in_callback || observation_error != noErr) {
		return 0;
	}
	in_callback = 1;
	replay_id = -1;
	observe_rectangle(paint, NULL, 2);
	if (replay_id < 0) {
		in_callback = 0;
		return 0;
	}
	return 1;
}

static void end_replay(unsigned int hook) {
	candidate_calls[replay_id]++;
	extra_calls[hook]++;
	SetPort(replay_original);
	SetZone(replay_zone);
	replay_id = -1;
	in_callback = 0;
}

static pascal void capture_text(Ptr text, short first, short count) {
	int nested = in_callback;
	if (begin_replay()) {
		((NativeText)&saved_extra[TEXT_HOOK])(text, first, count);
		end_replay(TEXT_HOOK);
	}
	in_callback = 1;
	((NativeText)&saved_extra[TEXT_HOOK])(text, first, count);
	in_callback = nested;
}

static pascal void capture_frame(const Rect *rect) {
	int nested = in_callback;
	if (begin_replay()) {
		((NativeRectangle)&saved_extra[FRAME_HOOK])(rect);
		end_replay(FRAME_HOOK);
	}
	in_callback = 1;
	((NativeRectangle)&saved_extra[FRAME_HOOK])(rect);
	in_callback = nested;
}

static pascal void capture_line(short h, short v) {
	int nested = in_callback;
	if (begin_replay()) {
		((NativeLine)&saved_extra[LINE_HOOK])(h, v);
		end_replay(LINE_HOOK);
	}
	in_callback = 1;
	((NativeLine)&saved_extra[LINE_HOOK])(h, v);
	in_callback = nested;
}

static pascal void capture_bits(BitMap *source, BitMap *destination,
		const Rect *source_rect, const Rect *destination_rect, short mode, RgnHandle mask) {
	int nested = in_callback;
	GrafPtr original;
	GetPort(&original);
	if ((original->portBits.rowBytes & 0xC000) != 0xC000 &&
			destination == &original->portBits && begin_replay()) {
		BitMap *replay_source = source;
		if (source->rowBytes & 0xC000) {
			observation_error = paramErr;
			observation_reason = "color source bitmap";
		}
		if (source->baseAddr == replay_original->portBits.baseAddr) {
			if (source == &replay_original->portBits) {
				replay_source = &candidates[replay_id].port.portBits;
			} else {
				observation_error = paramErr;
				observation_reason = "unmapped screen source";
			}
		}
		if (observation_error == noErr) {
			((NativeBits)&saved_extra[BITS_HOOK])(replay_source,
				&candidates[replay_id].port.portBits,
				source_rect, destination_rect, mode, mask);
		}
		end_replay(BITS_HOOK);
	}
	in_callback = 1;
	((NativeBits)&saved_extra[BITS_HOOK])(source, destination,
		source_rect, destination_rect, mode, mask);
	in_callback = nested;
}

static pascal void capture_scroll(Rect *rect, short dh, short dv, RgnHandle update) {
	int nested = in_callback;
	if (begin_replay()) {
		SetEmptyRgn(scroll_update);
		((NativeScroll)&saved_extra[SCROLL_HOOK])(rect, dh, dv, scroll_update);
		end_replay(SCROLL_HOOK);
	}
	in_callback = 1;
	((NativeScroll)&saved_extra[SCROLL_HOOK])(rect, dh, dv, update);
	in_callback = nested;
}

static pascal void native_observe_paint(const Rect *rect) {
	if (!in_callback) {
		in_callback = 1;
		observe_rectangle(paint, rect, 1);
		in_callback = 0;
	}
	((NativeRectangle)&saved_paint)(rect);
}

static pascal void native_observe_erase(const Rect *rect) {
	if (!in_callback) {
		in_callback = 1;
		observe_rectangle(erase, rect, 1);
		in_callback = 0;
	}
	((NativeRectangle)&saved_erase)(rect);
}

static pascal void native_observe_dispose(WindowPtr window) {
	ProcessSerialNumber process;
	OSErr error = GetCurrentProcess(&process);
	unsigned int i;
	if (error != noErr) {
		observation_error = error;
	} else {
		for (i = 0; i < 2; i++) {
			if (candidate_windows[i] == window &&
					same_process(&candidate_processes[i], &process)) {
				candidate_windows[i] = NULL;
				candidate_calls[i] = 0;
				disposed_windows++;
			}
		}
	}
	((NativeDisposeWindow)&saved_dispose)(window);
}

static pascal void intercept_paint(const Rect *rect) {
	if (!in_callback) {
		in_callback = 1;
		observe_rectangle(paint, rect, 0);
		in_callback = 0;
	}
	CallUniversalProc(original_paint, RECT_PROC_INFO, rect);
}

static pascal void intercept_erase(const Rect *rect) {
	if (!in_callback) {
		in_callback = 1;
		observe_rectangle(erase, rect, 0);
		in_callback = 0;
	}
	CallUniversalProc(original_erase, RECT_PROC_INFO, rect);
}

static int stop_observer(void) {
	unsigned int i;
	if (native_hooked) {
		const NativeVector *paint_vector = (const NativeVector *)native_observe_paint;
		const NativeVector *erase_vector = (const NativeVector *)native_observe_erase;
		const NativeVector *dispose_vector = (const NativeVector *)native_observe_dispose;
		if (memcmp(native_paint, paint_vector, sizeof(*native_paint)) ||
				memcmp(native_erase, erase_vector, sizeof(*native_erase)) ||
				memcmp(native_dispose, dispose_vector, sizeof(*native_dispose))) {
			message("Another patch changed native drawing vectors. The helper must remain loaded. Recover the disposable guest instead of force-quitting.");
			return 0;
		}
		for (i = 0; i < EXTRA_HOOKS; i++) {
			if (memcmp(extra_vectors[i], extra_callback(i), sizeof(NativeVector))) {
				message("Another patch changed an extended drawing vector. Keep the helper loaded and recover the disposable guest.");
				return 0;
			}
		}
		*native_paint = saved_paint;
		*native_erase = saved_erase;
		*native_dispose = saved_dispose;
		for (i = 0; i < EXTRA_HOOKS; i++) {
			*extra_vectors[i] = saved_extra[i];
		}
		for (i = 0; i < EXTRA_HOOKS; i++) {
			if (memcmp(extra_vectors[i], &saved_extra[i], sizeof(NativeVector))) {
				message("Extended drawing vector restoration did not persist. Keep the helper loaded and recover the disposable guest.");
				return 0;
			}
		}
		if (memcmp(native_paint, &saved_paint, sizeof(saved_paint)) ||
				memcmp(native_erase, &saved_erase, sizeof(saved_erase)) ||
				memcmp(native_dispose, &saved_dispose, sizeof(saved_dispose))) {
			message("Native vector restoration did not persist. The helper must remain loaded; recover the disposable guest.");
			return 0;
		}
		native_hooked = 0;
	}
	if (hooked) {
		if (GetToolboxTrapAddress(_PaintRect) != paint_callback ||
				GetToolboxTrapAddress(_EraseRect) != erase_callback) {
			message("Another patch changed the trap chain. The helper must stay running; restore the disposable guest instead of unloading callback code.");
			return 0;
		}
		SetToolboxTrapAddress(original_paint, _PaintRect);
		SetToolboxTrapAddress(original_erase, _EraseRect);
		hooked = 0;
	}
	if (paint_callback) {
		DisposeRoutineDescriptor(paint_callback);
		paint_callback = NULL;
	}
	if (erase_callback) {
		DisposeRoutineDescriptor(erase_callback);
		erase_callback = NULL;
	}
	if (interface_open) {
		OSErr error = CloseConnection(&interface_connection);
		if (error != noErr) {
			char text[120];
			snprintf(text, sizeof(text), "Vectors restored, but CloseConnection failed (Mac error %d).", error);
			message(text);
			return 0;
		}
		interface_open = 0;
		native_paint = native_erase = native_dispose = NULL;
	}
	for (i = 0; i < 2; i++) {
		dispose_surface(&candidates[i]);
		candidate_windows[i] = NULL;
		candidate_calls[i] = 0;
	}
	if (scroll_update) {
		DisposeRgn(scroll_update);
		scroll_update = NULL;
	}
	return 1;
}

static void start_native_observer(void) {
	static const unsigned char library[] = "\pInterfaceLib";
	static const unsigned char paint_name[] = "\pPaintRect";
	static const unsigned char erase_name[] = "\pEraseRect";
	static const unsigned char dispose_name[] = "\pDisposeWindow";
	Str255 error_name;
	Ptr main_address, address;
	SymClass symbol_class;
	OSErr error;
	char text[180];
	unsigned int i;
	if (hooked || native_hooked || interface_open) {
		message("Stop the current observer before installing native vectors.");
		return;
	}
	error = GetSharedLibrary(library, 'pwpc', kLoadLib, &interface_connection,
		&main_address, error_name);
	if (error != noErr) {
		goto failed;
	}
	interface_open = 1;
	error = FindSymbol(interface_connection, paint_name, &address, &symbol_class);
	if (error != noErr) {
		goto failed;
	}
	if (symbol_class != NATIVE_TVECTOR_SYMBOL) {
		error = paramErr;
		goto failed;
	}
	native_paint = (NativeVector *)address;
	error = FindSymbol(interface_connection, erase_name, &address, &symbol_class);
	if (error != noErr) {
		goto failed;
	}
	if (symbol_class != NATIVE_TVECTOR_SYMBOL) {
		error = paramErr;
		goto failed;
	}
	native_erase = (NativeVector *)address;
	error = FindSymbol(interface_connection, dispose_name, &address, &symbol_class);
	if (error != noErr) {
		goto failed;
	}
	if (symbol_class != NATIVE_TVECTOR_SYMBOL) {
		error = paramErr;
		goto failed;
	}
	native_dispose = (NativeVector *)address;
	for (i = 0; i < EXTRA_HOOKS; i++) {
		Str255 name;
		name[0] = strlen(extra_names[i]);
		memcpy(name + 1, extra_names[i], name[0]);
		error = FindSymbol(interface_connection, name, &address, &symbol_class);
		if (error != noErr) {
			goto failed;
		}
		if (symbol_class != NATIVE_TVECTOR_SYMBOL) {
			error = paramErr;
			goto failed;
		}
		extra_vectors[i] = (NativeVector *)address;
		saved_extra[i] = *extra_vectors[i];
		extra_calls[i] = 0;
	}
	scroll_update = NewRgn();
	if (!scroll_update) {
		error = memFullErr;
		goto failed;
	}
	for (i = 0; i < 2; i++) {
		/* OpenPort stays in the helper context, never in a foreign process. */
		if (!create_surface(&candidates[i], 640, 384)) {
			error = memFullErr;
			goto failed;
		}
	}
	saved_paint = *native_paint;
	saved_erase = *native_erase;
	saved_dispose = *native_dispose;
	intercepted = fixture_rectangles = fixture_windows = 0;
	disposed_windows = 0;
	observation_error = noErr;
	observation_reason = "none";
	/* Classic PowerPC function pointers address entry/TOC transition vectors. */
	*native_paint = *(const NativeVector *)native_observe_paint;
	*native_erase = *(const NativeVector *)native_observe_erase;
	*native_dispose = *(const NativeVector *)native_observe_dispose;
	for (i = 0; i < EXTRA_HOOKS; i++) {
		*extra_vectors[i] = *extra_callback(i);
	}
	native_hooked = 1;
	message("Native monochrome graphics capture installed for ZcDF. Color/custom ports are refused. Keep helper running; pause and export fixture references before helper candidates.");
	return;
failed:
	stop_observer();
	snprintf(text, sizeof(text), "Native observer installation failed (Mac error %d).", error);
	message(text);
}

static void export_candidates(int graphics) {
	unsigned long frames[2] = { 0, 0 };
	unsigned int i, bit;
	char text[180];
	if (!native_hooked || observation_error != noErr) {
		snprintf(text, sizeof(text), "No valid native capture session (Mac error %d: %s). No PBM exported.", observation_error, observation_reason);
		message(text);
		return;
	}
	for (i = 0; i < 2; i++) {
		ProcessInfoRec info;
		OSErr error;
		if (!candidate_windows[i] || !candidate_calls[i]) {
			message("Both fixture windows must have been drawn and remain open. No PBM exported.");
			return;
		}
		memset(&info, 0, sizeof(info));
		info.processInfoLength = sizeof(info);
		error = GetProcessInformation(&candidate_processes[i], &info);
		if (error != noErr) {
			candidate_windows[i] = NULL;
			snprintf(text, sizeof(text), "Captured owner is no longer available (Mac error %d). No PBM exported.", error);
			message(text);
			return;
		}
		if (candidates[i].width < 256) {
			message("Fixture window is too narrow for a full 32-bit frame marker. No PBM exported.");
			return;
		}
		for (bit = 0; bit < 32; bit++) {
			unsigned long pixel = ((unsigned char)candidates[i].pixels[bit] >> 7) & 1;
			frames[i] |= (pixel ^ i) << bit;
		}
	}
	if (frames[0] != frames[1]) {
		message("Captured frame markers disagree. Pause the fixture and retry. No PBM exported.");
		return;
	}
	for (i = 0; i < 2; i++) {
		if (!export_surface_profile(&candidates[i], i, "candidate", frames[i], "ZacHelper", graphics)) {
			return;
		}
	}
	snprintf(text, sizeof(text), "Helper candidates exported for frame %lu. Verify against independently exported fixture references.", frames[0]);
	message(text);
}

static void start_observer(void) {
	if (hooked || native_hooked || interface_open) {
		message("The rectangle observer is already installed.");
		return;
	}
	paint_callback = NewRoutineDescriptor((ProcPtr)intercept_paint,
		RECT_PROC_INFO, GetCurrentArchitecture());
	erase_callback = NewRoutineDescriptor((ProcPtr)intercept_erase,
		RECT_PROC_INFO, GetCurrentArchitecture());
	if (!paint_callback || !erase_callback) {
		stop_observer();
		message("Could not allocate observer callbacks.");
		return;
	}
	original_paint = GetToolboxTrapAddress(_PaintRect);
	original_erase = GetToolboxTrapAddress(_EraseRect);
	intercepted = fixture_rectangles = fixture_windows = 0;
	observation_error = noErr;
	SetToolboxTrapAddress(paint_callback, _PaintRect);
	SetToolboxTrapAddress(erase_callback, _EraseRect);
	hooked = 1;
	message("Rectangle observer installed. It counts calls and chains the original traps; it does not capture pixels. Inspect after advancing the separate fixture, then stop the observer.");
}

static OSErr write_line(short file, const char *line) {
	return write_bytes(file, line, strlen(line));
}

static void inspect_windows(void) {
	static const unsigned char filename[] = "\pClassicHelperReport.txt";
	ProcessSerialNumber process = { 0, kNoProcess };
	ProcessInfoRec info;
	Str255 name;
	short file, volume;
	unsigned int count, targets = 0;
	WindowPeek window;
	OSErr error, close_error;
	char line[512];
	error = HCreate(0, 0, filename, 'ZcSH', 'TEXT');
	if (error != noErr && error != dupFNErr) {
		goto failed;
	}
	error = HOpenDF(0, 0, filename, fsRdWrPerm, &file);
	if (error != noErr) {
		goto failed;
	}
	error = GetVRefNum(file, &volume);
	if (error == noErr) {
		error = SetEOF(file, 0);
	}
	if (error == noErr) {
		error = write_line(file,
			"Classic helper discovery report v4\n"
			"No window pointers or per-window drawing procedures are modified.\n");
	}
	for (count = 0; error == noErr && count < MAX_PROCESSES; count++) {
		error = GetNextProcess(&process);
		if (error == procNotFound) {
			error = noErr;
			break;
		}
		if (error != noErr) {
			break;
		}
		memset(&info, 0, sizeof(info));
		info.processInfoLength = sizeof(info);
		info.processName = name;
		error = GetProcessInformation(&process, &info);
		if (error != noErr) {
			break;
		}
		snprintf(line, sizeof(line),
			"process %lu:%lu signature=%08lx heap=%08lx size=%lu name=%.*s\n",
			(unsigned long)process.highLongOfPSN,
			(unsigned long)process.lowLongOfPSN,
			(unsigned long)info.processSignature,
			(unsigned long)info.processLocation,
			(unsigned long)info.processSize, name[0], name + 1);
		error = write_line(file, line);
	}
	if (error == noErr && count == MAX_PROCESSES) {
		error = write_line(file, "ERROR: process enumeration reached safety limit.\n");
		if (error == noErr) {
			error = paramErr;
		}
	}
	window = LMGetWindowList();
	for (count = 0; error == noErr && window && count < MAX_WINDOWS; count++) {
		static const unsigned char title_a[] = "\pClassic Probe A";
		static const unsigned char title_b[] = "\pClassic Probe B";
		GetWTitle((WindowPtr)window, name);
		if ((name[0] == title_a[0] && !memcmp(name, title_a, name[0] + 1)) ||
				(name[0] == title_b[0] && !memcmp(name, title_b, name[0] + 1))) {
			targets++;
		}
		snprintf(line, sizeof(line),
			"window=%08lx kind=%d visible=%d grafProcs=%08lx title=%.*s\n",
			(unsigned long)window, window->windowKind, window->visible,
			(unsigned long)window->port.grafProcs, name[0], name + 1);
		error = write_line(file, line);
		window = window->nextWindow;
	}
	if (error == noErr && window) {
		error = write_line(file, "ERROR: window enumeration reached safety limit.\n");
		if (error == noErr) {
			error = paramErr;
		}
	}
	if (error == noErr) {
		snprintf(line, sizeof(line),
			"target_windows=%u\nhooks_installed=%d native_installed=%d intercepted=%lu fixture_rectangles=%lu fixture_context_windows=%lu observation_error=%d\n"
			"native_paint=%08lx native_erase=%08lx\n",
			targets, hooked, native_hooked, intercepted, fixture_rectangles,
			fixture_windows, observation_error,
			(unsigned long)native_paint, (unsigned long)native_erase);
		error = write_line(file, line);
	}
	if (error == noErr) {
		snprintf(line, sizeof(line),
			"candidate_calls=%lu,%lu candidate_windows=%08lx,%08lx disposed_windows=%lu\n",
			candidate_calls[0], candidate_calls[1],
			(unsigned long)candidate_windows[0], (unsigned long)candidate_windows[1],
			disposed_windows);
		error = write_line(file, line);
	}
	if (error == noErr) {
		snprintf(line, sizeof(line),
			"graphics_calls text=%lu frame=%lu line=%lu bits=%lu scroll=%lu\nobservation_reason=%s\n",
			extra_calls[TEXT_HOOK], extra_calls[FRAME_HOOK], extra_calls[LINE_HOOK],
			extra_calls[BITS_HOOK], extra_calls[SCROLL_HOOK], observation_reason);
		error = write_line(file, line);
	}
	close_error = FSClose(file);
	if (error == noErr) {
		error = close_error;
	}
	if (error == noErr) {
		error = FlushVol(NULL, volume);
	}
	if (error == noErr) {
		if (observation_error != noErr) {
			snprintf(line, sizeof(line),
				"Report exported, but rectangle observation failed (Mac error %d).",
				observation_error);
			message(line);
			return;
		}
		snprintf(line, sizeof(line),
			"Report exported. Fixture windows in helper list: %u. Rectangle calls: %lu, from fixture: %lu. Native candidates require separate export and pixel verification.",
			targets, intercepted, fixture_rectangles);
		message(line);
		return;
	}
failed:
	snprintf(line, sizeof(line), "Discovery report failed (Mac error %d).", error);
	message(line);
}

int main(void) {
	EventRecord event;
	MenuHandle menu;
	int running = 1;
	static const unsigned char menu_title[] = "\pHelper";
	static const unsigned char menu_items[] =
		"\pInspect windows/I;Start observer/S;Stop observer/T;Native capture/N;Export candidates/E;(-;Quit/Q;Export graphics/G";
	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(NULL);
	InitCursor();
	helper_zone = GetZone();
	menu = NewMenu(128, menu_title);
	if (!menu) {
		message("Could not create the helper menu.");
		return 1;
	}
	AppendMenu(menu, menu_items);
	InsertMenu(menu, 0);
	DrawMenuBar();
	while (running) {
		if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
			long command = 0;
			if (event.what == mouseDown) {
				WindowPtr window;
				if (FindWindow(event.where, &window) == inMenuBar) {
					command = MenuSelect(event.where);
				}
			} else if ((event.what == keyDown || event.what == autoKey) &&
					(event.modifiers & cmdKey)) {
				command = MenuKey(event.message & charCodeMask);
			}
			if ((command >> 16) == 128) {
				if ((command & 0xffff) == 1) {
					inspect_windows();
				} else if ((command & 0xffff) == 2) {
					start_observer();
				} else if ((command & 0xffff) == 3) {
					if (stop_observer()) {
						message("Observer stopped; original trap entries restored.");
					}
				} else if ((command & 0xffff) == 4) {
					start_native_observer();
				} else if ((command & 0xffff) == 5) {
					export_candidates(0);
				} else if ((command & 0xffff) == 7) {
					if (stop_observer()) {
						running = 0;
					}
				} else if ((command & 0xffff) == 8) {
					export_candidates(1);
				}
			}
			HiliteMenu(0);
		}
	}
	DeleteMenu(128);
	DisposeMenu(menu);
	return 0;
}
