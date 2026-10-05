#ifndef ZACOS9_SETTINGS_H
#define ZACOS9_SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "draw.h"

/*
 * Appearance settings shared by the shell: ~/.config/zacos9/desktop.conf
 * (QSettings INI, "[General]" section), e.g. "pattern=ocean-ripple",
 * "alert-sound=glass". The Appearance and Sound control panels write it.
 */

/* Copies the value of `key` into out; false if it isn't set. The file is
 * re-read whenever it changes, so callers can ask on every paint. */
bool pl_setting(const char *key, char *out, size_t size);
/* Set `key` (writes the file; NULL value removes the key). */
bool pl_setting_set(const char *key, const char *value);

/* Accent colours (scroll thumbs, menu highlights, progress bars). Index 0
 * is Lavender, the default. */
int pl_accent_count(void);
const char *pl_accent_id(int index);
const char *pl_accent_name(int index);
struct pl_accent pl_accent_at(int index);
int pl_accent_find(const char *id); /* 0 if unknown */
/* The chosen accent ("accent=" in desktop.conf). */
struct pl_accent pl_accent_current(void);
/* The text highlight colour (list selections): "highlight=RRGGBB", or
 * the accent's light shade. */
uint32_t pl_highlight_current(void);

/* Where shared data (sounds/) lives: $ZACOS9_DATA, the installed data
 * directory, or the source tree's assets/ when running from a build. */
const char *pl_data_dir(void);

/* Play a sound from sounds/NAME.wav without waiting for it. NULL plays
 * the chosen alert sound ("alert-sound", default "platinum"). */
void pl_sound_play(const char *name);

/* The alert sound, for alerts and refused actions. */
void pl_beep(void);
/* Interface events use the selected sound theme, independently of alerts. */
void pl_sound_event(const char *event);
void pl_sound_preview(const char *path, int level);

/* Double-click time in ms (the Mouse panel's "double-click"; Mac OS's
 * default 533). */
int pl_double_click_ms(void);

#ifdef __cplusplus
}
#endif

#endif
