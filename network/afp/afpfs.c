#define _GNU_SOURCE
#define FUSE_USE_VERSION 31
#include "afpfs.h"

#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * A volume as a FUSE file system. Paths are walked a folder at a time:
 * each folder's AFP directory ID is remembered (and forgotten when it is
 * renamed or removed). FUSE runs us single-threaded: one AFP session
 * answers one request at a time anyway.
 */

static struct afp *afp;

/* ---- folder IDs ------------------------------------------------------------- */

#define CACHE_SLOTS 4096
static struct {
	char *path;
	uint32_t id;
} cache[CACHE_SLOTS];

static unsigned hash(const char *s) {
	unsigned h = 5381;
	while (*s) {
		h = h * 33 + (unsigned char)*s++;
	}
	return h % CACHE_SLOTS;
}

static void remember(const char *path, uint32_t id) {
	unsigned h = hash(path);
	free(cache[h].path);
	cache[h].path = strdup(path);
	cache[h].id = id;
}

/* Forgets a folder and everything under it. */
static void forget(const char *path) {
	const size_t n = strlen(path);
	for (int i = 0; i < CACHE_SLOTS; i++) {
		if (cache[i].path && strncmp(cache[i].path, path, n) == 0 &&
				(cache[i].path[n] == 0 || cache[i].path[n] == '/')) {
			free(cache[i].path);
			cache[i].path = NULL;
		}
	}
}

/* "/a/b/c" -> parent "/a/b" and name "c". */
static void split(const char *path, char *parent, const char **name) {
	const char *slash = strrchr(path, '/');
	size_t n = (size_t)(slash - path);
	if (n == 0) {
		strcpy(parent, "/");
	} else {
		memcpy(parent, path, n);
		parent[n] = 0;
	}
	*name = slash + 1;
}

/* The directory ID of the folder at `path`. */
static int folder_id(const char *path, uint32_t *id) {
	if (strcmp(path, "/") == 0) {
		*id = AFP_ROOT_DIR;
		return 0;
	}
	unsigned h = hash(path);
	if (cache[h].path && strcmp(cache[h].path, path) == 0) {
		*id = cache[h].id;
		return 0;
	}
	char parent[PATH_MAX];
	const char *name;
	split(path, parent, &name);
	uint32_t pid;
	int r = folder_id(parent, &pid);
	if (r < 0) {
		return r;
	}
	struct afp_entry e;
	if ((r = afp_stat(afp, pid, name, &e)) < 0) {
		return r;
	}
	if (!e.is_dir) {
		return -ENOTDIR;
	}
	remember(path, e.id);
	*id = e.id;
	return 0;
}

/* The folder an item is in, and its name there. */
static int locate(const char *path, uint32_t *dir, const char **name) {
	char parent[PATH_MAX];
	split(path, parent, name);
	return folder_id(parent, dir);
}

/* ---- operations ------------------------------------------------------------- */

static void fill_stat(const struct afp_entry *e, struct stat *st) {
	memset(st, 0, sizeof(*st));
	const struct fuse_context *ctx = fuse_get_context();
	st->st_uid = ctx->uid;
	st->st_gid = ctx->gid;
	st->st_ino = e->id;
	if (e->is_dir) {
		st->st_mode = S_IFDIR | (e->writable ? 0755 : 0555);
		st->st_nlink = 2;
	} else {
		st->st_mode = S_IFREG | (e->writable ? 0644 : 0444);
		st->st_nlink = 1;
		st->st_size = e->size;
		st->st_blocks = (e->size + 511) / 512;
	}
	st->st_mtime = e->modified;
	st->st_ctime = e->modified;
	st->st_atime = e->modified;
	st->st_blksize = 4096;
}

static int fs_getattr(const char *path, struct stat *st, struct fuse_file_info *fi) {
	struct afp_entry e;
	int r;
	if (strcmp(path, "/") == 0) {
		r = afp_stat(afp, AFP_ROOT_DIR, "", &e);
	} else {
		uint32_t dir;
		const char *name;
		if ((r = locate(path, &dir, &name)) < 0) {
			return r;
		}
		r = afp_stat(afp, dir, name, &e);
		if (r == 0 && e.is_dir) {
			remember(path, e.id);
		}
	}
	if (r == 0) {
		fill_stat(&e, st);
	}
	return r;
}

static int fs_readdir(const char *path, void *buf, fuse_fill_dir_t fill, off_t offset,
		struct fuse_file_info *fi, enum fuse_readdir_flags flags) {
	uint32_t dir;
	int r = folder_id(path, &dir);
	if (r < 0) {
		return r;
	}
	fill(buf, ".", NULL, 0, 0);
	fill(buf, "..", NULL, 0, 0);
	struct afp_entry entries[32];
	for (int start = 1;;) {
		int n;
		if ((r = afp_list(afp, dir, start, 32, entries, &n)) < 0) {
			return r;
		}
		if (n == 0) {
			return 0;
		}
		for (int i = 0; i < n; i++) {
			const struct afp_entry *e = &entries[i];
			if (e->invisible || !e->name[0]) {
				continue; /* as the Finder hides them */
			}
			if (e->is_dir) {
				char child[PATH_MAX];
				snprintf(child, sizeof(child), "%s/%s", strcmp(path, "/") ? path : "", e->name);
				remember(child, e->id);
			}
			struct stat st;
			fill_stat(e, &st);
			fill(buf, e->name, &st, 0, 0);
		}
		start += n;
	}
}

static int fs_open(const char *path, struct fuse_file_info *fi) {
	uint32_t dir;
	const char *name;
	int r = locate(path, &dir, &name);
	if (r < 0) {
		return r;
	}
	uint16_t fork;
	const bool write = (fi->flags & O_ACCMODE) != O_RDONLY;
	if ((r = afp_open_fork(afp, dir, name, write, &fork)) < 0) {
		return r;
	}
	if (write && (fi->flags & O_TRUNC)) {
		afp_set_fork_size(afp, fork, 0);
	}
	fi->fh = fork;
	fi->direct_io = 0;
	return 0;
}

static int fs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
	uint32_t dir;
	const char *name;
	int r = locate(path, &dir, &name);
	if (r < 0 || ((r = afp_create_file(afp, dir, name)) < 0 && r != -EEXIST)) {
		return r;
	}
	if (r == -EEXIST && (fi->flags & O_EXCL)) {
		return -EEXIST;
	}
	fi->flags = (fi->flags & ~O_ACCMODE) | O_RDWR;
	return fs_open(path, fi);
}

static int fs_read(const char *path, char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi) {
	if (offset >= 0xFFFFFFFFLL) {
		return 0;
	}
	return (int)afp_read(afp, (uint16_t)fi->fh, (uint32_t)offset, buf, size);
}

static int fs_write(const char *path, const char *buf, size_t size, off_t offset,
		struct fuse_file_info *fi) {
	if (offset + (off_t)size > 0xFFFFFFFFLL) {
		return -EFBIG; /* AFP 2's files stop at 4 GB */
	}
	return (int)afp_write(afp, (uint16_t)fi->fh, (uint32_t)offset, buf, size);
}

static int fs_release(const char *path, struct fuse_file_info *fi) {
	return afp_close_fork(afp, (uint16_t)fi->fh);
}

/* Flushing is a hint: Mac OS 9's server refuses FPFlushFork (writes
 * reach its disk anyway), so its answer doesn't matter. */
static int fs_flush(const char *path, struct fuse_file_info *fi) {
	afp_flush_fork(afp, (uint16_t)fi->fh);
	return 0;
}

static int fs_fsync(const char *path, int datasync, struct fuse_file_info *fi) {
	afp_flush_fork(afp, (uint16_t)fi->fh);
	return 0;
}

static int fs_truncate(const char *path, off_t size, struct fuse_file_info *fi) {
	if (size > 0xFFFFFFFFLL) {
		return -EFBIG;
	}
	if (fi) {
		return afp_set_fork_size(afp, (uint16_t)fi->fh, (uint32_t)size);
	}
	uint32_t dir;
	const char *name;
	uint16_t fork;
	int r = locate(path, &dir, &name);
	if (r < 0 || (r = afp_open_fork(afp, dir, name, true, &fork)) < 0) {
		return r;
	}
	r = afp_set_fork_size(afp, fork, (uint32_t)size);
	afp_close_fork(afp, fork);
	return r;
}

static int fs_mkdir(const char *path, mode_t mode) {
	uint32_t dir, id;
	const char *name;
	int r = locate(path, &dir, &name);
	if (r < 0 || (r = afp_create_dir(afp, dir, name, &id)) < 0) {
		return r;
	}
	remember(path, id);
	return 0;
}

static int fs_unlink(const char *path) {
	uint32_t dir;
	const char *name;
	int r = locate(path, &dir, &name);
	return r < 0 ? r : afp_delete(afp, dir, name);
}

static int fs_rmdir(const char *path) {
	uint32_t dir;
	const char *name;
	int r = locate(path, &dir, &name);
	if (r < 0 || (r = afp_delete(afp, dir, name)) < 0) {
		return r;
	}
	forget(path);
	return 0;
}

static int fs_rename(const char *from, const char *to, unsigned int flags) {
	if (flags & RENAME_EXCHANGE) {
		return -EINVAL;
	}
	uint32_t from_dir, to_dir;
	const char *from_name, *to_name;
	int r = locate(from, &from_dir, &from_name);
	if (r < 0 || (r = locate(to, &to_dir, &to_name)) < 0) {
		return r;
	}
	/* AFP won't move onto an existing item; Unix replaces it. */
	struct afp_entry e;
	if (afp_stat(afp, to_dir, to_name, &e) == 0) {
		if (flags & RENAME_NOREPLACE) {
			return -EEXIST;
		}
		if ((r = afp_delete(afp, to_dir, to_name)) < 0) {
			return r;
		}
		forget(to);
	}
	if ((r = afp_move(afp, from_dir, from_name, to_dir, to_name)) < 0) {
		return r;
	}
	forget(from);
	return 0;
}

static int fs_utimens(const char *path, const struct timespec tv[2], struct fuse_file_info *fi) {
	if (strcmp(path, "/") == 0 || tv[1].tv_nsec == UTIME_OMIT) {
		return 0;
	}
	uint32_t dir;
	const char *name;
	int r = locate(path, &dir, &name);
	if (r < 0) {
		return r;
	}
	const time_t t = tv[1].tv_nsec == UTIME_NOW ? time(NULL) : tv[1].tv_sec;
	return afp_set_modified(afp, dir, name, false, t);
}

/* The server decides who may do what; owners and modes aren't ours to
 * change, but copying tools expect these to work. */
static int fs_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {
	return 0;
}

static int fs_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {
	return 0;
}

static int fs_statfs(const char *path, struct statvfs *st) {
	uint64_t free_bytes = 0, total = 0;
	int r = afp_volume_space(afp, &free_bytes, &total);
	memset(st, 0, sizeof(*st));
	st->f_bsize = st->f_frsize = 4096;
	st->f_blocks = total / 4096;
	st->f_bfree = st->f_bavail = free_bytes / 4096;
	st->f_namemax = AFP_NAME_MAX;
	return r;
}

static void *fs_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {
	/* After any fork into the background: keep the session alive. */
	dsi_start_tickles(&afp->dsi);
	cfg->use_ino = 0;
	cfg->nullpath_ok = 0;
	return NULL;
}

static void fs_destroy(void *data) {
	afp_disconnect(afp);
}

static const struct fuse_operations ops = {
	.getattr = fs_getattr,
	.readdir = fs_readdir,
	.open = fs_open,
	.create = fs_create,
	.read = fs_read,
	.write = fs_write,
	.release = fs_release,
	.flush = fs_flush,
	.fsync = fs_fsync,
	.truncate = fs_truncate,
	.mkdir = fs_mkdir,
	.unlink = fs_unlink,
	.rmdir = fs_rmdir,
	.rename = fs_rename,
	.utimens = fs_utimens,
	.chmod = fs_chmod,
	.chown = fs_chown,
	.statfs = fs_statfs,
	.init = fs_init,
	.destroy = fs_destroy,
};

int afpfs_run(struct afp *session, const char *mountpoint, const char *source, bool foreground) {
	afp = session;
	/* fsname is how mount lists it: afp://server/volume (a comma would
	 * end the option). */
	char name[256], opts[300];
	snprintf(name, sizeof(name), "%s", source);
	for (char *p = name; (p = strchr(p, ',')) != NULL;) {
		*p = '_';
	}
	snprintf(opts, sizeof(opts), "fsname=%s,subtype=afp", name);
	char *args[8];
	int argc = 0;
	args[argc++] = "platinum-afp";
	args[argc++] = "-s";
	args[argc++] = "-o";
	args[argc++] = opts;
	if (foreground) {
		args[argc++] = "-f";
	}
	args[argc++] = (char *)mountpoint;
	args[argc] = NULL;
	return fuse_main(argc, args, &ops, NULL);
}
