#ifndef ZACOS9_IDLE_POLICY_H
#define ZACOS9_IDLE_POLICY_H

#include <stdbool.h>

/*
 * When screens and the system go to sleep (idle.c applies it). Every
 * screen is idle for as long as there has been no input; each then shows
 * the screen saver, and later goes dark - unless something keeps that
 * screen awake (a window filling it, or a visible app asking to stay
 * awake), so one screen can play a film while the other sleeps. The system
 * sleeps only when no screen is being kept awake. Pure: no wlroots.
 */

enum idle_state {
	IDLE_AWAKE,
	IDLE_SAVER,  /* the screen saver's turn */
	IDLE_ASLEEP, /* the display is off */
};

/* Milliseconds of no input before each stage; 0 is never. */
struct idle_timeouts {
	long saver_ms, display_ms, system_ms;
};

/* A screen's state after idle_ms without input; `kept` if something
 * keeps it awake. The screen saver comes first, then display sleep. */
enum idle_state idle_output_state(const struct idle_timeouts *t, long idle_ms, bool kept);

/* Milliseconds until that screen's state next changes with no input; -1
 * if it never will. */
long idle_output_next(const struct idle_timeouts *t, long idle_ms, bool kept);

/* Whether the system should sleep after idle_ms without input (or since it
 * last woke); `kept` if any screen is being kept awake. */
bool idle_system_due(const struct idle_timeouts *t, long idle_ms, bool kept);

/* Milliseconds until it is due; -1 if never. */
long idle_system_next(const struct idle_timeouts *t, long idle_ms, bool kept);

#endif
