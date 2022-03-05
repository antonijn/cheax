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
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_INPUT_FILES 16
static int num_input_files = 0;
static const char *input_files[MAX_INPUT_FILES], *cmd = NULL;
static bool read_stdin = false, use_prelude = true;
static CHEAX *c;

static void
print_usage(void)
{
	static const char usage[] =
		"Usage: cheax [OPTION]... [FILE]...\n"
		"Executes cheax programs.\n\n"
		"Options:\n"
		"  -c CMD        Read and evaluate command CMD.\n"
		"  -p            Don't load prelude.\n"
		"  --help        Show this message.\n";

	puts(usage);
}

static int
handle_string_option(const char *arg, int *arg_idx, int argc, char **argv)
{
	if (!strcmp(arg, "--help")) {
		print_usage();
		exit(0);
	}

	fprintf(stderr, "unknown option '%s'\n", arg);
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
			fprintf(stderr, "expected command after '-c'\n");
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
}

int
main(int argc, char **argv)
{		
	if (handle_args(argc, argv))
		return EXIT_FAILURE;

	c = cheax_init();
	atexit(cleanup);

	cheax_load_features(c, CHEAX_ALL_FEATURES);

	if (use_prelude && cheax_load_prelude(c)) {
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
