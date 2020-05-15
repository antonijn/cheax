/*
 * Copyright (c) 2020, Antonie Blom
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
int num_input_files = 0;
const char *input_files[MAX_INPUT_FILES];
bool read_stdin = false, use_prelude = true;

static void print_usage(void)
{
	static const char usage[] =
		"Usage: cheax [OPTION]... [FILE]...\n"
		"Executes CHEAX programs.\n\n"
		"Options:\n"
		"  -p\t\tDon't load prelude.\n"
		"  --help\tShow this message.\n";

	puts(usage);
}

static int handle_string_option(const char *arg)
{
	if (!strcmp(arg, "--help")) {
		print_usage();
		exit(0);
	}

	fprintf(stderr, "unknown option '%s'\n", arg);
	return -1;
}
static int handle_option(char opt)
{
	switch (opt) {
	case 'p':
		use_prelude = true;
		break;
	default:
		fprintf(stderr, "unknown option '%c'\n", opt);
		return -1;
	}
	return 0;
}
static int handle_arg(const char *arg)
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
		return handle_string_option(arg);

	for (int i = 1; arg[i]; ++i)
		if (handle_option(arg[i]))
			return -1;

	return 0;
}
static int handle_args(int argc, char **argv)
{
	for (int i = 1; i < argc; ++i)
		if (handle_arg(argv[i]))
			return -1;

	return 0;
}

int main(int argc, char **argv)
{		
	if (handle_args(argc, argv)) {
		print_usage();
		return EXIT_FAILURE;
	}

	CHEAX *c = cheax_init();
	if (use_prelude && cheax_load_prelude(c)) {
		perror("failed to load prelude");
		return EXIT_FAILURE;
	}

	cheax_load_extra_builtins(c, CHEAX_ALL_BUILTINS);

	if (read_stdin)
		cheax_exec(c, stdin);

	for (int i = 0; i < num_input_files; ++i) {
		FILE *f = fopen(input_files[i], "rb");
		if (!f) {
			perror("failed to open input file");
			return EXIT_FAILURE;
		}
		cheax_exec(c, f);
		fclose(f);
	}

	cheax_destroy(c);
}
