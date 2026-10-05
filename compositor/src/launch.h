#ifndef ZACOS9_LAUNCH_H
#define ZACOS9_LAUNCH_H

#include <stdbool.h>
#include <stdint.h>

struct plat_server;
void launch_init(struct plat_server *server);
bool launch_begin(struct plat_server *server, void *owner, uint32_t cookie,
	uint32_t pid, const char *app_id);
void launch_update(struct plat_server *server, void *owner, uint32_t cookie, uint32_t pid);
void launch_cancel(struct plat_server *server, void *owner, uint32_t cookie);
void launch_cancel_owner(struct plat_server *server, void *owner);
void launch_ready(struct plat_server *server, uint32_t pid, const char *app_id);
bool launch_pending(struct plat_server *server);

#endif
