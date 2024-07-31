/*
 * Interactive cheax prompt
 * Copyright (C) 2024  Antonie Blom
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
#include <string.h>
#ifdef CHEAKY_USE_READLINE
#  include <readline/readline.h>
#  include <readline/history.h>
#endif

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
	return CHEAX_NIL;
}
static struct chx_value
show_c(CHEAX *c, struct chx_list *args, void *info)
{
	show_file(CMAKE_INSTALL_PREFIX "/share/licenses/cheaky/CONDITIONS");
	return CHEAX_NIL;
}

static bool clear = false;

#ifndef CHEAKY_USE_READLINE

static int
expand_buf(char **buf, size_t new_len, size_t *cap)
{
	size_t old_cap = *cap;

	if (new_len <= old_cap)
		return 0;

	size_t new_cap = old_cap;
	while (new_len > old_cap) {
		new_cap *= 2;
		if (new_cap <= old_cap) {
			/* overflow */
			goto fail;
		}
	}

	char *new_buf = realloc(*buf, new_cap);
	if (new_buf == NULL)
		goto fail;

	*buf = new_buf;
	*cap = new_cap;
	return 0;

fail:
	free(*buf);
	*buf = NULL;
	*cap = 0;
	return -1;
}

static char *
cheaky_readline(char *prompt)
{
	fputs(prompt, stdout);

	size_t len = 1, cap = 128;

	char *res = malloc(cap);
	if (res == NULL)
		return res;

	res[0] = '\0';

	int c;
	while ((c = getchar()) != '\n') {
		if (c == EOF) {
			putchar('\n');
			free(res);
			return NULL;
		}

		if (expand_buf(&res, ++len, &cap) < 0)
			break;

		res[len - 2] = c;
		res[len - 1] = '\0';
	}

	return res;
}

#endif

static int
read_with_readline(CHEAX *c, int *line, int *pos, struct chx_value *out)
{
	char *prompt = "> ";
	char *fullstr = NULL;
	int res = 0;
	int out_line, out_pos;

	*out = CHEAX_NIL;

	do {
		out_line = *line;
		out_pos = *pos;

#ifdef CHEAKY_USE_READLINE
		char *input = readline(prompt);
#else
		char *input = cheaky_readline(prompt);
#endif
		/* end of file */
		if (input == NULL) {
			res = -1;
			break;
		}

		/* empty line */
		if (input[0] == '\0') {
			++out_line;
			free(input);
			break;
		}

#ifdef CHEAKY_USE_READLINE
		add_history(input);
#endif

		int size_fullstr = (fullstr != NULL) ? strlen(fullstr) : 0;
		int size_input = strlen(input);
		fullstr = realloc(fullstr, size_fullstr + size_input + 1 + 1);
		strcpy(fullstr + size_fullstr, input);
		strcpy(fullstr + size_fullstr + size_input, "\n");

		free(input);

		cheax_clear_errno(c);
		const char *to_read = fullstr;
		*out = cheax_readstr_at(c, &to_read, "<stdin>", &out_line, &out_pos);

		/* only one expression is read, but we should still
		 * consider the remaining string for our line count
		 * (bit hacky) */
		for (; *to_read != '\0'; ++to_read)
			out_line += (*to_read == '\n');

		prompt = "… ";
	} while (cheax_is_nil(*out) && cheax_errno(c) == CHEAX_EEOF);

	free(fullstr);
	*line = out_line;
	*pos = out_pos;
	return res;
}

static struct chx_value
clear_fun(CHEAX *c, struct chx_list *args, void *info)
{
	return clear = true, CHEAX_NIL;
}


int
main(void)
{
	CHEAX *c = cheax_init();

	cheax_load_feature(c, "all");

	cheax_defun(c, "show-w", show_w, NULL);
	cheax_defun(c, "show-c", show_c, NULL);

	cheax_defun(c, "clear", clear_fun, NULL);

	bool hide_nil = true;
	cheax_sync_bool(c, "cheaky-hide-nil", &hide_nil, 0);

	if (cheax_load_prelude(c)) {
		cheax_perror(c, "cheaky");
		return EXIT_FAILURE;
	}

	cheax_config_bool(c, "allow-redef", true);

	int line = 1, pos = 0;

	fputs("cheaky, Copyright (C) 2024 Antonie Blom\n", stderr);
	fputs("cheaky comes with ABSOLUTELY NO WARRANTY; for details type `(show-w)'.\n", stderr);
	fputs("This is free software, and you are welcome to redistribute it\n", stderr);
	fputs("under certain conditions; type `(show-c)' for details.\n", stderr);

	struct chx_value v;
	while (0 == read_with_readline(c, &line, &pos, &v)) {
		/* read error */
		cheax_ft(c, pad);

		v = cheax_preproc(c, v);
		cheax_ft(c, pad);

		v = cheax_eval(c, v);
		cheax_ft(c, pad);

		if (!cheax_is_nil(v) || !hide_nil) {
			cheax_print(c, stdout, v);
			printf("\n");
		}

		if (clear) {
			printf("\033[2J\033[H");
			clear = false;
		}

		continue;
pad:
		cheax_perror(c, "cheaky");
		cheax_clear_errno(c);
	}

	cheax_destroy(c);
	return 0;
}
