#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>

#include "settings.h"

static void config_path(char *out, size_t size) {
	const char *config = getenv("XDG_CONFIG_HOME");
	const char *home = getenv("HOME");
	if (config && *config) {
		snprintf(out, size, "%s/zacos9/desktop.conf", config);
	} else {
		snprintf(out, size, "%s/.config/zacos9/desktop.conf", home ? home : "");
	}
}

/* mkdir -p: a new account has no ~/.config yet, and mkdir() makes one level only. */
static void make_dirs(const char *dir) {
	char path[1024];
	snprintf(path, sizeof(path), "%s", dir);
	for (char *p = path + 1; *p; p++) {
		if (*p == '/') {
			*p = '\0';
			mkdir(path, 0755);
			*p = '/';
		}
	}
	mkdir(path, 0755);
}

static char *trim(char *s) {
	while (isspace((unsigned char)*s)) {
		s++;
	}
	char *e = s + strlen(s);
	while (e > s && isspace((unsigned char)e[-1])) {
		*--e = '\0';
	}
	return s;
}

/* The file's lines, cached until its modification time changes. */
#define MAX_LINES 64
static struct {
	struct timespec mtime;
	off_t size;
	bool loaded;
	int n;
	char lines[MAX_LINES][256];
} cache;

static void load(void) {
	char path[1024];
	config_path(path, sizeof(path));
	struct stat st;
	if (stat(path, &st) != 0) {
		cache.loaded = true;
		cache.n = 0;
		cache.mtime = (struct timespec){ 0 };
		return;
	}
	if (cache.loaded && st.st_mtim.tv_sec == cache.mtime.tv_sec &&
			st.st_mtim.tv_nsec == cache.mtime.tv_nsec && st.st_size == cache.size) {
		return;
	}
	cache.loaded = true;
	cache.mtime = st.st_mtim;
	cache.size = st.st_size;
	cache.n = 0;
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	while (cache.n < MAX_LINES && fgets(cache.lines[cache.n], sizeof(cache.lines[0]), f)) {
		cache.lines[cache.n][strcspn(cache.lines[cache.n], "\r\n")] = '\0';
		cache.n++;
	}
	fclose(f);
}

/* Index of `key`'s line, or -1. */
static int find_key(const char *key) {
	for (int i = 0; i < cache.n; i++) {
		const char *eq = strchr(cache.lines[i], '=');
		if (!eq) {
			continue;
		}
		char name[256];
		snprintf(name, sizeof(name), "%.*s", (int)(eq - cache.lines[i]), cache.lines[i]);
		if (strcmp(trim(name), key) == 0) {
			return i;
		}
	}
	return -1;
}

bool pl_setting(const char *key, char *out, size_t size) {
	load();
	int i = find_key(key);
	if (i < 0) {
		return false;
	}
	char value[256];
	snprintf(value, sizeof(value), "%s", strchr(cache.lines[i], '=') + 1);
	snprintf(out, size, "%s", trim(value));
	return out[0] != '\0';
}

bool pl_setting_set(const char *key, const char *value) {
	load();
	int i = find_key(key);
	if (i < 0 && value) {
		if (cache.n == 0) {
			snprintf(cache.lines[cache.n++], sizeof(cache.lines[0]), "[General]");
		}
		if (cache.n >= MAX_LINES) {
			return false;
		}
		i = cache.n++;
	}
	if (i >= 0) {
		if (value) {
			snprintf(cache.lines[i], sizeof(cache.lines[0]), "%s=%s", key, value);
		} else {
			memmove(cache.lines[i], cache.lines[i + 1], (size_t)(cache.n - i - 1) * sizeof(cache.lines[0]));
			cache.n--;
		}
	}
	char path[1024], tmp[1100];
	config_path(path, sizeof(path));
	char *slash = strrchr(path, '/');
	if (slash) {
		*slash = '\0';
		make_dirs(path);
		*slash = '/';
	}
	/* Write a new file and rename it over, so readers never see half. */
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	FILE *f = fopen(tmp, "w");
	if (!f) {
		return false;
	}
	for (int k = 0; k < cache.n; k++) {
		fprintf(f, "%s\n", cache.lines[k]);
	}
	if (fclose(f) != 0 || rename(tmp, path) != 0) {
		return false;
	}
	cache.loaded = false;
	return true;
}

/* ---- accents --------------------------------------------------------------- */

/* Lavender and Ivy are measured (HIG figures 2-24, 4-1 and 2-26); the
 * others keep Lavender's lightness steps with another hue (TODO: compare
 * with the real Mac OS 8 accent colours). */
static const struct {
	const char *id, *name;
	struct pl_accent ramp;
} accents[] = {
	{ "lavender", "Lavender", PL_ACCENT_DEFAULT },
	{ "graphite", "Graphite", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xE6, 0xE6, 0xE6), .body = RGB(0xCC, 0xCC, 0xCC), .dark = RGB(0x99, 0x99, 0x99), .shadow = RGB(0x66, 0x66, 0x66), .deep = RGB(0x44, 0x44, 0x44) } },
	{ "gold", "Gold", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xFF, 0xF2, 0xCC), .body = RGB(0xFF, 0xE6, 0x99), .dark = RGB(0xCC, 0xB3, 0x66), .shadow = RGB(0x99, 0x80, 0x33), .deep = RGB(0x88, 0x66, 0x00) } },
	{ "bronze", "Bronze", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xF4, 0xE3, 0xD7), .body = RGB(0xE8, 0xC7, 0xB0), .dark = RGB(0xB5, 0x94, 0x7D), .shadow = RGB(0x82, 0x61, 0x4A), .deep = RGB(0x69, 0x3E, 0x1F) } },
	{ "ivy", "Ivy", PL_ACCENT_GREEN },
	{ "ocean", "Ocean", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xCF, 0xED, 0xFC), .body = RGB(0x9E, 0xDB, 0xFA), .dark = RGB(0x6B, 0xA8, 0xC7), .shadow = RGB(0x38, 0x75, 0x94), .deep = RGB(0x07, 0x58, 0x81) } },
	{ "rose", "Rose", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xF9, 0xD2, 0xDF), .body = RGB(0xF2, 0xA6, 0xBF), .dark = RGB(0xBF, 0x73, 0x8C), .shadow = RGB(0x8C, 0x40, 0x59), .deep = RGB(0x77, 0x11, 0x33) } },
	{ "plum", "Plum", { .corner = RGB(0xEE, 0xEE, 0xEE), .grip_hi = RGB(0xEE, 0xEE, 0xEE), .light = RGB(0xF0, 0xD6, 0xF5), .body = RGB(0xE0, 0xAD, 0xEB), .dark = RGB(0xAD, 0x7A, 0xB8), .shadow = RGB(0x7A, 0x47, 0x85), .deep = RGB(0x5F, 0x1B, 0x6D) } },
};
#define N_ACCENTS ((int)(sizeof(accents) / sizeof(accents[0])))

int pl_accent_count(void) {
	return N_ACCENTS;
}

const char *pl_accent_id(int i) {
	return i >= 0 && i < N_ACCENTS ? accents[i].id : NULL;
}

const char *pl_accent_name(int i) {
	return i >= 0 && i < N_ACCENTS ? accents[i].name : NULL;
}

struct pl_accent pl_accent_at(int i) {
	return accents[i >= 0 && i < N_ACCENTS ? i : 0].ramp;
}

int pl_accent_find(const char *id) {
	for (int i = 0; id && i < N_ACCENTS; i++) {
		if (strcmp(accents[i].id, id) == 0) {
			return i;
		}
	}
	return 0;
}

struct pl_accent pl_accent_current(void) {
	char id[64];
	return pl_accent_at(pl_setting("accent", id, sizeof(id)) ? pl_accent_find(id) : 0);
}

uint32_t pl_highlight_current(void) {
	char hex[16];
	if (pl_setting("highlight", hex, sizeof(hex)) && strlen(hex) == 6) {
		char *end;
		unsigned long v = strtoul(hex, &end, 16);
		if (*end == '\0') {
			return 0xFF000000u | (uint32_t)v;
		}
	}
	return pl_accent_current().light;
}

static bool is_dir(const char *path) {
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

const char *pl_data_dir(void) {
	static char dir[1024];
	if (dir[0]) {
		return dir;
	}
	const char *env = getenv("ZACOS9_DATA");
	if (env && *env) {
		snprintf(dir, sizeof(dir), "%s", env);
	} else if (is_dir(ZACOS9_DATA_DIR "/sounds")) {
		snprintf(dir, sizeof(dir), "%s", ZACOS9_DATA_DIR);
	} else {
		snprintf(dir, sizeof(dir), "%s/assets", ZACOS9_SOURCE_DIR);
	}
	return dir;
}

static int interface_lock(void) {
	char path[1200];
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *cache_dir = getenv("XDG_CACHE_HOME");
	const char *home = getenv("HOME");
	if (runtime && *runtime) {
		snprintf(path, sizeof(path), "%s/zacos9-interface-sound.lock", runtime);
	} else if (cache_dir && *cache_dir) {
		snprintf(path, sizeof(path), "%s/zacos9/sounds/player.lock", cache_dir);
	} else {
		snprintf(path, sizeof(path), "%s/.cache/zacos9/sounds/player.lock", home ? home : "");
	}
	int fd = open(path, O_CREAT | O_RDWR | O_NOFOLLOW, 0600);
	if (fd < 0) {
		perror("Could not open interface sound lock");
	}
	if (fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) != 0) {
		if (errno != EWOULDBLOCK && errno != EAGAIN) {
			perror("Could not lock interface sound playback");
		}
		close(fd);
		return -1;
	}
	return fd;
}

static void sound_backend(const char *path, int level) {
	char pw_volume[16], pa_volume[24];
	snprintf(pw_volume, sizeof(pw_volume), "%.3f", level / 7.0);
	snprintf(pa_volume, sizeof(pa_volume), "--volume=%d", 65536 * level / 7);
	int null = open("/dev/null", O_WRONLY);
	if (null >= 0) {
		dup2(null, STDOUT_FILENO);
		close(null);
	}
	execlp("pw-play", "pw-play", "--volume", pw_volume, path, (char *)NULL);
	execlp("paplay", "paplay", pa_volume, path, (char *)NULL);
	execlp("aplay", "aplay", "-q", path, (char *)NULL);
	perror("No sound playback command could be started");
	_exit(127);
}

static int loop_stop_fd = -1;

static void play_sound(const char *path, int level, bool interface_sound, int lock_retry_ms) {
	if (access(path, R_OK) != 0) {
		fprintf(stderr, "Sound file cannot be read: %s\n", path);
		return;
	}
	level = level < 0 ? 0 : level > 7 ? 7 : level;
	if (level == 0) {
		return;
	}
	/* Detach fully (double fork) so the caller never waits or reaps. */
	pid_t pid = fork();
	if (pid < 0) {
		perror("Could not start sound playback");
		return;
	}
	if (pid == 0) {
		/* Detached one-shots must not extend the caller's loop lifetime, even
		 * while an end event waits for its interface lock before exec. */
		if (loop_stop_fd >= 0) {
			close(loop_stop_fd);
		}
		if (fork() == 0) {
			setsid();
			if (interface_sound) {
				int lock = interface_lock();
				/* A release event can arrive before the stopped loop has killed
				 * its backend. Retry only in this detached playback process. */
				while (lock < 0 && lock_retry_ms > 0) {
					struct timespec delay = { .tv_nsec = 10000000 };
					nanosleep(&delay, NULL);
					lock_retry_ms -= 10;
					lock = interface_lock();
				}
				if (lock < 0) {
					_exit(0); /* Keep a previous interface event from overlapping. */
				}
			}
			sound_backend(path, level);
		}
		_exit(0);
	}
	waitpid(pid, NULL, 0);
}

void pl_sound_preview(const char *path, int level) {
	play_sound(path, level, false, 0);
}

void pl_sound_play(const char *name) {
	char chosen[64] = "platinum";
	if (!name) {
		pl_setting("alert-sound", chosen, sizeof(chosen));
		name = chosen;
	}
	if (strcmp(name, "none") == 0 || strchr(name, '/')) {
		return;
	}
	char path[1200], v[8];
	snprintf(path, sizeof(path), "%s/sounds/%s.wav", pl_data_dir(), name);
	pl_sound_preview(path, pl_setting("alert-volume", v, sizeof(v)) ? atoi(v) : 7);
}

static bool event_sound(const char *event, char *path, size_t size, int *level) {
	static const char *const events[] = { "button-click", "checkbox-toggle", "menu-open",
		"menu-command", "window-open", "window-close", "trash-move", "trash-empty",
		"window-collapse", "window-expand", "window-drag", "window-drag-end" };
	int index = -1;
	for (size_t i = 0; event && i < sizeof(events) / sizeof(events[0]); i++) {
		if (strcmp(event, events[i]) == 0) {
			index = i;
			break;
		}
	}
	if (index < 0) {
		fprintf(stderr, "Unknown interface sound event: %s\n", event ? event : "(null)");
		return false;
	}
	char theme[128], value[128], key[64];
	if (!pl_setting("sound-theme", theme, sizeof(theme)) || strcmp(theme, "none") == 0) {
		return false;
	}
	char volume[8];
	*level = pl_setting("interface-volume", volume, sizeof(volume)) ? atoi(volume) : 5;
	if (*level <= 0) {
		return false;
	}
	if (*level > 7) {
		*level = 7;
	}
	if (strcmp(theme, "original") == 0) {
		static const char *const sounds[] = { "woodblock", "pluck", "droplet", "woodblock",
			"chirp", "pluck", "droplet", "woodblock", "pluck", "chirp", "droplet", "woodblock" };
		snprintf(path, size, "%s/sounds/%s.wav", pl_data_dir(), sounds[index]);
	} else {
		snprintf(key, sizeof(key), "sound.%s", event);
		if (!pl_setting(key, value, sizeof(value))) {
			return false; /* A theme need not supply every event. */
		}
		if (strchr(value, '/') || strchr(value, '\\') || strstr(value, "..")) {
			fprintf(stderr, "Invalid cached interface sound filename\n");
			return false;
		}
		const char *cache_dir = getenv("XDG_CACHE_HOME");
		if (cache_dir && *cache_dir) {
			snprintf(path, size, "%s/zacos9/sounds/%s", cache_dir, value);
		} else {
			const char *home = getenv("HOME");
			snprintf(path, size, "%s/.cache/zacos9/sounds/%s", home ? home : "", value);
		}
	}
	return access(path, R_OK) == 0;
}

void pl_sound_event(const char *event) {
	char path[1200];
	int level;
	if (!event_sound(event, path, sizeof(path), &level)) {
		return;
	}
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	static struct timespec last;
	bool drag_end = strcmp(event, "window-drag-end") == 0;
	if (!drag_end && (now.tv_sec - last.tv_sec) * 1000000000LL + now.tv_nsec - last.tv_nsec < 60000000LL) {
		return;
	}
	last = now;
	play_sound(path, level, true, drag_end ? 300 : 0);
}

void pl_sound_loop_stop(void) {
	if (loop_stop_fd >= 0) {
		close(loop_stop_fd); /* EOF also stops playback when the caller exits. */
		loop_stop_fd = -1;
	}
}

/* Inspect without reaping: the owned PID/group cannot be reused until cleanup. */
static bool backend_finished(pid_t pid, bool *failed) {
	siginfo_t info = {0};
	if (waitid(P_PID, pid, &info, WEXITED | WNOHANG | WNOWAIT) != 0) {
		perror("Could not inspect sound playback process");
		*failed = true;
		return true;
	}
	*failed = info.si_pid && (info.si_code != CLD_EXITED || info.si_status != 0);
	return info.si_pid != 0;
}

static void stop_backend(pid_t pid) {
	/* These disposable players have no state to save. Kill immediately so
	 * release/mute cannot leave an audible tail or a backend holding the lock. */
	kill(-pid, SIGKILL);
	while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
}

static void loop_supervisor(int fd, const char *event) {
	/* Do not inherit the compositor's SIGCHLD reaper or unrelated pipe writers. */
	struct sigaction action = { .sa_handler = SIG_DFL };
	sigemptyset(&action.sa_mask);
	sigaction(SIGCHLD, &action, NULL);
	sigset_t empty;
	sigemptyset(&empty);
	sigprocmask(SIG_SETMASK, &empty, NULL);
	setsid();
	if (fd != 3) {
		dup2(fd, 3);
		close(fd);
	}
	/* ZacOS is Linux; close_range avoids keeping GUI connections alive. */
	if (syscall(SYS_close_range, 4u, ~0u, 0u) != 0) {
		long max = sysconf(_SC_OPEN_MAX);
		for (int i = 4; i < max; i++) {
			close(i);
		}
	}
	fcntl(3, F_SETFD, FD_CLOEXEC);
	struct pollfd control = { .fd = 3, .events = POLLIN };
	pid_t backend = 0;
	int lock = -1, playing_level = 0;
	char playing_path[1200] = "";
	for (;;) {
		int ready = poll(&control, 1, 40);
		if (ready > 0 || (ready < 0 && errno != EINTR)) {
			break;
		}
		char path[1200];
		int level = 0;
		bool enabled = event_sound(event, path, sizeof(path), &level);
		bool failed = false;
		bool finished = backend && backend_finished(backend, &failed);
		if (failed) {
			fprintf(stderr, "Window drag sound playback failed; stopping the loop\n");
			break;
		}
		if (backend && (!enabled || level != playing_level ||
				strcmp(path, playing_path) != 0 || finished)) {
			stop_backend(backend);
			backend = 0;
			close(lock);
			lock = -1;
			/* Yield the interface lock between samples, even for very short WAVs. */
			continue;
		}
		if (!backend && enabled && (lock = interface_lock()) >= 0) {
			backend = fork();
			if (backend == 0) {
				close(3);
				setpgid(0, 0);
				sound_backend(path, level);
			}
			if (backend < 0) {
				perror("Could not start window drag sound backend");
				backend = 0;
				close(lock);
				lock = -1;
				break;
			} else {
				setpgid(backend, backend);
				playing_level = level;
				snprintf(playing_path, sizeof(playing_path), "%s", path);
			}
		}
	}
	if (backend) {
		stop_backend(backend);
	}
	if (lock >= 0) {
		close(lock);
	}
	_exit(0);
}

static void *reap_loop(void *arg) {
	int fd = (int)(intptr_t)arg;
	pid_t pid;
	ssize_t count;
	do {
		count = read(fd, &pid, sizeof(pid));
	} while (count < 0 && errno == EINTR);
	close(fd);
	if (count == sizeof(pid)) {
		while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {}
	}
	return NULL;
}

void pl_sound_loop_start(const char *event) {
	pl_sound_loop_stop();
	char path[1200];
	int level;
	if (!event_sound(event, path, sizeof(path), &level)) {
		return;
	}
	int control[2];
	if (pipe2(control, O_CLOEXEC) != 0) {
		perror("Could not create sound loop control pipe");
		return;
	}
	int reaper[2];
	if (pipe2(reaper, O_CLOEXEC) != 0) {
		perror("Could not create sound loop reaper pipe");
		close(control[0]);
		close(control[1]);
		return;
	}
	/* Reserve the reaper before creating a child, so thread exhaustion cannot
	 * leave a zombie in callers without their own SIGCHLD handler. */
	pthread_t thread;
	int error = pthread_create(&thread, NULL, reap_loop, (void *)(intptr_t)reaper[0]);
	if (error != 0) {
		fprintf(stderr, "Could not create sound loop reaper: %s\n", strerror(error));
		close(reaper[0]);
		close(reaper[1]);
		close(control[0]);
		close(control[1]);
		return;
	}
	pthread_detach(thread);
	pid_t pid = fork();
	if (pid == 0) {
		close(control[1]);
		loop_supervisor(control[0], event);
	}
	close(control[0]);
	if (pid < 0) {
		perror("Could not start window drag sound loop");
		close(reaper[1]);
		close(control[1]);
		return;
	}
	loop_stop_fd = control[1];
	ssize_t count;
	do {
		count = write(reaper[1], &pid, sizeof(pid));
	} while (count < 0 && errno == EINTR);
	close(reaper[1]);
}

void pl_beep(void) {
	pl_sound_play(NULL);
}

int pl_double_click_ms(void) {
	char v[16];
	const int ms = pl_setting("double-click", v, sizeof(v)) ? atoi(v) : 0;
	return ms >= 150 && ms <= 2000 ? ms : 533;
}
