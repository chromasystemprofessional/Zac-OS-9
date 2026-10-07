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

#endif
