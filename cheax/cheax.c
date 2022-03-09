/*
 * Copyright (c) 2022, Antonie Blom
 * 
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <cheax.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_INPUT_FILES 16
static int num_input_files = 0;
static const char *input_files[MAX_INPUT_FILES], *cmd = NULL;
static bool read_stdin = false, use_prelude = true;
static struct chx_config_help *help;
static size_t num_opts;
static CHEAX *c;

static void
pretty(const char *help, int head_start, int margin, int twidth)
{
	int len = strlen(help);
	int colwidth = twidth - margin;
	char *buf = malloc(colwidth + 1);

	while (len > 0) {
		for (int i = head_start; i < margin; ++i)
			putchar(' ');
		head_start = 0;

		while (isspace(*help)) {
			++help;
			--len;
		}

		int lnwidth = (len < colwidth) ? len : colwidth;
		memcpy(buf, help, lnwidth);
		buf[lnwidth] = '\0';

		if (isgraph(help[lnwidth])) {
			/* need to break-off word */
			char *space = strrchr(buf, ' ');
			if (space == NULL)
				space = buf + lnwidth;

			*space = '\0';
			lnwidth = space - buf;
		}

		puts(buf);

		len -= lnwidth;
		help += lnwidth;
	}

	free(buf);
}

static void
print_usage(void)
{
	const int margin = 24, twidth = 80;
	static const char usage[] =
		"Usage: cheax [OPTION]... [FILE]...\n"
		"Executes cheax programs.\n\n"
		"Options:\n"
		"  -c CMD                Read and evaluate command CMD.\n"
		"  -p                    Don't load prelude.\n"
		"  --help                Show this message.";

	puts(usage);

	for (size_t i = 0; i < num_opts; ++i) {
		const char *metavar;
		switch (help[i].type) {
		case CHEAX_INT:  metavar = "N";          break;
		case CHEAX_BOOL: metavar = "true/false"; break;
		default:         metavar = "???";        break;
		}

		int head_start = printf("  --%s %s ", help[i].name, metavar);
		if (head_start > margin) {
			putchar('\n');
			head_start = 0;
		}
		pretty(help[i].help, head_start, margin, twidth);
	}

	putchar('\n');
}

static int
handle_config_option(struct chx_config_help *ch, const char *value)
{
	char *endptr;
	long i;
	bool valid_num = true, b, valid_bool = false;

	switch (ch->type) {
	case CHEAX_INT:
		i = strtol(value, &endptr, 10);
		for (; *endptr != '\0'; ++endptr) {
			if (isgraph(*endptr)) {
				valid_num = false;
				break;
			}
		}

		if (errno == ERANGE || errno == EINVAL)
			valid_num = false;

		if (i > INT_MAX || i < INT_MIN)
			valid_num = false;

		if (!valid_num) {
			fprintf(stderr, "invalid int `%s'\n", value);
			return -1;
		}

		cheax_config_int(c, ch->name, i);
		break;
	case CHEAX_BOOL:
		if (0 == strcmp(value, "yes")
		 || 0 == strcmp(value, "1")
		 || 0 == strcmp(value, "true"))
		{
			b = valid_bool = true;
		}
		else
		if (0 == strcmp(value, "no")
		 || 0 == strcmp(value, "0")
		 || 0 == strcmp(value, "false"))
		{
			b = !(valid_bool = true);
		}

		if (!valid_bool) {
			fprintf(stderr, "invalid bool `%s'\n", value);
			return -1;
		}

		cheax_config_bool(c, ch->name, b);
		break;
	default:
		return -1;
	}

	if (cheax_errno(c) != 0) {
		cheax_perror(c, NULL);
		return -1;
	}

	return 0;
}

static int
handle_string_option(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (0 == strcmp(arg, "--help")) {
		print_usage();
		exit(0);
	}


	/* try to read cheax_config() option */
	size_t opt_len;
	const char *config_opt, *eq, *value;

	config_opt = arg + 2;
	eq = strchr(config_opt, '=');
	if (eq != NULL) {
		opt_len = eq - config_opt;
		value = eq + 1;
	} else {
		opt_len = strlen(config_opt);
		value = NULL;
	}

	for (size_t i = 0; i < num_opts; ++i) {
		if (0 != strncmp(config_opt, help[i].name, opt_len))
			continue;

		if (value == NULL) {
			if (*arg_idx + 1 >= argc) {
				fprintf(stderr, "expected value after `%s'\n", arg);
				return -1;
			}
			value = argv[++*arg_idx];
		}

		return handle_config_option(&help[i], value);
	}

	fprintf(stderr, "unknown option `%s'\n", arg);
	return -1;
}
static int
handle_option(char opt, int *arg_idx, int argc, char **argv)
{
	switch (opt) {
	case 'p':
		use_prelude = false;
		break;
	case 'c':
		if (*arg_idx + 1 >= argc) {
			fprintf(stderr, "expected command after `-c'\n");
			return -1;
		}

		cmd = argv[++*arg_idx];
		break;
	default:
		fprintf(stderr, "unknown option '%c'\n", opt);
		print_usage();
		return -1;
	}
	return 0;
}
static int
handle_arg(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (arg[0] != '-') {
		if (num_input_files >= MAX_INPUT_FILES) {
			fprintf(stderr, "maximum number of input files is %d\n", MAX_INPUT_FILES);
			return -1;
		}
		input_files[num_input_files++] = arg;
		return 0;
	}

	if (arg[1] == '\0') {
		read_stdin = true;
		return 0;
	}
	if (arg[1] == '-')
		return handle_string_option(arg, arg_idx, argc, argv);

	for (int i = 1; arg[i]; ++i)
		if (handle_option(arg[i], arg_idx, argc, argv))
			return -1;

	return 0;
}
static int
handle_args(int argc, char **argv)
{
	for (int i = 1; i < argc; ++i)
		if (handle_arg(argv[i], &i, argc, argv))
			return -1;

	if (!read_stdin && num_input_files == 0 && cmd == NULL) {
		fputs("no input files\n", stderr);
		return -1;
	}

	if (cmd != NULL && (read_stdin || num_input_files > 0)) {
		fputs("cannot specify both a command and input files\n", stderr);
		return -1;
	}

	return 0;
}

static void
cleanup(void)
{
	cheax_destroy(c);
	free(help);
}

int
main(int argc, char **argv)
{		
	c = cheax_init();
	atexit(cleanup);

	if (cheax_config_help(&help, &num_opts) < 0)
		return EXIT_FAILURE;

	if (handle_args(argc, argv) < 0)
		return EXIT_FAILURE;

	cheax_load_feature(c, "all");

	if (use_prelude && cheax_load_prelude(c) < 0) {
		cheax_perror(c, argv[0]);
		return EXIT_FAILURE;
	}

	if (cmd != NULL) {
		cheax_eval(c, cheax_readstr(c, cmd));
		if (cheax_errno(c) != 0) {
			cheax_perror(c, "cheax");
			return EXIT_FAILURE;
		}
	}

	if (read_stdin) {
		cheax_exec(c, stdin);
		if (cheax_errno(c) != 0) {
			cheax_perror(c, "-");
			return EXIT_FAILURE;
		}
	}

	for (int i = 0; i < num_input_files; ++i) {
		FILE *f = fopen(input_files[i], "rb");
		if (f == NULL) {
			perror("failed to open input file");
			return EXIT_FAILURE;
		}
		cheax_exec(c, f);
		fclose(f);
		if (cheax_errno(c) != 0) {
			cheax_perror(c, input_files[i]);
			return EXIT_FAILURE;
		}
	}

	return 0;
}
