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

#include <limits.h>

#include "core.h"
#include "err.h"
#include "unpack.h"

static struct chx_value *
do_aop(CHEAX *c,
       const char *name,
       struct chx_list *args,
       int (*iop)(CHEAX *, int, int),
       double (*fop)(CHEAX *, double, double))
{
	struct chx_value *l, *r;
	if (unpack(c, name, args, "[id][id]", &l, &r) < 0)
		return NULL;

	if (cheax_type_of(l) == CHEAX_INT && cheax_type_of(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		int res = iop(c, li, ri);
		if (cheax_errstate(c) == CHEAX_THROWN)
			return NULL;

		return &cheax_int(c, res)->base;
	}

	if (fop == NULL) {
		cry(c, name, CHEAX_ETYPE, "invalid operation on floating point numbers");
		return NULL;
	}

	double ld, rd;
	try_vtod(l, &ld);
	try_vtod(r, &rd);
	return &cheax_double(c, fop(c, ld, rd))->base;
}

static int
iop_add(CHEAX *c, int a, int b)
{
	if ((b > 0) && (a > INT_MAX - b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}
	if ((b < 0) && (a < INT_MIN - b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "integer underflow");
		return 0;
	}

	return a + b;
}
static double
fop_add(CHEAX *c, double a, double b)
{
	return a + b;
}
static int
iop_sub(CHEAX *c, int a, int b)
{
	if ((b > 0) && (a < INT_MIN + b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "integer underflow");
		return 0;
	}
	if ((b < 0) && (a > INT_MAX + b)) {
		cry(c, "+", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	return a - b;
}
static double
fop_sub(CHEAX *c, double a, double b)
{
	return a - b;
}
static int
iop_mul(CHEAX *c, int a, int b)
{
	if (((a == -1) && (b == INT_MIN))
	 || ((b == -1) && (a == INT_MIN)))
	{
		cry(c, "*", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	if (b != 0) {
		if (a > INT_MAX / b) {
			cry(c, "*", CHEAX_EOVERFLOW, "integer overflow");
			return 0;
		}
		if (a < INT_MIN / b) {
			cry(c, "+", CHEAX_EOVERFLOW, "integer underflow");
			return 0;
		}
	}

	return a * b;
}
static double
fop_mul(CHEAX *c, double a, double b)
{
	return a * b;
}
static int
iop_div(CHEAX *c, int a, int b)
{
	if (((a == -1) && (b == INT_MIN))
	 || ((b == -1) && (a == INT_MIN)))
	{
		cry(c, "/", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	if (b == 0) {
		cry(c, "/", CHEAX_EDIVZERO, "division by zero");
		return 0;
	}

	return a / b;
}
static double
fop_div(CHEAX *c, double a, double b)
{
	return a / b;
}
static int
iop_mod(CHEAX *c, int a, int b)
{
	if (((a == -1) && (b == INT_MIN))
	 || ((b == -1) && (a == INT_MIN)))
	{
		cry(c, "%", CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	if (b == 0) {
		cry(c, "%", CHEAX_EDIVZERO, "division by zero");
		return 0;
	}

	return a % b;
}

static struct chx_value *
bltn_add(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "+", args, iop_add, fop_add);
}
static struct chx_value *
bltn_sub(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "-", args, iop_sub, fop_sub);
}
static struct chx_value *
bltn_mul(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "*", args, iop_mul, fop_mul);
}
static struct chx_value *
bltn_div(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "/", args, iop_div, fop_div);
}
static struct chx_value *
bltn_mod(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, "%", args, iop_mod, NULL);
}

static struct chx_value *
do_cmp(CHEAX *c,
       const char *name,
       struct chx_list *args,
       bool lt, bool eq, bool gt)
{
	struct chx_value *l, *r;
	if (unpack(c, name, args, "[id][id]", &l, &r) < 0)
		return NULL;

	bool is_lt, is_eq, is_gt;

	if (cheax_type_of(l) == CHEAX_INT && cheax_type_of(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		is_lt = li < ri;
		is_eq = li == ri;
		is_gt = li > ri;
	} else {
		double ld, rd;
		try_vtod(l, &ld);
		try_vtod(r, &rd);
		is_lt = ld < rd;
		is_eq = ld == rd;
		is_gt = ld > rd;
	}

	return &cheax_bool(c, ((lt && is_lt) || (eq && is_eq) || (gt && is_gt)))->base;
}

static struct chx_value *
bltn_lt(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, "<", args, 1, 0, 0);
}
static struct chx_value *
bltn_le(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, "<=", args, 1, 1, 0);
}
static struct chx_value *
bltn_gt(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, ">", args, 0, 0, 1);
}
static struct chx_value *
bltn_ge(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, ">=", args, 0, 1, 1);
}

void
export_arith_bltns(CHEAX *c)
{
	cheax_defmacro(c, "+",  bltn_add, NULL);
	cheax_defmacro(c, "-",  bltn_sub, NULL);
	cheax_defmacro(c, "*",  bltn_mul, NULL);
	cheax_defmacro(c, "/",  bltn_div, NULL);
	cheax_defmacro(c, "%",  bltn_mod, NULL);
	cheax_defmacro(c, "<",  bltn_lt,  NULL);
	cheax_defmacro(c, "<=", bltn_le,  NULL);
	cheax_defmacro(c, ">",  bltn_gt,  NULL);
	cheax_defmacro(c, ">=", bltn_ge,  NULL);

	cheax_var(c, "int-max", &cheax_int(c, INT_MAX)->base, CHEAX_READONLY);
	cheax_var(c, "int-min", &cheax_int(c, INT_MIN)->base, CHEAX_READONLY);
}
