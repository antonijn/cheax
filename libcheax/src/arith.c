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

typedef uint_least64_t chx_uint;

static struct chx_value
do_aop_once(CHEAX *c,
            struct chx_value l,
            struct chx_value r,
            chx_int (*iop)(CHEAX *, chx_int, chx_int),
            chx_double (*fop)(CHEAX *, chx_double, chx_double))
{
	if (l.type == CHEAX_INT && r.type == CHEAX_INT)
		return bt_wrap(c, cheax_int(iop(c, l.data.as_int, r.data.as_int)));

	if (fop == NULL) {
		cheax_throwf(c, CHEAX_ETYPE, "invalid operation on floating point numbers");
		return bt_wrap(c, cheax_nil());
	}

	chx_double ld, rd;
	try_vtod(l, &ld);
	try_vtod(r, &rd);
	return bt_wrap(c, cheax_double(fop(c, ld, rd)));
}

static struct chx_value
do_aop(CHEAX *c,
       struct chx_list *args,
       chx_int (*iop)(CHEAX *, chx_int, chx_int),
       chx_double (*fop)(CHEAX *, chx_double, chx_double))
{
	struct chx_value l, r;
	return (0 == unpack(c, args, "[ID][ID]", &l, &r))
	     ? do_aop_once(c, l, r, iop, fop)
	     : cheax_nil();
}

static struct chx_value
do_assoc_aop(CHEAX *c,
             struct chx_list *args,
             chx_int (*iop)(CHEAX *, chx_int, chx_int),
             chx_double (*fop)(CHEAX *, chx_double, chx_double))
{
	struct chx_value accum;
	if (unpack(c, args, "[ID]_+", &accum, &args) < 0)
		return cheax_nil();

	while (args != NULL) {
		struct chx_value val;
		if (unpack(c, args, "[ID]_*", &val, &args) < 0)
			return cheax_nil();
		accum = do_aop_once(c, accum, val, iop, fop);
	}

	return accum;
}

static chx_int
iop_add(CHEAX *c, chx_int a, chx_int b)
{
	if ((b > 0) && (a > CHX_INT_MAX - b)) {
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}
	if ((b < 0) && (a < CHX_INT_MIN - b)) {
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer underflow");
		return 0;
	}

	return a + b;
}
static chx_double
fop_add(CHEAX *c, chx_double a, chx_double b)
{
	return a + b;
}
static chx_int
iop_sub(CHEAX *c, chx_int a, chx_int b)
{
	if ((b > 0) && (a < CHX_INT_MIN + b)) {
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer underflow");
		return 0;
	}
	if ((b < 0) && (a > CHX_INT_MAX + b)) {
		cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
		return 0;
	}

	return a - b;
}
static chx_double
fop_sub(CHEAX *c, chx_double a, chx_double b)
{
	return a - b;
}
static chx_int
iop_mul(CHEAX *c, chx_int a, chx_int b)
{
	if (((a == -1) && (b == CHX_INT_MIN))
	 || ((b == -1) && (a == CHX_INT_MIN)))
	{
		cheax_throwf(c, CHEAX_EOVERFLOW, "multiplication overflow");
		return 0;
	}

	if (a != 0 && b != 0) {
		/* TODO this relies on non-standard behaviour! */
		chx_int x = a * b;
		if (a != x / b) {
			cheax_throwf(c, CHEAX_EOVERFLOW, "multiplication overflow");
			return 0;
		}
	}

	return a * b;
}
static chx_double
fop_mul(CHEAX *c, chx_double a, chx_double b)
{
	return a * b;
}
static chx_int
iop_div(CHEAX *c, chx_int a, chx_int b)
{
	if (((a == -1) && (b == CHX_INT_MIN))
	 || ((b == -1) && (a == CHX_INT_MIN)))
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
static chx_double
fop_div(CHEAX *c, chx_double a, chx_double b)
{
	return a / b;
}
static chx_int
iop_mod(CHEAX *c, chx_int a, chx_int b)
{
	if (((a == -1) && (b == CHX_INT_MIN))
	 || ((b == -1) && (a == CHX_INT_MIN)))
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

static chx_int
iop_bit_and(CHEAX *c, chx_int a, chx_int b)
{
	return (chx_uint)a & (chx_uint)b;
}
static chx_int
iop_bit_or(CHEAX *c, chx_int a, chx_int b)
{
	return (chx_uint)a | (chx_uint)b;
}
static chx_int
iop_bit_xor(CHEAX *c, chx_int a, chx_int b)
{
	return (chx_uint)a ^ (chx_uint)b;
}

static struct chx_value
bltn_add(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_add, fop_add);
}
static struct chx_value
bltn_sub(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, args, iop_sub, fop_sub);
}
static struct chx_value
bltn_mul(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_mul, fop_mul);
}
static struct chx_value
bltn_div(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, args, iop_div, fop_div);
}
static struct chx_value
bltn_mod(CHEAX *c, struct chx_list *args, void *info)
{
	return do_aop(c, args, iop_mod, NULL);
}

static struct chx_value
bltn_bit_and(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_bit_and, NULL);
}
static struct chx_value
bltn_bit_or(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_bit_or, NULL);
}
static struct chx_value
bltn_bit_xor(CHEAX *c, struct chx_list *args, void *info)
{
	return do_assoc_aop(c, args, iop_bit_xor, NULL);
}

static struct chx_value
bltn_bit_not(CHEAX *c, struct chx_list *args, void *info)
{
	chx_int i;
	return (0 == unpack(c, args, "I", &i))
	     ? bt_wrap(c, cheax_int(~i))
	     : cheax_nil();
}

/* shift mode */
enum {
	BIT_SHIFT,
	ARITH_SHIFT,
	ROTATE,
};

#define UINT_BIT (sizeof(chx_uint) * CHAR_BIT)

static chx_uint
shift(chx_uint i, chx_uint j, bool right, int mode)
{
	const chx_uint hibit = (chx_uint)1U << (sizeof(chx_uint) * 8 - 1);

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

	chx_uint res;
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

static struct chx_value
do_shift(CHEAX *c, struct chx_list *args, bool right, int mode)
{
	chx_int i, j;
	struct chx_value jval;
	if (unpack(c, args, "II?", &i, &jval) < 0)
		return cheax_nil();

	j = cheax_is_nil(jval) ? 1 : jval.data.as_int;
	if (j < 0) {
		if (j == CHX_INT_MIN) {
			cheax_throwf(c, CHEAX_EOVERFLOW, "integer overflow");
			return bt_wrap(c, cheax_nil());
		}

		j = -j;
		right = !right;
	}

	return bt_wrap(c, cheax_int(shift(i, j, right, mode)));
}

static struct chx_value
bltn_bit_shl(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, false, BIT_SHIFT);
}
static struct chx_value
bltn_bit_shr(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, true, BIT_SHIFT);
}
static struct chx_value
bltn_bit_sal(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, false, ARITH_SHIFT);
}
static struct chx_value
bltn_bit_sar(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, true, ARITH_SHIFT);
}
static struct chx_value
bltn_bit_rol(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, false, ROTATE);
}
static struct chx_value
bltn_bit_ror(CHEAX *c, struct chx_list *args, void *info)
{
	return do_shift(c, args, true, ROTATE);
}

static struct chx_value
do_cmp(CHEAX *c, struct chx_list *args, bool lt, bool eq, bool gt)
{
	struct chx_value l, r;
	if (unpack(c, args, "[ID][ID]", &l, &r) < 0)
		return cheax_nil();

	bool is_lt, is_eq, is_gt;

	if (l.type == CHEAX_INT && r.type == CHEAX_INT) {
		chx_int li = l.data.as_int, ri = r.data.as_int;
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

	return cheax_bool((lt && is_lt) || (eq && is_eq) || (gt && is_gt));
}

static struct chx_value
bltn_lt(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, args, 1, 0, 0);
}
static struct chx_value
bltn_le(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, args, 1, 1, 0);
}
static struct chx_value
bltn_gt(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, args, 0, 0, 1);
}
static struct chx_value
bltn_ge(CHEAX *c, struct chx_list *args, void *info)
{
	return do_cmp(c, args, 0, 1, 1);
}

void
export_arith_bltns(CHEAX *c)
{
	cheax_defun(c, "+", bltn_add, NULL);
	cheax_defun(c, "-", bltn_sub, NULL);
	cheax_defun(c, "*", bltn_mul, NULL);
	cheax_defun(c, "/", bltn_div, NULL);
	cheax_defun(c, "%", bltn_mod, NULL);

	cheax_defun(c, "bit-and", bltn_bit_and, NULL);
	cheax_defun(c, "bit-or",  bltn_bit_or,  NULL);
	cheax_defun(c, "bit-xor", bltn_bit_xor, NULL);
	cheax_defun(c, "bit-not", bltn_bit_not, NULL);
	cheax_defun(c, "bit-shl", bltn_bit_shl, NULL);
	cheax_defun(c, "bit-shr", bltn_bit_shr, NULL);
	cheax_defun(c, "bit-sal", bltn_bit_sal, NULL);
	cheax_defun(c, "bit-sar", bltn_bit_sar, NULL);
	cheax_defun(c, "bit-rol", bltn_bit_rol, NULL);
	cheax_defun(c, "bit-ror", bltn_bit_ror, NULL);

	cheax_defun(c, "<",  bltn_lt, NULL);
	cheax_defun(c, "<=", bltn_le, NULL);
	cheax_defun(c, ">",  bltn_gt, NULL);
	cheax_defun(c, ">=", bltn_ge, NULL);

	cheax_def(c, "int-max", cheax_int(CHX_INT_MAX), CHEAX_READONLY);
	cheax_def(c, "int-min", cheax_int(CHX_INT_MIN), CHEAX_READONLY);
}
