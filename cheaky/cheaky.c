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

#include <cheax.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <readline/readline.h>
#include <readline/history.h>

int main(void)
{
	CHEAX *c = cheax_init();
	if (cheax_load_prelude(c)) {
		perror("failed to load prelude");
		return EXIT_FAILURE;
	}

	while (true) {
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
