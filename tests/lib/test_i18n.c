#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "i18n.h"
#include "text.h"

int main(int argc, char **argv) {
	if (argc > 1 && strcmp(argv[1], "strict") == 0) {
		/* What the pseudo-locale run turns on: the warning names the string
		 * a longer translation pushed out of its box. */
		setenv("ZACOS9_TEXT_STRICT", "1", 1);
		struct plat_text *t = text_render_font(
			"Translations are longer than English", 20, PL_FONT_SYSTEM);
		if (!t) {
			return 1;
		}
		text_destroy(t);
		return 0;
	}
	zacos9_i18n_init();
	printf("%s\n%s\n", zacos9_tr("Mouse"), zacos9_tr("Double-Click Speed"));
	return 0;
}
