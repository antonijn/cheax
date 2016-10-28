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
#include <stdio.h>

int main(void)
{
	CHEAX *c = cheax_init();
	bool quit = false;
	cheax_sync(c, "quit", CHEAX_BOOL, &quit);
	FILE *prel = fopen(CMAKE_INSTALL_PREFIX "/share/cheax/prelude.chx", "rb");
	cheax_exec(c, prel);
	fclose(prel);
	while (!quit) {
		printf("> ");
		cheax_print(stdout, cheax_eval(c, cheax_read(stdin)));
		printf("\n");
	}
	cheax_destroy(c);
	return 0;
}
