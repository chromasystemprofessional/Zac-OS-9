#include <Quickdraw.h>
#include <Windows.h>
#include <Dialogs.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <MacMemory.h>
#include <Files.h>
#include <Errors.h>
#include <stdio.h>
#include <string.h>

#include "pattern.h"
#include "surface.h"

static WindowPtr windows[2];
static Surface reference[2];
#ifdef CLASSIC_PROBE_CAPTURE
static Surface candidate[2];
static QDProcs capture_procs;
static QDRectUPP rect_callback;
#endif
static GrafPort safe_port;
static unsigned long frame_number;
static int paused;
static int graphics_mode;
static int color_mode;
static RgnHandle graphics_clip, graphics_saved_clip, graphics_update;
static Surface image;
static ColorSurface color_reference[2];
#ifdef CLASSIC_COLOR_AUTO
static int auto_exported;
#endif

static void dispose_graphics(void) {
	if (graphics_clip) {
		DisposeRgn(graphics_clip);
		graphics_clip = NULL;
	}
	if (graphics_saved_clip) {
		DisposeRgn(graphics_saved_clip);
		graphics_saved_clip = NULL;
	}
	if (graphics_update) {
		DisposeRgn(graphics_update);
		graphics_update = NULL;
	}
	dispose_surface(&image);
}

#ifdef CLASSIC_PROBE_CAPTURE
static pascal void capture_rectangle(GrafVerb verb, const Rect *rect) {
	GrafPtr original;
	unsigned int i;
	GetPort(&original);
	for (i = 0; i < 2; i++) {
		if (original == windows[i] && candidate[i].pixels) {
			SetPort(&candidate[i].port);
			/* The fixture only uses black/white PaintRect and EraseRect. */
			ForeColor(blackColor);
			BackColor(whiteColor);
			PenNormal();
			StdRect(verb, rect);
			SetPort(original);
			break;
		}
	}
	StdRect(verb, rect);
}
#endif

static void paint_pattern(GrafPtr port, unsigned int id) {
	GrafPtr previous;
	Rect cell;
	int x, y;
	GetPort(&previous);
	SetPort(port);
	ForeColor(blackColor);
	BackColor(whiteColor);
	PenNormal();
	port->txFont = 0;
	port->txFace = 0;
	port->txMode = srcOr;
	port->txSize = 12;
	port->spExtra = 0;
	RectRgn(port->clipRgn, &port->portRect);
	for (y = 0; y < port->portRect.bottom; y += 8) {
		for (x = 0; x < port->portRect.right; x += 8) {
			int right = x + 8 < port->portRect.right ? x + 8 : port->portRect.right;
			int bottom = y + 8 < port->portRect.bottom ? y + 8 : port->portRect.bottom;
			SetRect(&cell, x, y, right, bottom);
			if (probe_black(id, frame_number, x, y)) {
				PaintRect(&cell);
			} else {
				EraseRect(&cell);
			}
		}
	}
	if (graphics_mode) {
		static const char text[] = "ZacOS drawing state";
		Rect source, destination;
		GetClip(graphics_saved_clip);
		SetRect(&cell, 16, 16, port->portRect.right - 16, port->portRect.bottom - 16);
		RectRgn(graphics_clip, &cell);
		SetClip(graphics_clip);
		ForeColor(whiteColor);
		BackColor(blackColor);
		PenSize(3, 2);
		PenPat(&qd.gray);
		PenMode(patXor);
		FrameRect(&cell);
		MoveTo(12, 24);
		LineTo(port->portRect.right - 12, port->portRect.bottom - 24);
		ForeColor(blackColor);
		BackColor(whiteColor);
		PenNormal();
		TextFont(0);
		TextSize(14);
		TextFace(bold | underline);
		TextMode(srcXor);
		SpaceExtra(0x8000);
		MoveTo(20, 48);
		DrawText((Ptr)text, 0, sizeof(text) - 1);
		TextFace(italic);
		TextSize(10);
		TextMode(srcOr);
		MoveTo(28, 64);
		DrawText((Ptr)text, 6, 7);
		SetRect(&source, 0, 0, 32, 24);
		SetRect(&destination, 24, 72, 88, 96);
		CopyBits(&image.port.portBits, &port->portBits,
			&source, &destination, srcXor, graphics_clip);
		source = destination;
		SetRect(&destination, 104, 72, 168, 96);
		CopyBits(&port->portBits, &port->portBits,
			&source, &destination, srcCopy, NULL);
		SetRect(&cell, 16, 104, port->portRect.right - 16, port->portRect.bottom - 16);
		if (cell.bottom > cell.top) {
			ScrollRect(&cell, (frame_number & 1) ? 7 : -5, 3, graphics_update);
		}
		SetClip(graphics_saved_clip);
	}
	SetPort(previous);
}

static void set_rgb(RGBColor *color, const unsigned char rgb[3]) {
	color->red = (uint16_t)(rgb[0] * 257);
	color->green = (uint16_t)(rgb[1] * 257);
	color->blue = (uint16_t)(rgb[2] * 257);
}

#ifndef CLASSIC_PROBE_CAPTURE
static int main_depth(void) {
	PixMapHandle pixmap = (**GetMainDevice()).gdPMap;
	return pixmap ? (**pixmap).pixelSize : 0;
}
#endif

static void paint_color_port(CGrafPtr port, int offscreen, unsigned int id) {
	static const char text[] = "ZacOS color state";
	static RGBColor gray = { 0x8000, 0x8000, 0x8000 };
	static RGBColor white = { 0xffff, 0xffff, 0xffff };
	static RGBColor red = { 0xffff, 0x0000, 0x0000 };
	static RGBColor blue = { 0x0000, 0x0000, 0xffff };
	CGrafPtr previous;
	GDHandle device;
	Rect cell, clip_rect;
	unsigned char rgb[3];
	RGBColor color;
	int x, y;
	GetGWorld(&previous, &device);
	SetGWorld(port, offscreen ? NULL : GetMainDevice());
	PenNormal();
	TextFont(0);
	TextFace(0);
	TextSize(12);
	TextMode(srcOr);
	SpaceExtra(0);
	for (y = 0; y < port->portRect.bottom; y += 8) {
		for (x = 0; x < port->portRect.right; x += 8) {
			int right = x + 8 < port->portRect.right ? x + 8 : port->portRect.right;
			int bottom = y + 8 < port->portRect.bottom ? y + 8 : port->portRect.bottom;
			SetRect(&cell, x, y, right, bottom);
			probe_color(id, frame_number, x, y, rgb);
			set_rgb(&color, rgb);
			RGBForeColor(&color);
			if (y < PROBE_COLOR_SCENE_ROWS || !(((x / 8) + (y / 8)) & 1)) {
				PaintRect(&cell);
			} else {
				RGBBackColor(&color);
				EraseRect(&cell);
			}
		}
	}
	if (port->portRect.bottom > PROBE_COLOR_SCENE_ROWS) {
		Rect body, source, destination, scroll;
		SetRect(&body, 4, PROBE_COLOR_SCENE_ROWS, port->portRect.right - 4,
			port->portRect.bottom - 4);
		if (body.right > body.left && body.bottom > body.top) {
			GetClip(graphics_saved_clip);
			SetRect(&clip_rect, body.left, body.top, body.right, body.bottom);
			RectRgn(graphics_clip, &clip_rect);
			SetClip(graphics_clip);
			RGBForeColor(&gray);
			RGBBackColor(&white);
			PenPat(&qd.gray);
			PenSize(3, 2);
			PenMode(patXor);
			FrameRect(&body);
			RGBForeColor(&red);
			MoveTo(body.left + 4, body.top + 4);
			LineTo(body.right - 4, body.bottom - 4);
			RGBForeColor(&blue);
			TextFont(0);
			TextSize(14);
			TextFace(bold | underline);
			TextMode(srcXor);
			SpaceExtra(0x8000);
			MoveTo(body.left + 8, body.top + 24);
			DrawText((Ptr)text, 0, sizeof(text) - 1);
			TextFace(italic);
			TextSize(10);
			TextMode(srcOr);
			SpaceExtra(0);
			MoveTo(body.left + 16, body.top + 40);
			DrawText((Ptr)text, 6, 7);
			if (body.bottom - body.top >= 96) {
				SetRect(&cell, 0, 0, 32, 24);
				SetRect(&destination, body.left + 24, body.top + 48,
					body.left + 88, body.top + 72);
				CopyBits((BitMap*)*port->portPixMap, (BitMap*)*port->portPixMap,
					&cell, &destination, srcXor, graphics_clip);
				source = destination;
				SetRect(&destination, body.left + 104, body.top + 48,
					body.left + 168, body.top + 72);
				CopyBits((BitMap*)*port->portPixMap, (BitMap*)*port->portPixMap,
					&source, &destination, srcCopy, NULL);
			}
			if (body.bottom - body.top >= 128) {
				SetRect(&scroll, body.left + 8, body.top + 72, body.right - 8,
					body.bottom - 8);
				if (scroll.bottom > scroll.top) {
					ScrollRect(&scroll, (frame_number & 1) ? 7 : -5, 3,
						graphics_update);
				}
			}
			SetClip(graphics_saved_clip);
		}
	}
	SetGWorld(previous, device);
}

static void render_color(void) {
	unsigned int i;
	for (i = 0; i < 2; i++) {
		if (windows[i] && color_reference[i].world) {
			paint_color_port((CGrafPtr)color_reference[i].world, 1, i);
			paint_color_port((CGrafPtr)windows[i], 0, i);
		}
	}
}

static void render(void) {
	unsigned int i;
	if (color_mode) {
		render_color();
		return;
	}
	for (i = 0; i < 2; i++) {
		if (windows[i]) {
			paint_pattern(&reference[i].port, i);
			paint_pattern(windows[i], i);
		}
	}
}

static void export_color_frames(void) {
	unsigned int i;
	int success = 1;
	render_color();
	for (i = 0; i < 2; i++) {
		if (!windows[i] || !color_reference[i].world) {
			continue;
		}
		if (!export_color_pixmap(color_reference[i].pixmap, color_reference[i].width,
				color_reference[i].height, i, "reference", frame_number, "ZacProbe") ||
				!export_color_pixmap(((CGrafPtr)windows[i])->portPixMap,
					color_reference[i].width, color_reference[i].height, i,
					"screen", frame_number, "ZacProbe")) {
			success = 0;
			break;
		}
	}
	if (success) {
#ifndef CLASSIC_COLOR_AUTO
		message("Exported color references and screen crops as ZacProbe-*.ppm. Candidate PPMs must match the references exactly; obscured screen crops are the negative control.");
#endif
	}
}

static void export_frames(void) {
	unsigned int i;
	GrafPtr previous;
	int success = 1;
	if (color_mode) {
		export_color_frames();
		return;
	}
	render();
	GetPort(&previous);
	for (i = 0; i < 2; i++) {
		Surface capture;
		if (!windows[i]) {
			continue;
		}
		if (!export_surface_profile(&reference[i], i, "reference", frame_number, "ZacProbe", graphics_mode)
#ifdef CLASSIC_PROBE_CAPTURE
				|| !export_surface(&candidate[i], i, "candidate", frame_number, "ZacProbe")
#endif
				) {
			success = 0;
			break;
		}
		if (!create_surface(&capture, reference[i].width, reference[i].height)) {
			message("Could not allocate a screen capture buffer.");
			success = 0;
			break;
		}
		SetPort(&capture.port);
		ForeColor(blackColor);
		BackColor(whiteColor);
		CopyBits(&windows[i]->portBits, &capture.port.portBits,
			&windows[i]->portRect, &capture.port.portRect, srcCopy, NULL);
		SetPort(previous);
		success = export_surface_profile(&capture, i, "screen", frame_number, "ZacProbe", graphics_mode);
		dispose_surface(&capture);
		if (!success) {
			break;
		}
	}
	if (success) {
#ifdef CLASSIC_PROBE_CAPTURE
		message("Exported reference buffers, rectangle-hook candidates and screen crops as ZacProbe-*.pbm. This tests rectangle interception only, not general seamless windows.");
#else
		message("Exported references and screen crops as ZacProbe-*.pbm. This separate drawing fixture installs no capture hooks.");
#endif
	}
}

static int close_window(unsigned int i) {
	if (Alert(129, NULL) == 2) {
		SetPort(&safe_port);
		DisposeWindow(windows[i]);
		windows[i] = NULL;
		dispose_surface(&reference[i]);
#ifdef CLASSIC_PROBE_CAPTURE
		dispose_surface(&candidate[i]);
#endif
		dispose_color_surface(&color_reference[i]);
		return 1;
	}
	return 0;
}

static int create_probe_windows(void) {
	static const unsigned char mono_a[] = "\pClassic Probe A";
	static const unsigned char mono_b[] = "\pClassic Probe B";
	static const unsigned char color_a[] = "\pClassic Color A";
	static const unsigned char color_b[] = "\pClassic Color B";
	Rect bounds;
	unsigned int i;
	SetRect(&bounds, 100, 100, 420, 292);
	for (i = 0; i < 2; i++) {
		ConstStr255Param title = color_mode ?
			(i == 0 ? color_a : color_b) : (i == 0 ? mono_a : mono_b);
		if (color_mode) {
			PixMapHandle pixmap;
			int depth;
			windows[i] = NewCWindow(NULL, &bounds, title, true, documentProc,
				(WindowPtr)-1, true, i);
			if (!windows[i]) {
				return 0;
			}
			/* The helper requires standard drawing procedures. */
			((CGrafPtr)windows[i])->grafProcs = NULL;
			pixmap = ((CGrafPtr)windows[i])->portPixMap;
			depth = pixmap ? (**pixmap).pixelSize : 0;
			if (!create_color_surface(&color_reference[i], 320, 192, depth)) {
				return 0;
			}
		} else {
			windows[i] = NewWindow(NULL, &bounds, title, true, documentProc,
				(WindowPtr)-1, true, i);
			if (!windows[i] || !create_surface(&reference[i], 320, 192)
#ifdef CLASSIC_PROBE_CAPTURE
					|| !create_surface(&candidate[i], 320, 192)
#endif
					) {
				return 0;
			}
		}
	}
	return 1;
}

static void dispose_probe_windows(void) {
	unsigned int i;
	for (i = 0; i < 2; i++) {
		if (windows[i]) {
			SetPort(&safe_port);
			DisposeWindow(windows[i]);
			windows[i] = NULL;
		}
		dispose_surface(&reference[i]);
#ifdef CLASSIC_PROBE_CAPTURE
		dispose_surface(&candidate[i]);
#endif
		dispose_color_surface(&color_reference[i]);
	}
	SetPort(&safe_port);
}

#ifndef CLASSIC_PROBE_CAPTURE
static void set_color_mode(int enabled) {
	if (enabled == color_mode) {
		return;
	}
	if (enabled) {
		int depth = main_depth();
		if (depth != 16 && depth != 32) {
			message("Color mode needs a Thousands or Millions display. The helper supports direct 16- and 32-bit color ports only.");
			return;
		}
	}
	dispose_probe_windows();
	color_mode = enabled;
	if (!create_probe_windows()) {
		message("Could not create the color probe windows.");
		dispose_probe_windows();
		color_mode = 0;
		if (!create_probe_windows()) {
			message("Could not recreate the monochrome probe windows.");
		}
	}
	CheckItem(GetMenu(128), 6, graphics_mode && !color_mode);
	CheckItem(GetMenu(128), 7, color_mode);
	render();
}
#endif

static void menu_command(long command) {
	short menu = command >> 16;
	short item = command & 0xffff;
	unsigned int i;
	if (menu == 128) {
		if (item == 1) {
			export_frames();
		} else if (item == 2) {
			paused = !paused;
			CheckItem(GetMenu(128), 2, paused);
		} else if (item == 3) {
			for (i = 0; i < 2; i++) {
				if (windows[i] && windows[i] != FrontWindow()) {
					SelectWindow(windows[i]);
					break;
				}
			}
		} else if (item == 5) {
			for (i = 0; i < 2; i++) {
				if (windows[i]) {
					if (!close_window(i)) {
						break;
					}
				}
			}
		} else if (item == 6) {
#ifdef CLASSIC_PROBE_CAPTURE
			message("Graphics mode is for the separate hook-free drawing fixture, not the rectangle-only in-process probe.");
#else
			if (color_mode) {
				message("Graphics mode applies to the monochrome scene; color mode always includes its graphics overlay.");
			} else {
				graphics_mode = !graphics_mode;
				CheckItem(GetMenu(128), 6, graphics_mode);
				render();
			}
#endif
		} else if (item == 7) {
#ifdef CLASSIC_PROBE_CAPTURE
			message("Color mode is for the separate hook-free fixture, not the in-process rectangle probe.");
#else
			set_color_mode(!color_mode);
#endif
		}
	}
	HiliteMenu(0);
}

int main(void) {
	Rect limits;
	EventRecord event;
	MenuHandle menu;
	unsigned long lastTick;
	unsigned int i;
	static const unsigned char menu_title[] = "\pProbe";
	static const unsigned char menu_items[] =
		"\pExport snapshots/E;Pause/P;Switch window/W;(-;Quit/Q;Graphics mode/M;Color mode/K";
	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(NULL);
	InitCursor();
	OpenPort(&safe_port);
	graphics_clip = NewRgn();
	graphics_saved_clip = NewRgn();
	graphics_update = NewRgn();
	if (!graphics_clip || !graphics_saved_clip || !graphics_update ||
			!create_surface(&image, 32, 24)) {
		message("Could not initialize graphics test resources.");
		dispose_graphics();
		ClosePort(&safe_port);
		return 1;
	}
	{
		int row;
		for (row = 0; row < 24; row++) {
			memset(image.pixels + row * image.stride, (row & 1) ? 0x99 : 0x66, image.stride);
		}
	}
#ifdef CLASSIC_PROBE_CAPTURE
	rect_callback = NewQDRectUPP(capture_rectangle);
	if (!rect_callback) {
		message("Could not allocate the rectangle capture callback.");
		dispose_graphics();
		ClosePort(&safe_port);
		return 1;
	}
	SetStdProcs(&capture_procs);
	capture_procs.rectProc = rect_callback;
#endif
	menu = NewMenu(128, menu_title);
	if (!menu) {
		message("Could not create the probe menu.");
		dispose_graphics();
#ifdef CLASSIC_PROBE_CAPTURE
		DisposeQDRectUPP(rect_callback);
#endif
		ClosePort(&safe_port);
		return 1;
	}
	AppendMenu(menu, menu_items);
	InsertMenu(menu, 0);
	DrawMenuBar();
#ifdef CLASSIC_COLOR_AUTO
	color_mode = 1;
#endif
	if (!create_probe_windows()) {
		message("Could not initialize both probe windows.");
		dispose_probe_windows();
#ifdef CLASSIC_PROBE_CAPTURE
		DisposeQDRectUPP(rect_callback);
#endif
		DeleteMenu(128);
		DisposeMenu(menu);
		dispose_graphics();
		ClosePort(&safe_port);
		return 1;
	}
#ifdef CLASSIC_PROBE_CAPTURE
	for (i = 0; i < 2; i++) {
		windows[i]->grafProcs = &capture_procs;
	}
#endif
	lastTick = TickCount();
	while (windows[0] || windows[1]) {
		if (WaitNextEvent(everyEvent, &event, 1, NULL)) {
			if (event.what == updateEvt) {
				WindowPtr window = (WindowPtr)event.message;
				BeginUpdate(window);
				for (i = 0; i < 2; i++) {
					if (window == windows[i]) {
						if (color_mode) {
							paint_color_port((CGrafPtr)window, 0, i);
						} else {
							paint_pattern(window, i);
						}
					}
				}
				EndUpdate(window);
			} else if (event.what == mouseDown) {
				WindowPtr window;
				short part = FindWindow(event.where, &window);
				if (part == inMenuBar) {
					menu_command(MenuSelect(event.where));
				} else if (part == inDrag) {
					SetRect(&limits, 0, 24, 1600, 1200);
					DragWindow(window, event.where, &limits);
				} else if (part == inContent) {
					SelectWindow(window);
				} else if (part == inGoAway && TrackGoAway(window, event.where)) {
					for (i = 0; i < 2; i++) {
						if (window == windows[i]) {
							close_window(i);
						}
					}
				} else if (part == inGrow) {
					long size;
					SetRect(&limits, 64, 64, 641, 385);
					size = GrowWindow(window, event.where, &limits);
					if (size) {
						for (i = 0; i < 2; i++) {
							if (window == windows[i]) {
								if (color_mode) {
									ColorSurface replacement;
									PixMapHandle pixmap;
									SizeWindow(window, size & 0xffff, size >> 16, true);
									pixmap = ((CGrafPtr)window)->portPixMap;
									if (create_color_surface(&replacement,
											size & 0xffff, size >> 16,
											pixmap ? (**pixmap).pixelSize : 0)) {
										dispose_color_surface(&color_reference[i]);
										color_reference[i] = replacement;
										render();
									} else {
										message("Could not allocate the resized color reference buffer.");
									}
								} else {
									Surface replacement;
#ifdef CLASSIC_PROBE_CAPTURE
									Surface captured;
#endif
									if (create_surface(&replacement, size & 0xffff, size >> 16)) {
#ifdef CLASSIC_PROBE_CAPTURE
										if (create_surface(&captured, size & 0xffff, size >> 16)) {
#endif
											dispose_surface(&reference[i]);
#ifdef CLASSIC_PROBE_CAPTURE
											dispose_surface(&candidate[i]);
#endif
											reference[i] = replacement;
#ifdef CLASSIC_PROBE_CAPTURE
											candidate[i] = captured;
#endif
											SizeWindow(window, size & 0xffff, size >> 16, true);
											render();
#ifdef CLASSIC_PROBE_CAPTURE
										} else {
											dispose_surface(&replacement);
											message("Could not allocate resized candidate buffer.");
										}
#endif
									} else {
										message("Could not allocate resized reference buffer.");
									}
								}
							}
						}
					}
				}
			} else if ((event.what == keyDown || event.what == autoKey) &&
					(event.modifiers & cmdKey)) {
				char key = event.message & charCodeMask;
				/* MenuKey is case-sensitive for this Toolbox version; handle
				   the experiment's shortcuts directly. */
				if (key == 'p' || key == 'P') {
					paused = !paused;
					CheckItem(GetMenu(128), 2, paused);
#ifndef CLASSIC_PROBE_CAPTURE
				} else if (key == 'k' || key == 'K') {
					set_color_mode(!color_mode);
#endif
				} else if (key == 'e' || key == 'E') {
					export_frames();
				} else if (key == 'w' || key == 'W') {
					for (i = 0; i < 2; i++) {
						if (windows[i] && windows[i] != FrontWindow()) {
							SelectWindow(windows[i]);
							break;
						}
					}
				} else {
					menu_command(MenuKey(key));
				}
			}
		}
		if (!paused && TickCount() - lastTick >= 6) {
			lastTick = TickCount();
			frame_number++;
			render();
#ifdef CLASSIC_COLOR_AUTO
			if (!auto_exported && frame_number >= 10) {
				auto_exported = 1;
				paused = 1;
				export_frames();
			}
#endif
		}
#ifdef CLASSIC_COLOR_AUTO
		else if (auto_exported && TickCount() - lastTick >= 6) {
			/* Keep the paused frame repainting so a helper launched later can
			   still capture exactly the frame the references describe. */
			lastTick = TickCount();
			render();
		}
#endif
	}
	DeleteMenu(128);
	DisposeMenu(menu);
#ifdef CLASSIC_PROBE_CAPTURE
	DisposeQDRectUPP(rect_callback);
#endif
	dispose_graphics();
	ClosePort(&safe_port);
	return 0;
}
