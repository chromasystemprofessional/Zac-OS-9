#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <glib.h>

#include "menubar.h"
#include <sys/stat.h>

static uint32_t launched_pid;
uint32_t launch_feedback_begin(void) {
	return 1;
}

void launch_feedback_update(uint32_t cookie, uint32_t pid) {
	assert(cookie == 1 && pid > 1);
	launched_pid = pid;
}

void launch_feedback_cancel(uint32_t cookie) {
	assert(!"Unexpected launch failure");
}

static struct mb_item *item_named(struct mb_menu *menu, const char *label) {
	for (int i = 0; i < menu->n; i++) {
		if (menu->items[i].label && strcmp(menu->items[i].label, label) == 0) {
			return &menu->items[i];
		}
	}
	return NULL;
}

static int contains_item(struct mb_menu *menu, const char *label) {
	for (int i = 0; i < menu->n; i++) {
		if (menu->items[i].label && strcmp(menu->items[i].label, label) == 0)
			return 1;
		if (menu->items[i].submenu && contains_item(menu->items[i].submenu, label))
			return 1;
	}
	return 0;
}

/* The Zac menu's recent items live in recent.c, not tested here. */
void recent_fill_menu(struct mb_menu *menu) {
	(void)menu;
}

int main(int argc, char **argv) {
	if (argc == 3 && strcmp(argv[1], "--child") == 0) {
		FILE *file = fopen(argv[2], "w");
		assert(file);
		fprintf(file, "%u %u\n", (unsigned)getpid(), (unsigned)getsid(0));
		fclose(file);
		return 0;
	}
	char config[] = "/tmp/zacos9-menu-items-XXXXXX";
	assert(mkdtemp(config));
	assert(setenv("XDG_CONFIG_HOME", config, 1) == 0);
	char tasks[512], script[640], ignored[640];
	snprintf(tasks, sizeof(tasks), "%s/zacos9/ZacOS 9 Menu Items/Automated Tasks", config);
	assert(g_mkdir_with_parents(tasks, 0700) == 0);
	snprintf(script, sizeof(script), "%s/hello task.sh", tasks);
	snprintf(ignored, sizeof(ignored), "%s/not executable", tasks);
	assert(g_file_set_contents(script, "#!/bin/sh\nexit 0\n", -1, NULL));
	assert(chmod(script, 0700) == 0);
	assert(g_file_set_contents(ignored, "not executable", -1, NULL));
	char items[512], tools[640], nested[768], top[640], linked[640], doc[640], doc_alias[768];
	snprintf(items, sizeof(items), "%s/zacos9/ZacOS 9 Menu Items", config);
	snprintf(tools, sizeof(tools), "%s/Tools", items);
	assert(g_mkdir_with_parents(tools, 0700) == 0);
	char empty[768];
	snprintf(empty, sizeof(empty), "%s/Empty", items);
	assert(mkdir(empty, 0700) == 0);
	snprintf(nested, sizeof(nested), "%s/Nested.desktop", tools);
	snprintf(top, sizeof(top), "%s/Top.desktop", items);
	assert(g_file_set_contents(nested, "[Desktop Entry]\nName=Nested app\nExec=/bin/true\n",
		-1, NULL));
	assert(g_file_set_contents(top, "[Desktop Entry]\nName=Top app\nExec=/bin/false\n",
		-1, NULL));
	snprintf(doc, sizeof(doc), "%s/Document.txt", config);
	snprintf(doc_alias, sizeof(doc_alias), "%s/Document alias", items);
	assert(g_file_set_contents(doc, "text", -1, NULL));
	assert(symlink(doc, doc_alias) == 0);
	snprintf(linked, sizeof(linked), "%s/Linked folder", items);
	assert(symlink(tools, linked) == 0);
	char *levels[5];
	levels[0] = g_build_filename(items, "Level 1", NULL);
	assert(mkdir(levels[0], 0700) == 0);
	for (int i = 1; i < 5; i++) {
		char name[16];
		snprintf(name, sizeof(name), "Level %d", i + 1);
		levels[i] = g_build_filename(levels[i - 1], name, NULL);
		assert(mkdir(levels[i], 0700) == 0);
	}
	char *at_limit = g_build_filename(levels[3], "AtLimit.desktop", NULL);
	assert(g_file_set_contents(at_limit,
		"[Desktop Entry]\nName=At limit app\nExec=/bin/true\n", -1, NULL));
	char *too_deep = g_build_filename(levels[4], "TooDeep.desktop", NULL);
	assert(g_file_set_contents(too_deep,
		"[Desktop Entry]\nName=Too deep app\nExec=/bin/true\n", -1, NULL));

	struct mb_menu menu = { 0 };
	launch_fill_logo_menu(&menu, NULL, 0);
	struct mb_item *automated = item_named(&menu, "Automated Tasks");
	assert(automated && automated->enabled && automated->submenu);
	assert(automated->submenu->n == 1);
	assert(strcmp(automated->submenu->items[0].label, "hello task.sh") == 0);
	char *quoted = g_shell_quote(script);
	char expected[1400];
	snprintf(expected, sizeof(expected), "exec %s", quoted);
	assert(strcmp(automated->submenu->items[0].arg, expected) == 0);
	g_free(quoted);
	struct mb_item *tools_item = item_named(&menu, "Tools");
	assert(tools_item && tools_item->submenu && tools_item->submenu->n == 1);
	assert(strcmp(tools_item->submenu->items[0].label, "Nested app") == 0);
	assert(tools_item->submenu->items[0].action == ACT_LAUNCH &&
		strcmp(tools_item->submenu->items[0].arg, "/bin/true") == 0);
	struct mb_item *linked_item = item_named(&menu, "Linked folder");
	assert(item_named(&menu, "Top app") && linked_item && !linked_item->submenu &&
		linked_item->action == ACT_FINDER);
	char expected_alias[800];
	snprintf(expected_alias, sizeof(expected_alias), "open-path %s", linked);
	assert(strcmp(linked_item->arg, expected_alias) == 0);
	struct mb_item *document_alias = item_named(&menu, "Document alias");
	assert(document_alias && document_alias->action == ACT_FINDER);
	snprintf(expected_alias, sizeof(expected_alias), "open-path %s", doc_alias);
	assert(strcmp(document_alias->arg, expected_alias) == 0);
	assert(!item_named(&menu, "Empty"));
	assert(contains_item(&menu, "At limit app"));
	assert(!contains_item(&menu, "Too deep app"));
	char parent[512];
	unlink(script);
	unlink(ignored);
	unlink(nested);
	unlink(top);
	unlink(linked);
	unlink(doc_alias);
	unlink(doc);
	unlink(at_limit);
	unlink(too_deep);
	for (int i = 4; i >= 0; i--) {
		rmdir(levels[i]);
		g_free(levels[i]);
	}
	g_free(at_limit);
	g_free(too_deep);
	rmdir(tools);
	rmdir(empty);
	rmdir(tasks);
	rmdir(items);
	snprintf(parent, sizeof(parent), "%s/zacos9/ZacOS 9 Menu Items", config);
	rmdir(parent);
	snprintf(parent, sizeof(parent), "%s/zacos9", config);
	rmdir(parent);
	rmdir(config);

	char path[] = "/tmp/zacos9-menu-launch-XXXXXX";
	int fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);
	char *exe = g_canonicalize_filename(argv[0], NULL);
	assert(exe);
	char command[4096];
	assert(snprintf(command, sizeof(command), "exec '%s' --child '%s'", exe, path)
		< (int)sizeof(command));
	g_free(exe);
	launch(command);
	unsigned int pid = 0, session = 0;
	for (int tries = 0; tries < 250; tries++) {
		FILE *file = fopen(path, "r");
		assert(file);
		int fields = fscanf(file, "%u %u", &pid, &session);
		fclose(file);
		if (fields == 2) {
			break;
		}
		struct timespec pause = { .tv_nsec = 20000000 };
		nanosleep(&pause, NULL);
	}
	unlink(path);
	assert(pid == launched_pid);
	assert(session == pid); /* Preserve the detached application session. */
	puts("ok: menu launch reports the actual detached application's PID");
	return 0;
}
