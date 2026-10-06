#ifndef ZACOS9_SNAPSHOT_H
#define ZACOS9_SNAPSHOT_H

struct plat_server;
struct plat_output;
struct wlr_buffer;

void snapshot_init(struct plat_server *server);
void snapshot_output_commit(struct plat_output *output, struct wlr_buffer *buffer);
void snapshot_output_finish(struct plat_output *output);

#endif
