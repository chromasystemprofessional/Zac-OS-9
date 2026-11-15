/*
 * Classic Window Agent: discover and capture windows from every Classic
 * process.
 *
 * The agent runs from Startup Items and installs native InterfaceLib
 * transition-vector hooks. Drawing calls identify the owning window and are
 * replayed into a per-window color GWorld before the original call runs, so a
 * window that is fully obscured still has an independent backing store.
 *
 * This first slice exports an inventory plus PPM frames to the shared Unix
 * volume. It does not yet forward guest input or create host windows.
 *
 * Original code; no Apple software is shipped.
 */
#include <Quickdraw.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Processes.h>
#include <LowMem.h>
#include <Files.h>
#include <Errors.h>
#include <MacMemory.h>
#include <stdio.h>
#include <string.h>

#include "surface.h"

enum {
	MAX_WINDOWS = 32,
	MAX_CAPTURES = 8,
	SCAN_TICKS = 60,
	EXPORT_TICKS = 15,
	ROOT_DIR_ID = 2,
	NATIVE_TVECTOR_SYMBOL = 2
};

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
typedef pascal void (*NativeStdText)(short, Ptr, Point, Point);

typedef struct {
	WindowPtr window;
	ProcessSerialNumber process;
	Str31 app_name;
	OSType signature;
	Str255 title;
	Rect bounds;
	Rect global;
	Rect window_frame;
	short kind;
	int go_away;
	short depth;
	unsigned short version;
	int color;
	int visible;
	unsigned long calls;
	unsigned long frame;
	unsigned long op_counts[7];
	unsigned long bk_color, rgb_bk_color;
	Rect clip_box;
	unsigned int serial;
	int used;
	int capture_ok;
	OSErr capture_error;
	ColorSurface color_surface;
	int dirty;
} WindowInfo;

enum { TEXT_HOOK, FRAME_HOOK, LINE_HOOK, BITS_HOOK, SCROLL_HOOK, STDTEXT_HOOK, EXTRA_HOOKS };
static const char *extra_names[EXTRA_HOOKS] = {
	"DrawText", "FrameRect", "LineTo", "CopyBits", "ScrollRect", "StdText"
};

static NativeVector *native_paint, *native_erase, *native_dispose, *native_set_title;
static NativeVector *native_hide, *native_show, *native_close;
static NativeVector saved_paint, saved_erase, saved_dispose, saved_set_title;
static NativeVector saved_hide, saved_show, saved_close;
static NativeVector *extra_vectors[EXTRA_HOOKS];
static NativeVector saved_extra[EXTRA_HOOKS];
static ConnectionID interface_connection;
static int interface_open, native_hooked, in_callback, in_capture;
static WindowInfo windows[MAX_WINDOWS];
static unsigned int window_count, next_serial;
static unsigned int overflow, capture_count;
static unsigned long draw_calls, window_matches, offscreen_draws, process_errors;
static unsigned long captured_ops;
static OSErr last_error;
static short unix_vref;
static GrafPort agent_port;
static GrafPtr replay_original;
static CGrafPtr replay_world;
static GDHandle replay_device;
static THz replay_zone;
static WindowInfo *replay_window;
static RgnHandle scroll_update;

static pascal void observe_paint(const Rect *rect);
static pascal void observe_erase(const Rect *rect);
static pascal void observe_dispose(WindowPtr window);
static pascal void observe_set_title(WindowPtr window, ConstStringPtr title);
static pascal void observe_hide(WindowPtr window);
static pascal void observe_show(WindowPtr window);
static pascal void observe_close(WindowPtr window);
static pascal void capture_text(Ptr text, short first, short count);
static pascal void capture_frame(const Rect *rect);
static pascal void capture_line(short h, short v);
static pascal void capture_bits(BitMap *source, BitMap *destination,
	const Rect *source_rect, const Rect *destination_rect, short mode, RgnHandle mask);
static pascal void capture_scroll(Rect *rect, short dh, short dv, RgnHandle update);
static pascal void capture_std_text(short n, Ptr text, Point num, Point den);

static int same_process(const ProcessSerialNumber *a, const ProcessSerialNumber *b) {
	return a->highLongOfPSN == b->highLongOfPSN &&
		a->lowLongOfPSN == b->lowLongOfPSN;
}

static void make_pstring(Str255 out, const char *text) {
	size_t length = strlen(text);
	if (length > 255)
		length = 255;
	out[0] = (unsigned char)length;
	memcpy(out + 1, text, length);
}

static short find_unix_volume(void) {
	HParamBlockRec pb;
	Str255 name;
	short index;
	for (index = 1; index <= 64; index++) {
		memset(&pb, 0, sizeof(pb));
		pb.volumeParam.ioNamePtr = name;
		pb.volumeParam.ioVolIndex = index;
		if (PBHGetVInfoSync(&pb) != noErr)
			break;
		if (name[0] == 4 && memcmp(name + 1, "Unix", 4) == 0)
			return pb.volumeParam.ioVRefNum;
	}
	return 0;
}

static OSErr write_text_file(const char *name, const char *text, long length) {
	Str255 pname;
	short ref, volume;
	OSErr error, close_error;
	make_pstring(pname, name);
	error = HCreate(unix_vref, ROOT_DIR_ID, pname, 'ZcWA', 'TEXT');
	if (error != noErr && error != dupFNErr)
		return error;
	error = HOpenDF(unix_vref, ROOT_DIR_ID, pname, fsRdWrPerm, &ref);
	if (error != noErr)
		return error;
	error = SetEOF(ref, 0);
	if (error == noErr)
		error = GetVRefNum(ref, &volume);
	if (error == noErr)
		error = write_bytes(ref, text, length);
	close_error = FSClose(ref);
	if (error == noErr)
		error = close_error;
	if (error == noErr)
		error = FlushVol(NULL, volume);
	return error;
}

static void write_error(const char *where, OSErr error) {
	char text[160];
	snprintf(text, sizeof(text), "version=1\nerror=%d\nwhere=%s\n", error, where);
	if (unix_vref == 0)
		unix_vref = find_unix_volume();
	if (unix_vref != 0)
		write_text_file("ClassicWindowsError.txt", text, (long)strlen(text));
}

static WindowInfo *find_window(WindowPtr window, const ProcessSerialNumber *process) {
	unsigned int i;
	for (i = 0; i < window_count; i++) {
		if (windows[i].used && windows[i].window == window &&
				same_process(&windows[i].process, process))
			return &windows[i];
	}
	return NULL;
}

static WindowInfo *add_window(WindowPtr window, const ProcessSerialNumber *process) {
	WindowInfo *info;
	unsigned int i;
	for (i = 0; i < window_count; i++) {
		if (!windows[i].used) {
			info = &windows[i];
			goto found;
		}
	}
	if (window_count >= MAX_WINDOWS) {
		overflow++;
		return NULL;
	}
	info = &windows[window_count++];
found:
	memset(info, 0, sizeof(*info));
	info->window = window;
	info->process = *process;
	info->serial = next_serial++;
	info->used = 1;
	info->clip_box.left = info->clip_box.top = 0;
	info->clip_box.right = info->clip_box.bottom = 0;
	return info;
}

static int is_color_port(GrafPtr port) {
	return (((CGrafPtr)port)->portVersion & 0xC000) != 0;
}

static WindowInfo *observe_window(void) {
	GrafPtr port;
	WindowPeek cursor, target = NULL;
	ProcessSerialNumber process;
	ProcessInfoRec info;
	Str31 process_name;
	WindowInfo *record;
	unsigned int count;
	OSErr error;

	draw_calls++;
	GetPort(&port);
	if (port == &agent_port)
		return NULL;
	cursor = LMGetWindowList();
	for (count = 0; cursor && count < MAX_WINDOWS; count++) {
		if ((GrafPtr)cursor == port) {
			target = cursor;
			break;
		}
		cursor = cursor->nextWindow;
	}
	if (!target) {
		offscreen_draws++;
		return NULL;
	}
	window_matches++;
	error = GetCurrentProcess(&process);
	if (error != noErr) {
		process_errors++;
		last_error = error;
		return NULL;
	}
	record = find_window((WindowPtr)target, &process);
	if (!record)
		record = add_window((WindowPtr)target, &process);
	if (!record)
		return NULL;
	record->calls++;
	record->kind = target->windowKind;
	record->visible = target->visible;
	record->bounds = port->portRect;
	if (target->contRgn)
		record->global = (*target->contRgn)->rgnBBox;
	else
		record->global = port->portRect;
	if (target->strucRgn)
		record->window_frame = (*target->strucRgn)->rgnBBox;
	else
		record->window_frame = record->global;
	record->go_away = target->goAwayFlag;
	record->version = ((CGrafPtr)port)->portVersion;
	record->color = is_color_port(port);
	record->depth = 1;
	if (record->color) {
		PixMapHandle pixmap = ((CGrafPtr)port)->portPixMap;
		if (pixmap)
			record->depth = (**pixmap).pixelSize;
	}
	{
		Str255 title;
		GetWTitle((WindowPtr)target, title);
		memcpy(record->title, title, title[0] + 1);
	}
	memset(&info, 0, sizeof(info));
	process_name[0] = 0;
	info.processInfoLength = sizeof(info);
	info.processName = process_name;
	if (GetProcessInformation(&process, &info) == noErr) {
		memcpy(record->app_name, process_name, process_name[0] + 1);
		record->signature = info.processSignature;
	}
	return record;
}

static void copy_color_state(CGrafPtr destination, CGrafPtr source, OSErr *error) {
	destination->rgbFgColor = source->rgbFgColor;
	destination->rgbBkColor = source->rgbBkColor;
	destination->fgColor = source->fgColor;
	destination->bkColor = source->bkColor;
	destination->bkPixPat = source->bkPixPat;
	destination->pnPixPat = source->pnPixPat;
	destination->fillPixPat = source->fillPixPat;
	destination->pnLoc = source->pnLoc;
	destination->pnSize = source->pnSize;
	destination->pnMode = source->pnMode;
	destination->pnVis = source->pnVis;
	destination->txFont = source->txFont;
	destination->txFace = source->txFace;
	destination->txMode = source->txMode;
	destination->txSize = source->txSize;
	destination->spExtra = source->spExtra;
	destination->chExtra = source->chExtra;
	destination->pnLocHFrac = source->pnLocHFrac;
	if (source->clipRgn && destination->clipRgn) {
		CopyRgn(source->clipRgn, destination->clipRgn);
		*error = probe_mem_error();
	} else {
		*error = paramErr;
	}
}

static void seed_color_surface(ColorSurface *surface, RGBColor color) {
	PixMap *pixmap;
	unsigned char *base;
	unsigned long stride, y;
	int x;
	if (!surface->pixmap)
		return;
	pixmap = *surface->pixmap;
	base = (unsigned char *)GetPixBaseAddr(surface->pixmap);
	stride = color_row_bytes(surface->pixmap);
	if (pixmap->pixelSize == 32) {
		for (y = 0; y < (unsigned long)surface->height; y++) {
			unsigned char *row = base + y * stride;
			for (x = 0; x < surface->width; x++) {
				row[x * 4] = 0;
				row[x * 4 + 1] = color.red >> 8;
				row[x * 4 + 2] = color.green >> 8;
				row[x * 4 + 3] = color.blue >> 8;
			}
		}
	} else if (pixmap->pixelSize == 16) {
		unsigned int value = ((color.red >> 11) << 10) |
			((color.green >> 11) << 5) | (color.blue >> 11);
		for (y = 0; y < (unsigned long)surface->height; y++) {
			unsigned char *row = base + y * stride;
			for (x = 0; x < surface->width; x++) {
				row[x * 2] = value >> 8;
				row[x * 2 + 1] = value & 0xff;
			}
		}
	}
}

static WindowInfo *begin_capture(void) {
	WindowInfo *record;
	CGrafPtr color_port;
	PixMapHandle pixmap;
	int width, height, depth;
	OSErr error;

	if (in_capture)
		return NULL;
	in_capture = 1;
	record = observe_window();
	if (!record) {
		in_capture = 0;
		return NULL;
	}
	if (!record->color) {
		record->capture_error = paramErr;
		in_capture = 0;
		return NULL;
	}
	color_port = (CGrafPtr)record->window;
	if (color_port->grafProcs) {
		record->capture_error = paramErr;
		in_capture = 0;
		return NULL;
	}
	pixmap = color_port->portPixMap;
	if (!pixmap || !color_pixmap_supported(*pixmap)) {
		record->capture_error = paramErr;
		in_capture = 0;
		return NULL;
	}
	width = record->bounds.right - record->bounds.left;
	height = record->bounds.bottom - record->bounds.top;
	depth = (**pixmap).pixelSize;
	if (record->bounds.left || record->bounds.top || width < 1 || height < 1 ||
			width > 1024 || height > 768 || depth == 1) {
		record->capture_error = paramErr;
		in_capture = 0;
		return NULL;
	}
	if (!record->capture_ok || record->color_surface.width != width ||
			record->color_surface.height != height ||
			record->color_surface.depth != depth) {
		THz previous_zone = GetZone();
		SetZone(probe_system_zone());
		dispose_color_surface(&record->color_surface);
		if (!create_color_surface(&record->color_surface, width, height, depth)) {
			SetZone(previous_zone);
			record->capture_ok = 0;
			record->capture_error = memFullErr;
			in_capture = 0;
			return NULL;
		}
		SetZone(previous_zone);
		record->capture_ok = 1;
		record->capture_error = noErr;
		seed_color_surface(&record->color_surface, color_port->rgbBkColor);
		capture_count++;
	}
	GetGWorld(&replay_world, &replay_device);
	SetGWorld((CGrafPtr)record->color_surface.world, NULL);
	copy_color_state((CGrafPtr)record->color_surface.world, color_port, &error);
	if (error != noErr) {
		record->capture_error = error;
		SetGWorld(replay_world, replay_device);
		SetPort((GrafPtr)record->window);
		in_capture = 0;
		return NULL;
	}
	record->bk_color = (unsigned long)color_port->bkColor;
	record->rgb_bk_color = ((unsigned long)color_port->rgbBkColor.red << 16) |
		((unsigned long)color_port->rgbBkColor.green << 8) |
		(unsigned long)color_port->rgbBkColor.blue;
	if (color_port->clipRgn)
		record->clip_box = (*color_port->clipRgn)->rgnBBox;
	replay_original = (GrafPtr)record->window;
	replay_zone = GetZone();
	replay_window = record;
	return record;
}

static void end_capture(WindowInfo *record) {
	if (!record) {
		in_capture = 0;
		return;
	}
	captured_ops++;
	record->frame++;
	record->dirty = 1;
	SetGWorld(replay_world, replay_device);
	SetPort(replay_original);
	SetZone(replay_zone);
	replay_window = NULL;
	replay_original = NULL;
	in_capture = 0;
}

static void replay_rectangle(GrafVerb verb, const Rect *rect) {
	WindowInfo *record = begin_capture();
	if (record) {
		record->op_counts[verb == paint ? 0 : 1]++;
		StdRect(verb, rect);
		end_capture(record);
	}
}

static pascal void observe_paint(const Rect *rect) {
	if (!in_callback) {
		in_callback = 1;
		replay_rectangle(paint, rect);
		in_callback = 0;
	}
	((NativeRectangle)&saved_paint)(rect);
}

static pascal void observe_erase(const Rect *rect) {
	if (!in_callback) {
		in_callback = 1;
		replay_rectangle(erase, rect);
		in_callback = 0;
	}
	((NativeRectangle)&saved_erase)(rect);
}

static pascal void capture_text(Ptr text, short first, short count) {
	int nested = in_callback;
	WindowInfo *record = NULL;
	if (!in_callback) {
		in_callback = 1;
		record = begin_capture();
		if (record) {
			record->op_counts[2]++;
			((NativeText)&saved_extra[TEXT_HOOK])(text, first, count);
		}
		end_capture(record);
		in_callback = 0;
	}
	in_callback = 1;
	((NativeText)&saved_extra[TEXT_HOOK])(text, first, count);
	in_callback = nested;
}

static pascal void capture_std_text(short n, Ptr text, Point num, Point den) {
	int nested = in_callback;
	WindowInfo *record = NULL;
	if (!in_callback) {
		in_callback = 1;
		record = begin_capture();
		if (record) {
			record->op_counts[2]++;
			((NativeStdText)&saved_extra[STDTEXT_HOOK])(n, text, num, den);
		}
		end_capture(record);
		in_callback = 0;
	}
	in_callback = 1;
	((NativeStdText)&saved_extra[STDTEXT_HOOK])(n, text, num, den);
	in_callback = nested;
}

static pascal void capture_frame(const Rect *rect) {
	int nested = in_callback;
	WindowInfo *record = NULL;
	if (!in_callback) {
		in_callback = 1;
		record = begin_capture();
		if (record) {
			record->op_counts[3]++;
			((NativeRectangle)&saved_extra[FRAME_HOOK])(rect);
		}
		end_capture(record);
		in_callback = 0;
	}
	in_callback = 1;
	((NativeRectangle)&saved_extra[FRAME_HOOK])(rect);
	in_callback = nested;
}

static pascal void capture_line(short h, short v) {
	int nested = in_callback;
	WindowInfo *record = NULL;
	if (!in_callback) {
		in_callback = 1;
		record = begin_capture();
		if (record) {
			record->op_counts[4]++;
			((NativeLine)&saved_extra[LINE_HOOK])(h, v);
		}
		end_capture(record);
		in_callback = 0;
	}
	in_callback = 1;
	((NativeLine)&saved_extra[LINE_HOOK])(h, v);
	in_callback = nested;
}

static pascal void capture_bits(BitMap *source, BitMap *destination,
		const Rect *source_rect, const Rect *destination_rect, short mode, RgnHandle mask) {
	int nested = in_callback;
	WindowInfo *record = NULL;
	if (!in_callback) {
		in_callback = 1;
		record = begin_capture();
		if (record) {
			PixMap *window_pixmap = *((CGrafPtr)replay_original)->portPixMap;
			PixMap *capture_pixmap = *record->color_surface.pixmap;
			BitMap *replay_source = source;
			if ((PixMap *)destination != window_pixmap) {
				/* Drawing from the window into an offscreen buffer is not
				   window content; leave it alone. */
			} else if (!(destination->rowBytes & 0x8000)) {
				record->capture_error = paramErr;
			} else if ((PixMap *)source == window_pixmap) {
				replay_source = (BitMap *)capture_pixmap;
			} else if (!(source->rowBytes & 0x8000)) {
				record->capture_error = paramErr;
			} else {
				record->op_counts[5]++;
				((NativeBits)&saved_extra[BITS_HOOK])(replay_source,
					(BitMap *)capture_pixmap, source_rect, destination_rect, mode, mask);
			}
		}
		end_capture(record);
		in_callback = 0;
	}
	in_callback = 1;
	((NativeBits)&saved_extra[BITS_HOOK])(source, destination,
		source_rect, destination_rect, mode, mask);
	in_callback = nested;
}

static pascal void capture_scroll(Rect *rect, short dh, short dv, RgnHandle update) {
	int nested = in_callback;
	WindowInfo *record = NULL;
	if (!in_callback) {
		in_callback = 1;
		record = begin_capture();
		if (record) {
			record->op_counts[6]++;
			SetEmptyRgn(scroll_update);
			((NativeScroll)&saved_extra[SCROLL_HOOK])(rect, dh, dv, scroll_update);
		}
		end_capture(record);
		in_callback = 0;
	}
	in_callback = 1;
	((NativeScroll)&saved_extra[SCROLL_HOOK])(rect, dh, dv, update);
	in_callback = nested;
}

static pascal void observe_dispose(WindowPtr window) {
	ProcessSerialNumber process;
	if (!in_callback && GetCurrentProcess(&process) == noErr) {
		WindowInfo *record = find_window(window, &process);
		if (record) {
			THz previous_zone = GetZone();
			SetZone(probe_system_zone());
			dispose_color_surface(&record->color_surface);
			SetZone(previous_zone);
			record->capture_ok = 0;
			record->used = 0;
		}
	}
	((NativeDisposeWindow)&saved_dispose)(window);
}

static void update_visibility(WindowPtr window, int visible) {
	ProcessSerialNumber process;
	WindowInfo *record;
	if (!in_callback && GetCurrentProcess(&process) == noErr) {
		record = find_window(window, &process);
		if (record)
			record->visible = visible;
	}
}

static pascal void observe_hide(WindowPtr window) {
	update_visibility(window, 0);
	((pascal void (*)(WindowPtr))&saved_hide)(window);
}

static pascal void observe_show(WindowPtr window) {
	update_visibility(window, 1);
	((pascal void (*)(WindowPtr))&saved_show)(window);
}

static pascal void observe_close(WindowPtr window) {
	update_visibility(window, 0);
	((pascal void (*)(WindowPtr))&saved_close)(window);
}

static pascal void observe_set_title(WindowPtr window, ConstStringPtr title) {
	ProcessSerialNumber process;
	WindowInfo *record;
	if (!in_callback && GetCurrentProcess(&process) == noErr) {
		record = find_window(window, &process);
		if (!record)
			record = add_window(window, &process);
		if (record && title)
			memcpy(record->title, title, title[0] + 1);
	}
	((pascal void (*)(WindowPtr, ConstStringPtr))&saved_set_title)(window, title);
}

static const NativeVector *extra_callback(unsigned int hook) {
	switch (hook) {
		case TEXT_HOOK: return (const NativeVector *)capture_text;
		case FRAME_HOOK: return (const NativeVector *)capture_frame;
		case LINE_HOOK: return (const NativeVector *)capture_line;
		case BITS_HOOK: return (const NativeVector *)capture_bits;
		case SCROLL_HOOK: return (const NativeVector *)capture_scroll;
		case STDTEXT_HOOK: return (const NativeVector *)capture_std_text;
	}
	return NULL;
}

static int start_observer(void) {
	static const unsigned char library[] = "\pInterfaceLib";
	static const unsigned char paint_name[] = "\pPaintRect";
	static const unsigned char erase_name[] = "\pEraseRect";
	static const unsigned char dispose_name[] = "\pDisposeWindow";
	static const unsigned char set_title_name[] = "\pSetWTitle";
	Str255 error_name;
	Ptr main_address, address;
	SymClass symbol_class;
	OSErr error;
	unsigned int i;

	error = GetSharedLibrary(library, 'pwpc', kLoadLib, &interface_connection,
		&main_address, error_name);
	if (error != noErr)
		return error;
	interface_open = 1;
	error = FindSymbol(interface_connection, paint_name, &address, &symbol_class);
	if (error != noErr || symbol_class != NATIVE_TVECTOR_SYMBOL)
		return error != noErr ? error : paramErr;
	native_paint = (NativeVector *)address;
	error = FindSymbol(interface_connection, erase_name, &address, &symbol_class);
	if (error != noErr || symbol_class != NATIVE_TVECTOR_SYMBOL)
		return error != noErr ? error : paramErr;
	native_erase = (NativeVector *)address;
	error = FindSymbol(interface_connection, dispose_name, &address, &symbol_class);
	if (error != noErr || symbol_class != NATIVE_TVECTOR_SYMBOL)
		return error != noErr ? error : paramErr;
	native_dispose = (NativeVector *)address;
	error = FindSymbol(interface_connection, set_title_name, &address, &symbol_class);
	if (error != noErr || symbol_class != NATIVE_TVECTOR_SYMBOL)
		return error != noErr ? error : paramErr;
	native_set_title = (NativeVector *)address;
	{
		static const unsigned char hide_name[] = "\pHideWindow";
		static const unsigned char show_name[] = "\pShowWindow";
		static const unsigned char close_name[] = "\pCloseWindow";
		struct { const unsigned char *name; NativeVector **vector; } lifecycle[] = {
			{ hide_name, &native_hide }, { show_name, &native_show },
			{ close_name, &native_close }
		};
		unsigned int n;
		for (n = 0; n < 3; n++) {
			error = FindSymbol(interface_connection, lifecycle[n].name, &address,
				&symbol_class);
			if (error == noErr && symbol_class == NATIVE_TVECTOR_SYMBOL)
				*lifecycle[n].vector = (NativeVector *)address;
			else
				*lifecycle[n].vector = NULL;
		}
	}
	for (i = 0; i < EXTRA_HOOKS; i++) {
		Str255 name;
		name[0] = strlen(extra_names[i]);
		memcpy(name + 1, extra_names[i], name[0]);
		error = FindSymbol(interface_connection, name, &address, &symbol_class);
		if (error != noErr || symbol_class != NATIVE_TVECTOR_SYMBOL) {
			if (i == STDTEXT_HOOK) {
				extra_vectors[i] = NULL;
				continue;
			}
			return error != noErr ? error : paramErr;
		}
		extra_vectors[i] = (NativeVector *)address;
		saved_extra[i] = *extra_vectors[i];
	}
	scroll_update = NewRgn();
	if (!scroll_update)
		return memFullErr;
	saved_paint = *native_paint;
	saved_erase = *native_erase;
	saved_dispose = *native_dispose;
	saved_set_title = *native_set_title;
	if (native_hide) saved_hide = *native_hide;
	if (native_show) saved_show = *native_show;
	if (native_close) saved_close = *native_close;
	*native_paint = *(const NativeVector *)observe_paint;
	*native_erase = *(const NativeVector *)observe_erase;
	*native_dispose = *(const NativeVector *)observe_dispose;
	*native_set_title = *(const NativeVector *)observe_set_title;
	if (native_hide) *native_hide = *(const NativeVector *)observe_hide;
	if (native_show) *native_show = *(const NativeVector *)observe_show;
	if (native_close) *native_close = *(const NativeVector *)observe_close;
	for (i = 0; i < EXTRA_HOOKS; i++) {
		if (extra_vectors[i])
			*extra_vectors[i] = *extra_callback(i);
	}
	native_hooked = 1;
	return noErr;
}

static void stop_observer(void) {
	unsigned int i;
	if (!native_hooked)
		return;
	*native_paint = saved_paint;
	*native_erase = saved_erase;
	*native_dispose = saved_dispose;
	*native_set_title = saved_set_title;
	if (native_hide) *native_hide = saved_hide;
	if (native_show) *native_show = saved_show;
	if (native_close) *native_close = saved_close;
	for (i = 0; i < EXTRA_HOOKS; i++) {
		if (extra_vectors[i])
			*extra_vectors[i] = saved_extra[i];
	}
	native_hooked = 0;
	for (i = 0; i < window_count; i++) {
		THz previous_zone = GetZone();
		SetZone(probe_system_zone());
		dispose_color_surface(&windows[i].color_surface);
		SetZone(previous_zone);
	}
	if (scroll_update) {
		DisposeRgn(scroll_update);
		scroll_update = NULL;
	}
}

static OSErr export_window_ppm(WindowInfo *record) {
	char name[64], header[180];
	Str255 pname;
	short file, volume;
	unsigned long stride;
	int width, height, x, y, length;
	OSErr error, close_error;
	static unsigned char row[1024 * 3];
	if (!record->capture_ok || !record->color_surface.pixmap)
		return 0;
	width = record->color_surface.width;
	height = record->color_surface.height;
	if (width < 1 || width > 1024 || height < 1 || height > 768)
		return paramErr;
	stride = color_row_bytes(record->color_surface.pixmap);
	snprintf(name, sizeof(name), "ZacWin-%u.ppm", record->serial);
	make_pstring(pname, name);
	error = HCreate(unix_vref, ROOT_DIR_ID, pname, 'ZcWP', 'PPM ');
	if (error != noErr && error != dupFNErr)
		return error;
	error = HOpenDF(unix_vref, ROOT_DIR_ID, pname, fsRdWrPerm, &file);
	if (error != noErr)
		return error;
	error = SetEOF(file, 0);
	if (error == noErr)
		error = GetVRefNum(file, &volume);
	length = snprintf(header, sizeof(header),
		"P6\n# zacos9-classic-window 1 %u %lu\n%d %d\n255\n",
		record->serial, record->frame, width, height);
	if (error == noErr)
		error = write_bytes(file, header, length);
	for (y = 0; y < height && error == noErr; y++) {
		const unsigned char *source = (const unsigned char *)GetPixBaseAddr(
			record->color_surface.pixmap) + (unsigned long)y * stride;
		for (x = 0; x < width; x++)
			color_sample(*record->color_surface.pixmap, source, x, row + x * 3);
		error = write_bytes(file, row, width * 3);
	}
	close_error = FSClose(file);
	if (error == noErr)
		error = close_error;
	if (error == noErr)
		error = FlushVol(NULL, volume);
	return error;
}

static void write_window_meta(WindowInfo *record, int length) {
	char name[64], text[360];
	snprintf(name, sizeof(name), "ZacWin-%u.txt", record->serial);
	length = snprintf(text, sizeof(text),
		"version=1\nserial=%u\napp=\"%.*s\"\ntitle=\"%.*s\"\n"
		"psn=%lu:%lu\nsig=%08lx\nkind=%d\nvisible=%d\ncolor=%d\ndepth=%d\n"
		"bounds=%d,%d,%d,%d\nglobal=%d,%d,%d,%d\n"
		"frame=%d,%d,%d,%d\ngoAway=%d\nwidth=%d\nheight=%d\n"
		"frameNumber=%lu\ncalls=%lu\nfile=ZacWin-%u.ppm\n",
		record->serial, record->app_name[0], record->app_name + 1,
		record->title[0], record->title + 1,
		(unsigned long)record->process.highLongOfPSN,
		(unsigned long)record->process.lowLongOfPSN,
		(unsigned long)record->signature, record->kind, record->visible,
		record->color, record->depth, record->bounds.left,
		record->bounds.top, record->bounds.right, record->bounds.bottom,
		record->global.left, record->global.top,
		record->global.right, record->global.bottom,
		record->window_frame.left, record->window_frame.top,
		record->window_frame.right, record->window_frame.bottom, record->go_away,
		record->color_surface.width, record->color_surface.height,
		record->frame, record->calls, record->serial);
	write_text_file(name, text, length);
}

static void write_report(void) {
	char text[4096], line[360];
	int length = 0, i;
	unsigned int n;
	length += snprintf(text + length, sizeof(text) - length,
		"version=1\ncount=%u\noverflow=%u\nhooks=%d\nerror=%d\n"
		"ticks=%lu draws=%lu matches=%lu offscreen=%lu process_errors=%lu\n"
		"captures=%u captured_ops=%lu\n",
		window_count, overflow, native_hooked, last_error,
		(unsigned long)TickCount(), draw_calls, window_matches,
		offscreen_draws, process_errors, capture_count, captured_ops);
	for (i = 0, n = 0; i < (int)window_count && length < (int)sizeof(text) - 400; i++) {
		WindowInfo *record = &windows[i];
		if (!record->used)
			continue;
		{
			int line_length = snprintf(line, sizeof(line),
				"window=%u serial=%u psn=%lu:%lu app=\"%.*s\" sig=%08lx "
				"kind=%d visible=%d color=%d depth=%d bounds=%d,%d,%d,%d "
				"global=%d,%d,%d,%d frame=%d,%d,%d,%d goAway=%d "
				"title=\"%.*s\" calls=%lu frame=%lu capture=%d error=%d "
				"ops=%lu,%lu,%lu,%lu,%lu,%lu,%lu bk=%08lx rgbbk=%06lx clips=%d,%d,%d,%d\n",
				n++, record->serial,
				(unsigned long)record->process.highLongOfPSN,
				(unsigned long)record->process.lowLongOfPSN,
				record->app_name[0], record->app_name + 1,
				(unsigned long)record->signature, record->kind,
				record->visible, record->color, record->depth,
				record->bounds.left, record->bounds.top,
				record->bounds.right, record->bounds.bottom,
				record->global.left, record->global.top,
				record->global.right, record->global.bottom,
				record->window_frame.left, record->window_frame.top,
				record->window_frame.right, record->window_frame.bottom, record->go_away,
				record->title[0], record->title + 1, record->calls,
				record->frame, record->capture_ok, record->capture_error,
				record->op_counts[0], record->op_counts[1], record->op_counts[2],
				record->op_counts[3], record->op_counts[4], record->op_counts[5],
				record->op_counts[6], record->bk_color, record->rgb_bk_color,
				record->clip_box.left, record->clip_box.top,
				record->clip_box.right, record->clip_box.bottom);
			if (line_length < 0 ||
					length + line_length >= (int)sizeof(text) - 1)
				break;
			memcpy(text + length, line, line_length);
			length += line_length;
		}
	}
	text[length] = 0;
	if (unix_vref == 0)
		unix_vref = find_unix_volume();
	if (unix_vref == 0) {
		last_error = fnfErr;
		return;
	}
	last_error = write_text_file("ClassicWindows.txt", text, length);
	if (last_error != noErr)
		write_error("report", last_error);
}

int main(void) {
	EventRecord event;
	OSErr error;
	unsigned long last_scan, last_export;
	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(NULL);
	OpenPort(&agent_port);
	SetPort(&agent_port);
	unix_vref = find_unix_volume();
	error = start_observer();
	if (error != noErr) {
		last_error = error;
		write_error("start", error);
	}
	last_scan = TickCount();
	last_export = last_scan;
	write_report();
	for (;;) {
		if (TickCount() - last_scan >= SCAN_TICKS) {
			last_scan = TickCount();
			write_report();
		}
		if (TickCount() - last_export >= EXPORT_TICKS) {
			unsigned int i;
			last_export = TickCount();
			for (i = 0; i < window_count; i++) {
				if (windows[i].used && windows[i].capture_ok && windows[i].dirty) {
					if (export_window_ppm(&windows[i]) == noErr) {
						write_window_meta(&windows[i], 0);
						windows[i].dirty = 0;
					}
				}
			}
		}
		if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
			if ((event.what == keyDown || event.what == autoKey) &&
					(event.modifiers & cmdKey) &&
					((event.message & charCodeMask) == 'q' ||
					 (event.message & charCodeMask) == 'Q'))
				break;
		}
	}
	stop_observer();
	if (interface_open)
		CloseConnection(&interface_connection);
	return 0;
}
