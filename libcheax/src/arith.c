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
do_aop_once(CHEAX *c,
            struct chx_value *l,
            struct chx_value *r,
            int (*iop)(CHEAX *, int, int),
            double (*fop)(CHEAX *, double, double))
{
	if (cheax_type_of(l) == CHEAX_INT && cheax_type_of(r) == CHEAX_INT) {
		int li = ((struct chx_int *)l)->value;
		int ri = ((struct chx_int *)r)->value;
		int res = iop(c, li, ri);
		return bt_wrap(c, &cheax_int(c, res)->base);
	}

	if (fop == NULL) {
		cheax_throwf(c, CHEAX_ETYPE, "invalid operation on floating point numbers");
		return bt_wrap(c, NULL);
	}

	double ld, rd;
	try_vtod(l, &ld);
	try_vtod(r, &rd);
	return bt_wrap(c, &cheax_double(c, fop(c, ld, rd))->base);
}

static struct chx_value *
do_aop(CHEAX *c,
       struct chx_list *args,
       int (*iop)(CHEAX *, int, int),
       double (*fop)(CHEAX *, double, double))
{
	struct chx_value *l, *r;
	return (0 == unpack(c, args, "[id][id]", &l, &r))
	     ? do_aop_once(c, l, r, iop, fop)
	     : NULL;
}

static struct chx_value *
do_assoc_aop(CHEAX *c,
             struct chx_list *args,
             int (*iop)(CHEAX *, int, int),
             double (*fop)(CHEAX *, double, double))
{
	struct chx_value *accum;
	if (unpack(c, args, "[id]_+", &accum, &args) < 0)
		return NULL;

	while (args != NULL) {
		struct chx_value *val;
		if (unpack(c, args, "[id]_*", &val, &args) < 0)
			return NULL;
		accum = do_aop_once(c, accum, val, iop, fop);
	}

	return accum;
}

static int
iop_add(CHEAX *c, int a, int b)
{
	if ((b > 0) && (a > INT_MAX - b)) {
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}
	if ((b < 0) && (a < INT_MIN - b)) {
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer underflow");
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
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer underflow");
		return 0;
	}
	if ((b < 0) && (a > INT_MAX + b)) {
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
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
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	if (b != 0) {
		if (a > INT_MAX / b) {
			cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
			return 0;
		}
		if (a < INT_MIN / b) {
			cheax_throwf(c, CHEAX_EOVERFLOW, "integer underflow");
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
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	if (b == 0) {
		cheax_throwf(c, CHEAX_EDIVZERO, "division by zero");
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
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	if (b == 0) {
		cheax_throwf(c, CHEAX_EDIVZERO, "division by zero");
		return 0;
	}

	return a % b;
}

static int
iop_bit_and(CHEAX *c, int a, int b)
{
	return (int)((unsigned)a & (unsigned)b);
}
static int
iop_bit_or(CHEAX *c, int a, int b)
{
	return (int)((unsigned)a | (unsigned)b);
}
static int
iop_bit_xor(CHEAX *c, int a, int b)
{
	return (int)((unsigned)a ^ (unsigned)b);
}

static struct chx_value *
bltn_add(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_add, fop_add);
}
static struct chx_value *
bltn_sub(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, args, iop_sub, fop_sub);
}
static struct chx_value *
bltn_mul(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_mul, fop_mul);
}
static struct chx_value *
bltn_div(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, args, iop_div, fop_div);
}
static struct chx_value *
bltn_mod(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, args, iop_mod, NULL);
}

static struct chx_value *
bltn_bit_and(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_bit_and, NULL);
}
static struct chx_value *
bltn_bit_or(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_bit_or, NULL);
}
static struct chx_value *
bltn_bit_xor(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_bit_xor, NULL);
}

static struct chx_value *
bltn_bit_not(CHEAX *c, struct chx_list *args, void *info)
{
	int i;
	return (0 == unpack(c, args, "i!", &i))
	     ? bt_wrap(c, &cheax_int(c, ~i)->base)
	     : NULL;
}

/* shift mode */
enum {
	BIT_SHIFT,
	ARITH_SHIFT,
	ROTATE,
};

#define UINT_BIT (sizeof(unsigned) * CHAR_BIT)

static unsigned
shift(unsigned i, unsigned j, bool right, int mode)
{
	const unsigned hibit = 1U << (UINT_BIT - 1);

	if (mode == ROTATE) {
		j %= UINT_BIT;
		if (j == 0)
			return i;

		return right
		     ? (i >> j) | (i << (UINT_BIT - j))
		     : (i << j) | (i >> (UINT_BIT - j));
	}

	if (j == 0)
		return i;

	unsigned res;
	if (j >= UINT_BIT) {
		j = UINT_BIT;
		res = 0;
	} else {
		res = right ? i >> j : i << j;
	}

	if (right && mode == ARITH_SHIFT && (i & hibit) != 0)
		res |= ~0U << (UINT_BIT - j);

	return res;
}

static struct chx_value *
do_shift(CHEAX *c, struct chx_list *args, bool right, int mode)
{
	int i, j;
	struct chx_int *jval;
	if (unpack(c, args, "i!i?", &i, &jval) < 0)
		return NULL;

	j = (jval == NULL) ? 1 : jval->value;
	if (j < 0) {
		if (j == INT_MIN) {
			cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
			return bt_wrap(c, NULL);
		}

		j = -j;
		right = !right;
	}

	return bt_wrap(c, &cheax_int(c, shift(i, j, right, mode))->base);
}

static struct chx_value *
bltn_bit_shl(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, false, BIT_SHIFT);
}
static struct chx_value *
bltn_bit_shr(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, true, BIT_SHIFT);
}
static struct chx_value *
bltn_bit_sal(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, false, ARITH_SHIFT);
}
static struct chx_value *
bltn_bit_sar(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, true, ARITH_SHIFT);
}
static struct chx_value *
bltn_bit_rol(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, false, ROTATE);
}
static struct chx_value *
bltn_bit_ror(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, true, ROTATE);
}

static struct chx_value *
do_cmp(CHEAX *c,
       const char *name,
       struct chx_list *args,
       bool lt, bool eq, bool gt)
{
	struct chx_value *l, *r;
	if (unpack(c, args, "[id][id]", &l, &r) < 0)
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

	cheax_defmacro(c, "bit-and", bltn_bit_and, NULL);
	cheax_defmacro(c, "bit-or",  bltn_bit_or,  NULL);
	cheax_defmacro(c, "bit-xor", bltn_bit_xor, NULL);
	cheax_defmacro(c, "bit-not", bltn_bit_not, NULL);
	cheax_defmacro(c, "bit-shl", bltn_bit_shl, NULL);
	cheax_defmacro(c, "bit-shr", bltn_bit_shr, NULL);
	cheax_defmacro(c, "bit-sal", bltn_bit_sal, NULL);
	cheax_defmacro(c, "bit-sar", bltn_bit_sar, NULL);
	cheax_defmacro(c, "bit-rol", bltn_bit_rol, NULL);
	cheax_defmacro(c, "bit-ror", bltn_bit_ror, NULL);

	cheax_defmacro(c, "<",  bltn_lt, NULL);
	cheax_defmacro(c, "<=", bltn_le, NULL);
	cheax_defmacro(c, ">",  bltn_gt, NULL);
	cheax_defmacro(c, ">=", bltn_ge, NULL);

	cheax_def(c, "int-max", &cheax_int(c, INT_MAX)->base, CHEAX_READONLY);
	cheax_def(c, "int-min", &cheax_int(c, INT_MIN)->base, CHEAX_READONLY);
}
