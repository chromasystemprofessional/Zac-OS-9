#ifndef ZACOS_CLASSIC_PROBE_SURFACE_H
#define ZACOS_CLASSIC_PROBE_SURFACE_H

#include <Quickdraw.h>
#include <Dialogs.h>
#include <MacMemory.h>
#include <Files.h>
#include <Errors.h>
#include <stdio.h>
#include <string.h>

typedef struct {
	GrafPort port;
	Ptr pixels;
	int width, height, stride;
} Surface;

typedef struct {
	GWorldPtr world;
	PixMapHandle pixmap;
	int width, height, depth;
	int locked;
} ColorSurface;

/* Classic direct-color PixMaps are 16-bit RGB555 or 32-bit ARGB. Other pixel
   types and depths are refused instead of being cast through these layouts. */
enum { DIRECT_PIXEL_TYPE = 16 };

/* GCC 16's array-bounds diagnostic misfires on MemError's absolute-address
   macro at -O3. Keep the read in one out-of-line place. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Warray-bounds"
static OSErr probe_mem_error(void) __attribute__((noinline, unused));
static OSErr probe_mem_error(void) {
	return MemError();
}

static THz probe_system_zone(void) __attribute__((noinline, unused));
static THz probe_system_zone(void) {
	return LMGetSysZone();
}
#pragma GCC diagnostic pop

static inline void message(const char *text) {
	Str255 value;
	static const unsigned char empty[] = "\p";
	size_t size = strlen(text);
	if (size > 255) {
		size = 255;
	}
	value[0] = size;
	memcpy(value + 1, text, size);
	ParamText(value, empty, empty, empty);
	StopAlert(128, NULL);
}

static inline void dispose_surface(Surface *surface) {
	if (surface->pixels) {
		ClosePort(&surface->port);
		DisposePtr(surface->pixels);
		memset(surface, 0, sizeof(*surface));
	}
}

static inline int create_surface(Surface *surface, int width, int height) {
	GrafPtr previous;
	BitMap bitmap;
	GetPort(&previous);
	memset(surface, 0, sizeof(*surface));
	surface->stride = ((width + 15) / 16) * 2;
	surface->pixels = NewPtrClear(surface->stride * height);
	if (!surface->pixels) {
		return 0;
	}
	surface->width = width;
	surface->height = height;
	OpenPort(&surface->port);
	bitmap.baseAddr = surface->pixels;
	bitmap.rowBytes = surface->stride;
	SetRect(&bitmap.bounds, 0, 0, width, height);
	SetPortBits(&bitmap);
	surface->port.portRect = bitmap.bounds;
	RectRgn(surface->port.visRgn, &bitmap.bounds);
	RectRgn(surface->port.clipRgn, &bitmap.bounds);
	SetPort(previous);
	return 1;
}

static inline unsigned long color_row_bytes(PixMapHandle pixmap) {
	return (unsigned long)((**pixmap).rowBytes & 0x3FFF);
}

static inline int color_pixmap_supported(const PixMap *pixmap) {
	if (pixmap->pixelType != DIRECT_PIXEL_TYPE) {
		return 0;
	}
	if (pixmap->pixelSize == 32) {
		return pixmap->cmpCount == 3 && pixmap->cmpSize == 8;
	}
	if (pixmap->pixelSize == 16) {
		return pixmap->cmpCount == 3 && pixmap->cmpSize == 5;
	}
	return 0;
}

static inline void color_sample(const PixMap *pixmap, const unsigned char *row,
		int x, unsigned char rgb[3]) {
	if (pixmap->pixelSize == 32) {
		const unsigned char *pixel = row + x * 4;
		/* k32ARGBPixelFormat in memory order: alpha, red, green, blue. */
		rgb[0] = pixel[1];
		rgb[1] = pixel[2];
		rgb[2] = pixel[3];
	} else if (pixmap->pixelSize == 16) {
		unsigned int value = ((unsigned int)row[x * 2] << 8) | row[x * 2 + 1];
		unsigned int red = (value >> 10) & 31;
		unsigned int green = (value >> 5) & 31;
		unsigned int blue = value & 31;
		/* Replicate the top bits so 31 becomes 255 and 0 stays 0. */
		rgb[0] = (unsigned char)((red << 3) | (red >> 2));
		rgb[1] = (unsigned char)((green << 3) | (green >> 2));
		rgb[2] = (unsigned char)((blue << 3) | (blue >> 2));
	} else {
		rgb[0] = rgb[1] = rgb[2] = 0;
	}
}

static inline int create_color_surface(ColorSurface *surface, int width, int height,
		int depth) {
	Rect bounds;
	QDErr error;
	CGrafPtr port;
	unsigned long stride;
	THz previous_zone;
	memset(surface, 0, sizeof(*surface));
	if ((depth != 16 && depth != 32) || width < 1 || height < 1) {
		return 0;
	}
	SetRect(&bounds, 0, 0, width, height);
	/* The application partition is small and fragmented. Allocate the
	   GWorld in the system zone (plenty of room); LockPixels pins it. */
	previous_zone = GetZone();
	SetZone(probe_system_zone());
	error = NewGWorld(&surface->world, depth, &bounds, NULL, NULL, 0);
	if (error != noErr || !surface->world) {
		SetZone(previous_zone);
		surface->world = NULL;
		return 0;
	}
	surface->pixmap = GetGWorldPixMap(surface->world);
	if (!surface->pixmap || !LockPixels(surface->pixmap)) {
		DisposeGWorld(surface->world);
		SetZone(previous_zone);
		memset(surface, 0, sizeof(*surface));
		return 0;
	}
	surface->locked = 1;
	surface->width = width;
	surface->height = height;
	surface->depth = depth;
	port = (CGrafPtr)surface->world;
	port->portRect = bounds;
	RectRgn(port->visRgn, &bounds);
	RectRgn(port->clipRgn, &bounds);
	if (probe_mem_error() != noErr || !color_pixmap_supported(*surface->pixmap)) {
		UnlockPixels(surface->pixmap);
		DisposeGWorld(surface->world);
		SetZone(previous_zone);
		memset(surface, 0, sizeof(*surface));
		return 0;
	}
	stride = color_row_bytes(surface->pixmap);
	if (stride < (unsigned long)width * (unsigned long)(depth / 8)) {
		UnlockPixels(surface->pixmap);
		DisposeGWorld(surface->world);
		SetZone(previous_zone);
		memset(surface, 0, sizeof(*surface));
		return 0;
	}
	memset(GetPixBaseAddr(surface->pixmap), 0, stride * (unsigned long)height);
	SetZone(previous_zone);
	return 1;
}

static inline void dispose_color_surface(ColorSurface *surface) {
	if (surface->world) {
		THz previous_zone = GetZone();
		SetZone(probe_system_zone());
		if (surface->locked && surface->pixmap) {
			UnlockPixels(surface->pixmap);
		}
		DisposeGWorld(surface->world);
		SetZone(previous_zone);
	}
	memset(surface, 0, sizeof(*surface));
}

static inline OSErr write_bytes(short file, const void *data, long length) {
	long written = length;
	OSErr error = FSWrite(file, &written, data);
	return error != noErr ? error : written == length ? noErr : ioErr;
}

static inline int export_surface_profile(const Surface *surface, unsigned int id,
		const char *mode, unsigned long frame, const char *prefix, int graphics) {
	char name[80], header[120];
	Str255 filename;
	short file, volume;
	int y, length;
	OSErr error, closeError;
	if (graphics && surface->width < 256) {
		message("Graphics capture is too narrow for a complete frame marker. No PBM exported.");
		return 0;
	}
	snprintf(name, sizeof(name), "%s-%u-%s.pbm", prefix, id, mode);
	filename[0] = strlen(name);
	memcpy(filename + 1, name, filename[0]);
	error = HCreate(0, 0, filename, 'ZcSP', 'PBM ');
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
	length = snprintf(header, sizeof(header),
		graphics ? "P4\n# zacos9-classic-probe 2 %s %u %lu graphics\n%d %d\n" :
		"P4\n# zacos9-classic-probe 1 %s %u %lu\n%d %d\n",
		mode, id, frame, surface->width, surface->height);
	if (error == noErr) {
		error = write_bytes(file, header, length);
	}
	for (y = 0; y < surface->height && error == noErr; y++) {
		error = write_bytes(file, surface->pixels + y * surface->stride,
			(surface->width + 7) / 8);
	}
	closeError = FSClose(file);
	if (error == noErr) {
		error = closeError;
	}
	if (error == noErr) {
		error = FlushVol(NULL, volume);
	}
	if (error == noErr) {
		return 1;
	}
failed:
	snprintf(header, sizeof(header), "Export failed for %s (Mac error %d).", name, error);
	message(header);
	return 0;
}

static inline int export_surface(const Surface *surface, unsigned int id,
		const char *mode, unsigned long frame, const char *prefix) {
	return export_surface_profile(surface, id, mode, frame, prefix, 0);
}

static inline int export_color_pixmap(PixMapHandle pixmap, int width, int height,
		unsigned int id, const char *mode, unsigned long frame, const char *prefix) {
	char name[80], header[140];
	Str255 filename;
	short file, volume;
	int x, y, length;
	unsigned long stride;
	OSErr error, closeError;
	static unsigned char row[640 * 3];
	if (!pixmap || !color_pixmap_supported(*pixmap) || width < 1 || width > 640 ||
			height < 1 || height > 384) {
		message("The color port's pixel format is not supported. No PPM exported.");
		return 0;
	}
	if (width < 256) {
		message("Color capture is too narrow for a complete frame marker. No PPM exported.");
		return 0;
	}
	stride = color_row_bytes(pixmap);
	if (stride < (unsigned long)width * (unsigned long)((**pixmap).pixelSize / 8)) {
		message("The color port's row stride is too small. No PPM exported.");
		return 0;
	}
	snprintf(name, sizeof(name), "%s-%u-%s.ppm", prefix, id, mode);
	filename[0] = strlen(name);
	memcpy(filename + 1, name, filename[0]);
	error = HCreate(0, 0, filename, 'ZcSP', 'PPM ');
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
	length = snprintf(header, sizeof(header),
		"P6\n# zacos9-classic-probe 3 %s %u %lu color\n%d %d\n255\n",
		mode, id, frame, width, height);
	if (error == noErr) {
		error = write_bytes(file, header, length);
	}
	for (y = 0; y < height && error == noErr; y++) {
		const unsigned char *source = (const unsigned char *)GetPixBaseAddr(pixmap)
			+ (unsigned long)y * stride;
		for (x = 0; x < width; x++) {
			color_sample(*pixmap, source, x, row + x * 3);
		}
		error = write_bytes(file, row, width * 3);
	}
	closeError = FSClose(file);
	if (error == noErr) {
		error = closeError;
	}
	if (error == noErr) {
		error = FlushVol(NULL, volume);
	}
	if (error == noErr) {
		return 1;
	}
failed:
	snprintf(header, sizeof(header), "Export failed for %s (Mac error %d).", name, error);
	message(header);
	return 0;
}

#endif
