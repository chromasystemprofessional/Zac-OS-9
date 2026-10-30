#include "theme.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
	char dir[] = "/tmp/zacthemeXXXXXX";
	assert(mkdtemp(dir) != NULL);
	char conf[4096];
	snprintf(conf, sizeof(conf), "%s/theme.conf", dir);
	FILE *f = fopen(conf, "w");
	assert(f);
	fputs("name=Test Theme\n", f);
	fclose(f);

	ZacTheme t;
	assert(zac_theme_load(&t, dir) == 0);
	assert(strcmp(t.name, "Test Theme") == 0);
	assert(t.directory && strcmp(t.directory, dir) == 0);
	zac_theme_free(&t);
	assert(t.name == NULL && t.directory == NULL);

	/* Missing theme.conf: still loads with a default name. */
	char dir2[] = "/tmp/zacthemeXXXXXX";
	assert(mkdtemp(dir2) != NULL);
	ZacTheme t2;
	assert(zac_theme_load(&t2, dir2) == 0);
	assert(t2.name != NULL);
	zac_theme_free(&t2);

	rmdir(dir);
	rmdir(dir2);
	printf("theme: ok\n");
	return 0;
}