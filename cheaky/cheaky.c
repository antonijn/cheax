/*
 * Interactive cheax prompt
 * Copyright (C) 2022  Antonie Blom
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

#include <cheax.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

static void
show_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		perror("failed to find license file!");
		return;
	}

	enum { BSIZE = 1024 };
	char buf[BSIZE];
	size_t ln;
	while ((ln = fread(buf, 1, BSIZE, f)) > 0)
		fwrite(buf, ln, 1, stdout);
	fflush(stdout);
	fclose(f);
}
static struct chx_value
show_w(CHEAX *c, struct chx_list *args, void *info)
{
	show_file(CMAKE_INSTALL_PREFIX "/share/licenses/cheaky/WARRANTY");
	return cheax_nil();
}
static struct chx_value
show_c(CHEAX *c, struct chx_list *args, void *info)
{
	show_file(CMAKE_INSTALL_PREFIX "/share/licenses/cheaky/CONDITIONS");
	return cheax_nil();
}

static bool quit = false;

static struct chx_value
read_with_readline(CHEAX *c, int *line, int *pos)
{
	char *prompt = "> ";
	char *fullstr = NULL;
	struct chx_value res = cheax_nil();

	int out_line, out_pos;

	do {
		out_line = *line;
		out_pos = *pos;
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
		const char *to_read = fullstr;
		res = cheax_readstr_at(c, &to_read, "<stdin>", &out_line, &out_pos);

		/* only one expression is read, but we should still
		 * consider the remaining string for our line count
		 * (bit hacky) */
		for (; *to_read != '\0'; ++to_read)
			out_line += (*to_read == '\n');

		prompt = "… ";
	} while (cheax_is_nil(res) && cheax_errno(c) == CHEAX_EEOF);

stop:
	free(fullstr);
	*line = out_line;
	*pos = out_pos;
	return res;
}

static struct chx_value
quit_fun(CHEAX *c, struct chx_list *args, void *info)
{
	return quit = true, cheax_nil();
}

int
main(void)
{
	CHEAX *c = cheax_init();

	cheax_load_feature(c, "all");

	cheax_def_special_form(c, "show-w", show_w, NULL);
	cheax_def_special_form(c, "show-c", show_c, NULL);

	cheax_def_special_form(c, "quit", quit_fun, NULL);

	if (cheax_load_prelude(c)) {
		cheax_perror(c, "cheaky");
		return EXIT_FAILURE;
	}

	cheax_config_bool(c, "allow-redef", true);

	int line = 1, pos = 0;

	fputs("cheaky, Copyright (C) 2022 Antonie Blom\n", stderr);
	fputs("cheaky comes with ABSOLUTELY NO WARRANTY; for details type `(show-w)'.\n", stderr);
	fputs("This is free software, and you are welcome to redistribute it\n", stderr);
	fputs("under certain conditions; type `(show-c)' for details.\n", stderr);

	while (!quit) {
		struct chx_value v = read_with_readline(c, &line, &pos);
		cheax_ft(c, pad);
		if (quit)
			break;

		v = cheax_macroexpand(c, v);
		cheax_ft(c, pad);

		v = cheax_eval(c, v);
		cheax_ft(c, pad);
		if (quit)
			break;

		cheax_print(c, stdout, v);
		printf("\n");
		continue;
pad:
		cheax_perror(c, "cheaky");
		cheax_clear_errno(c);
	}

	cheax_destroy(c);
	return 0;
}
