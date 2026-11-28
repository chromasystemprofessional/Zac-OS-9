/* libFuzzer's entry point: feed arbitrary bytes to the same parsers the AFP
   client uses for a server's status, item parameters and enumerate reply.
   Built with -Dfuzz=true and Clang; see docs/security.md. */
#include "afp_parse.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
	struct afp_server_info info;
	afp_parse_server_info(data, size, &info);

	struct afp_entry entries[8];
	afp_parse_list_reply(data, size, 8, entries);

	if (size >= 3) {
		const bool is_dir = data[0] & 1;
		const uint16_t bitmap = (uint16_t)(data[1] << 8 | data[2]);
		afp_parse_params(data + 3, size - 3, is_dir, bitmap, &entries[0]);
	}
	return 0;
}
