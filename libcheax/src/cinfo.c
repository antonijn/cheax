/* Copyright (c) 2022, Antonie Blom
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

#include "cinfo.h"

bool
c_isdigit(int c)
{
	return c >= '0' && c <= '9';
}

bool
c_isspace(int c)
{
	return c == ' '  || c == '\n' || c == '\r'
	    || c == '\t' || c == '\v' || c == '\f';
}

bool
c_isgraph(int c)
{
	return c >= '!' && c <= '~';
}

bool
c_isprint(int c)
{
	return c_isgraph(c) || c_isspace(c);
}

bool
c_isid(int c)
{
	return c_isgraph(c)
	    && c != '(' && c != ')' && c != '\'' && c != '`'
	    && c != ',' && c != '"' && c != ';';
}

bool
c_isid_initial(int c)
{
	return !c_isdigit(c) && c_isid(c);
}

int
c_todigit(int c, int base)
{
	int max_digit = (base <= 10) ? '0' + base - 1 : '9';
	if (c >= '0' && c <= max_digit)
		return c - '0';
	if (base == 16 && c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	if (base == 16 && c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}
