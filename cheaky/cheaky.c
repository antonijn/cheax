/*
 * Interactive cheax prompt
 * Copyright (C) 2020  Antonie Blom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "config.h"

#include <cheax.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

#ifdef HAVE_ISATTY
#include <unistd.h>
#endif

static void show_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) {
		perror("failed to find license file!");
		return;
	}

	const size_t bsize = 1024;
	char buf[bsize];
	size_t ln;
	while ((ln = fread(buf, 1, bsize, f)) > 0)
		fwrite(buf, ln, 1, stdout);
	fflush(stdout);
	fclose(f);
}
static struct chx_value *show_w(CHEAX *c, struct chx_list *args)
{
	show_file(CMAKE_INSTALL_PREFIX "/share/licenses/cheaky/WARRANTY");
	return NULL;
}
static struct chx_value *show_c(CHEAX *c, struct chx_list *args)
{
	show_file(CMAKE_INSTALL_PREFIX "/share/licenses/cheaky/CONDITIONS");
	return NULL;
}

static bool quit = false;

static struct chx_value *read_with_readline(CHEAX *c)
{
	char *prompt = "> ";
	char *fullstr = NULL;
	struct chx_value *res = NULL;

	do {
		char *input = readline(prompt);
		if (!input) {
			quit = true;
			free(input);
			goto stop;
		}
		if (input[0] == '\0') {
			free(input);
			goto stop;
		}
		add_history(input);

		int size_fullstr = (fullstr != NULL) ? strlen(fullstr) : 0;
		int size_input = strlen(input);
		fullstr = realloc(fullstr, size_fullstr + size_input + 1 + 1);
		strcpy(fullstr + size_fullstr, input);
		strcpy(fullstr + size_fullstr + size_input, "\n");

		free(input);

		cheax_clear_errno(c);
		res = cheax_readstr(c, fullstr);

		prompt = "… ";
	} while (res == NULL && cheax_errno(c) == CHEAX_EEOF);

stop:
	free(fullstr);
	return res;
}

static struct chx_value *quit_fun(CHEAX *c, struct chx_list *args)
{
	quit = true;
	return NULL;
}

int main(void)
{
	CHEAX *c = cheax_init();

	cheax_load_extra_builtins(c, CHEAX_ALL_BUILTINS);

	cheax_defmacro(c, "show-w", show_w);
	cheax_defmacro(c, "show-c", show_c);

	cheax_defmacro(c, "quit", quit_fun);

	if (cheax_load_prelude(c)) {
		cheax_perror(c, "cheaky");
		return EXIT_FAILURE;
	}

#ifdef HAVE_ISATTY
	if (!isatty(1)) {
		struct chx_value *v;
		while (v = cheax_read(c, stdin)) {
			cheax_print(c, stdout, cheax_eval(c, v));
			printf("\n");
		}
		cheax_destroy(c);
		return 0;
	}
#endif

	fputs("Cheaky, Copyright (C) 2020 Antonie Blom\n", stderr);
	fputs("Cheaky comes with ABSOLUTELY NO WARRANTY; for details type `(show-w)'.\n", stderr);
	fputs("This is free software, and you are welcome to redistribute it\n", stderr);
	fputs("under certain conditions; type `(show-c)' for details.\n", stderr);

	while (!quit) {
		struct chx_value *v = read_with_readline(c);
		struct chx_value *e = cheax_eval(c, v);
		if (cheax_errno(c) != 0) {
			cheax_perror(c, "cheaky");
			cheax_clear_errno(c);
		} else {
			cheax_print(c, stdout, e);
			printf("\n");
		}
	}

	cheax_destroy(c);
	return 0;
}
