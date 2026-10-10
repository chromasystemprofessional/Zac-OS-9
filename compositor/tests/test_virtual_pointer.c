#include <assert.h>

#include "../src/input.c"

static struct wlr_cursor *attached_cursor;
static struct wlr_input_device *attached_device;
static struct wlr_output *mapped_output;
static unsigned int map_count;

void wlr_cursor_attach_input_device(struct wlr_cursor *cursor,
		struct wlr_input_device *device) {
	attached_cursor = cursor;
	attached_device = device;
}

void wlr_cursor_map_input_to_output(struct wlr_cursor *cursor,
		struct wlr_input_device *device, struct wlr_output *output) {
	assert(cursor == attached_cursor);
	assert(device == attached_device);
	mapped_output = output;
	map_count++;
}

int main(void) {
	struct wlr_cursor cursor = {0};
	struct plat_server server = {.cursor = &cursor};
	struct wlr_virtual_pointer_v1 pointer = {0};
	struct wlr_output output = {0};
	struct wlr_virtual_pointer_v1_new_pointer_event event = {
		.new_pointer = &pointer,
		.suggested_output = &output,
	};
	new_virtual_pointer(&server.new_virtual_pointer, &event);
	assert(attached_cursor == &cursor);
	assert(attached_device == &pointer.pointer.base);
	assert(mapped_output == &output);
	assert(map_count == 1);

	event.suggested_output = NULL;
	new_virtual_pointer(&server.new_virtual_pointer, &event);
	assert(map_count == 1);
	return 0;
}
