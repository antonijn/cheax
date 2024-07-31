/*
 * Copyright (c) 2024, Antonie Blom
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

typedef void (*cmd_action)(CHEAX *c, const char *cmd);
typedef void (*stdin_action)(CHEAX *c);
typedef void (*path_action)(CHEAX *c, const char *path);

#define MAX_INPUT_FILES 16
#define TERM_WIDTH      80
#define HELP_MARGIN     24

/* cheax_config_help() output */
static size_t num_cfg_opts;
static struct chx_config_help *cfg_help;

/* program input files */
static size_t num_input_files = 0;
static const char *input_files[MAX_INPUT_FILES];

/* -c CMD */
static const char *cmd = NULL;

static bool read_stdin = false, use_prelude = true, preproc_only = false;

static CHEAX *c;
static const char *progname;

/*
 * Print text as column, with left margin HELP_MARGIN, and right margin
 * TERM_WIDTH. Used to print --help message.
 */
static void
print_column(const char *msg, int head_start)
{
	int len = strlen(msg);
	int colwidth = TERM_WIDTH - HELP_MARGIN;
	char *buf = malloc(colwidth + 1);

	while (len > 0) {
		for (int i = head_start; i < HELP_MARGIN; ++i)
			putchar(' ');
		head_start = 0;

		while (isspace(*msg)) {
			++msg;
			--len;
		}

		int lnwidth = (len < colwidth) ? len : colwidth;
		memcpy(buf, msg, lnwidth);
		buf[lnwidth] = '\0';

		if (isgraph(msg[lnwidth])) {
			/* need to break off word */
			char *space = strrchr(buf, ' ');
			if (space == NULL)
				space = buf + lnwidth;

			*space = '\0';
			lnwidth = space - buf;
		}

		puts(buf);

		len -= lnwidth;
		msg += lnwidth;
	}

	free(buf);
}

static void
print_opt_info(const char *name, const char *metavar, const char *help)
{
	int head_start = 0;
	head_start += printf("  %s%s ", (strlen(name) > 1) ? "--" : "-", name);
	if (metavar != NULL)
		head_start += printf("%s ", metavar);

	if (head_start > HELP_MARGIN) {
		/* scoot to next line */
		putchar('\n');
		head_start = 0;
	}

	print_column(help, head_start);
}

static void
print_usage(void)
{
	static const char usage[] =
		"Usage: cheax [OPTION]... [FILE]...\n"
		"Executes cheax programs.\n\n"
		"Options:";

	puts(usage);

	struct { const char *name, *metavar, *help; } opts[] = {
		{ "c",       "CMD", "Read and evaluate command CMD."                },
		{ "E",       NULL,  "Preprocess only, don't evaluate expressions. "
		                    "Output written to stdout."                     },
		{ "p",       NULL,  "Don't load prelude."                           },
		{ "help",    NULL,  "Show this message"                             },
		{ "version", NULL,  "Show cheax version information."               },
	};

	for (size_t i = 0; i < sizeof(opts) / sizeof(opts[0]); ++i)
		print_opt_info(opts[i].name, opts[i].metavar, opts[i].help);

	for (size_t i = 0; i < num_cfg_opts; ++i)
		print_opt_info(cfg_help[i].name, cfg_help[i].metavar, cfg_help[i].help);

	putchar('\n');
}

static void
print_version(void)
{
	printf("libcheax %s\n", cheax_version());
}

static int
parse_int(const char *value, int *out)
{
	char *endptr;
	long i = strtol(value, &endptr, 10);
	for (; *endptr != '\0'; ++endptr)
		if (isgraph(*endptr))
			return -1;

	if (errno == ERANGE || errno == EINVAL || i > INT_MAX || i < INT_MIN)
		return -1;

	*out = i;
	return 0;
}

static int
parse_bool(const char *value, bool *out)
{
	if (0 == strcmp(value, "true") || 0 == strcmp(value, "1") || 0 == strcmp(value, "yes")) {
		*out = true;
		return 0;
	}

	if (0 == strcmp(value, "false") || 0 == strcmp(value, "0") || 0 == strcmp(value, "no")) {
		*out = false;
		return 0;
	}

	return -1;
}

/* set cheax_config() */
static int
set_config(struct chx_config_help *ch, const char *value)
{
	int i;
	bool b;

	switch (ch->type) {
	case CHEAX_INT:
		if (parse_int(value, &i) < 0) {
			fprintf(stderr, "invalid int `%s'\n", value);
			return -1;
		}
		cheax_config_int(c, ch->name, i);
		break;
	case CHEAX_BOOL:
		if (parse_bool(value, &b) < 0) {
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

	if (0 == strcmp(arg, "--version")) {
		print_version();
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

	for (size_t i = 0; i < num_cfg_opts; ++i) {
		if (0 != strncmp(config_opt, cfg_help[i].name, opt_len))
			continue;

		if (value == NULL) {
			if (*arg_idx + 1 >= argc) {
				fprintf(stderr, "expected value after `%s'\n", arg);
				return -1;
			}
			value = argv[++*arg_idx];
		}

		return set_config(&cfg_help[i], value);
	}

	fprintf(stderr, "unknown option `%s'\n", arg);
	return -1;
}
static int
handle_option(char opt, int *arg_idx, int argc, char **argv)
{
	switch (opt) {
	case 'c':
		if (*arg_idx + 1 >= argc) {
			fprintf(stderr, "expected command after `-c'\n");
			return -1;
		}

		cmd = argv[++*arg_idx];
		break;
	case 'E':
		preproc_only = true;
		break;
	case 'p':
		use_prelude = false;
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
exec_path(CHEAX *c, const char *path)
{
	cheax_exec(c, path);
}

static void
preproc_handle(CHEAX *c, FILE *f, const char *name)
{
	int line = 1, pos = 0;
	for (;;) {
		struct chx_value v = cheax_read_at(c, f, name, &line, &pos);
		cheax_ft(c, pad);
		if (cheax_is_nil(v) && feof(f))
			break;

		v = cheax_preproc(c, v);
		cheax_ft(c, pad);

		cheax_print(c, stdout, v);
		fputc('\n', stdout);
	}

	fflush(stdout);
pad:
	return;
}

static void
preproc_path(CHEAX *c, const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		perror(progname);
		return;
	}

	char shebang[2] = { 0 };
	size_t bytes = fread(shebang, 1, 2, f);
	if (shebang[0] == '#' && shebang[1] == '!') {
		while (fgetc(f) != '\n')
			;
	} else {
		for (; bytes > 0; --bytes)
			ungetc(shebang[bytes - 1], f);
	}

	preproc_handle(c, f, path);
	fclose(f);
}

static void
exec_stdin(CHEAX *c)
{
	int line = 1, pos = 0;
	for (;;) {
		struct chx_value v = cheax_read_at(c, stdin, "<stdin>", &line, &pos);
		cheax_ft(c, pad);
		if (cheax_is_nil(v) && feof(stdin))
			break;

		cheax_eval(c, v);
		cheax_ft(c, pad);
	}
pad:
	return;
}

static void
preproc_stdin(CHEAX *c)
{
	preproc_handle(c, stdin, "<stdin>");
}

static void
exec_cmd(CHEAX *c, const char *cmd)
{
	struct chx_value v = cheax_readstr(c, cmd);
	if (cheax_errno(c) != 0)
		cheax_eval(c, v);
}

static void
preproc_cmd(CHEAX *c, const char *cmd)
{
	struct chx_value v = cheax_readstr(c, cmd);
	cheax_ft(c, pad);
	v = cheax_preproc(c, v);
	cheax_ft(c, pad);
	cheax_print(c, stdout, v);
pad:
	return;
}

static void
cleanup(void)
{
	cheax_destroy(c);
	free(cfg_help);
}

int
main(int argc, char **argv)
{
	const char *errstr = progname = argv[0];

	c = cheax_init();
	atexit(cleanup);

	if (cheax_config_help(&cfg_help, &num_cfg_opts) < 0)
		return EXIT_FAILURE;

	if (handle_args(argc, argv) < 0)
		return EXIT_FAILURE;

	cheax_load_feature(c, "all");

	if (use_prelude && cheax_load_prelude(c) < 0)
		goto pad;

	cmd_action cmd_act = exec_cmd;
	stdin_action stdin_act = exec_stdin;
	path_action path_act = exec_path;

	if (preproc_only) {
		cmd_act = preproc_cmd;
		stdin_act = preproc_stdin;
		path_act = preproc_path;
	}

	if (cmd != NULL) {
		cmd_act(c, cmd);
		cheax_ft(c, pad);
	}

	if (read_stdin) {
		errstr = "-";
		stdin_act(c);
	}

	for (size_t i = 0; i < num_input_files; ++i) {
		errstr = input_files[i];
		path_act(c, input_files[i]);
		cheax_ft(c, pad);
	}

	return 0;
pad:
	cheax_perror(c, errstr);
	return EXIT_FAILURE;
}
