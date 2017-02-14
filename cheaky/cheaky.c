/*
 * Interactive cheax prompt
 * Copyright (C) 2016  Antonie Blom
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
static struct chx_value *show_w(CHEAX *c, struct chx_cons *args)
{
	show_file(CMAKE_INSTALL_PREFIX "/share/licenses/cheaky/WARRANTY");
	return NULL;
}
static struct chx_value *show_c(CHEAX *c, struct chx_cons *args)
{
	show_file(CMAKE_INSTALL_PREFIX "/share/licenses/cheaky/CONDITIONS");
	return NULL;
}

static bool quit = false;

static struct chx_value *quit_fun(CHEAX *c, struct chx_cons *args)
{
	quit = true;
	return NULL;
}

int main(void)
{
	CHEAX *c = cheax_init();

	cheax_defmacro(c, "show-w", show_w);
	cheax_defmacro(c, "show-c", show_c);

	cheax_defmacro(c, "quit", quit_fun);

	if (cheax_load_prelude(c)) {
		perror("failed to load prelude");
		return EXIT_FAILURE;
	}

#ifdef HAVE_ISATTY
	if (!isatty(1)) {
		struct chx_value *v;
		while (v = cheax_read(stdin)) {
			cheax_print(stdout, cheax_eval(c, v));
			printf("\n");
		}
		cheax_destroy(c);
		return 0;
	}
#endif

	fputs("Cheaky, Copyright (C) 2017 Antonie Blom\n", stderr);
	fputs("Cheaky comes with ABSOLUTELY NO WARRANTY; for details type '(show-w)'.\n", stderr);
	fputs("This is free software, and you are welcome to redistribute it\n", stderr);
	fputs("under certain conditions; type '(show-c)' for details.\n", stderr);

	while (!quit) {
		char *input = readline("> ");
		if (!input)
			break;
		add_history(input);

		struct chx_value *v = cheax_readstr(input);

		free(input);

		cheax_print(stdout, cheax_eval(c, v));
		printf("\n");
	}

	cheax_destroy(c);
	return 0;
}
