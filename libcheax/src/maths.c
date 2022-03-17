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

#include <math.h>

#include "core.h"
#include "err.h"
#include "maths.h"
#include "unpack.h"

static struct chx_value *
bltn_acos(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, acos(x))->base);
}

static struct chx_value *
bltn_acosh(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, acosh(x))->base);
}

static struct chx_value *
bltn_asin(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, asin(x))->base);
}

static struct chx_value *
bltn_asinh(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, asinh(x))->base);
}

static struct chx_value *
bltn_atan(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, atan(x))->base);
}

static struct chx_value *
bltn_atan2(CHEAX *c, struct chx_list *args, void *info)
{
	double x, y;
	if (unpack(c, args, "#!#!", &x, &y) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, atan2(x, y))->base);
}

static struct chx_value *
bltn_atanh(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}
	if (x == 1.0 || x == -1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "range error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, atanh(x))->base);
}

static struct chx_value *
bltn_cbrt(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, cbrt(x))->base);
}

static struct chx_value *
bltn_ceil(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_int(c, ceil(x))->base);
}

static struct chx_value *
bltn_cos(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, cos(x))->base);
}

static struct chx_value *
bltn_cosh(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	/* TODO deal with ERANGE? */

	return bt_wrap(c, &cheax_double(c, cosh(x))->base);
}

static struct chx_value *
bltn_erf(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x != 0.0 && !isnormal(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "floating point underflow");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, erf(x))->base);
}

static struct chx_value *
bltn_exp(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	/* TODO deal with ERANGE? */

	return bt_wrap(c, &cheax_double(c, exp(x))->base);
}

static struct chx_value *
bltn_expm1(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	/* TODO deal with ERANGE? */

	return bt_wrap(c, &cheax_double(c, expm1(x))->base);
}

static struct chx_value *
bltn_floor(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_int(c, floor(x))->base);
}

static struct chx_value *
bltn_ldexp(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	int e;
	if (unpack(c, args, "#!i!", &x, &e) < 0)
		return NULL;

	/* TODO deal with ERANGE? */

	return bt_wrap(c, &cheax_double(c, ldexp(x, e))->base);
}

static struct chx_value *
bltn_lgamma(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x <= 0.0 && (int)x == x) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, lgamma(x))->base);
}

static struct chx_value *
bltn_log(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, log(x))->base);
}

static struct chx_value *
bltn_log10(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, log10(x))->base);
}

static struct chx_value *
bltn_log1p(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}
	if (x == -1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, log1p(x))->base);
}

static struct chx_value *
bltn_log2(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, log2(x))->base);
}

static struct chx_value *
bltn_nextafter(CHEAX *c, struct chx_list *args, void *info)
{
	double x, y;
	if (unpack(c, args, "#!#!", &x, &y) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, nextafter(x, y))->base);
}

static struct chx_value *
bltn_pow(CHEAX *c, struct chx_list *args, void *info)
{
	double x, y;
	if (unpack(c, args, "#!#!", &x, &y) < 0)
		return NULL;

	if (x < 0.0 && isfinite(y) && (int)y != y) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}
	if (x == 0.0 && y < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, pow(x, y))->base);
}

static struct chx_value *
bltn_round(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_int(c, round(x))->base);
}

static struct chx_value *
bltn_sin(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, sin(x))->base);
}

static struct chx_value *
bltn_sinh(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, sinh(x))->base);
}

static struct chx_value *
bltn_sqrt(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, sqrt(x))->base);
}

static struct chx_value *
bltn_tan(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (isinf(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, tan(x))->base);
}

static struct chx_value *
bltn_tanh(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_double(c, tanh(x))->base);
}

static struct chx_value *
bltn_tgamma(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	if (x < 0.0 && isinf(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, NULL);
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, NULL);
	}

	return bt_wrap(c, &cheax_double(c, tgamma(x))->base);
}

static struct chx_value *
bltn_trunc(CHEAX *c, struct chx_list *args, void *info)
{
	double x;
	if (unpack(c, args, "#!", &x) < 0)
		return NULL;

	return bt_wrap(c, &cheax_int(c, trunc(x))->base);
}

void
export_math_bltns(CHEAX *c)
{
	cheax_defmacro(c, "acos",      bltn_acos,      NULL);
	cheax_defmacro(c, "acosh",     bltn_acosh,     NULL);
	cheax_defmacro(c, "asin",      bltn_asin,      NULL);
	cheax_defmacro(c, "asinh",     bltn_asinh,     NULL);
	cheax_defmacro(c, "atan",      bltn_atan,      NULL);
	cheax_defmacro(c, "atan2",     bltn_atan2,     NULL);
	cheax_defmacro(c, "atanh",     bltn_atanh,     NULL);
	cheax_defmacro(c, "cbrt",      bltn_cbrt,      NULL);
	cheax_defmacro(c, "ceil",      bltn_ceil,      NULL);
	cheax_defmacro(c, "cos",       bltn_cos,       NULL);
	cheax_defmacro(c, "cosh",      bltn_cosh,      NULL);
	cheax_defmacro(c, "erf",       bltn_erf,       NULL);
	cheax_defmacro(c, "exp",       bltn_exp,       NULL);
	cheax_defmacro(c, "expm1",     bltn_expm1,     NULL);
	cheax_defmacro(c, "floor",     bltn_floor,     NULL);
	cheax_defmacro(c, "ldexp",     bltn_ldexp,     NULL);
	cheax_defmacro(c, "lgamma",    bltn_lgamma,    NULL);
	cheax_defmacro(c, "log",       bltn_log,       NULL);
	cheax_defmacro(c, "log10",     bltn_log10,     NULL);
	cheax_defmacro(c, "log1p",     bltn_log1p,     NULL);
	cheax_defmacro(c, "log2",      bltn_log2,      NULL);
	cheax_defmacro(c, "nextafter", bltn_nextafter, NULL);
	cheax_defmacro(c, "pow",       bltn_pow,       NULL);
	cheax_defmacro(c, "round",     bltn_round,     NULL);
	cheax_defmacro(c, "sin",       bltn_sin,       NULL);
	cheax_defmacro(c, "sinh",      bltn_sinh,      NULL);
	cheax_defmacro(c, "sqrt",      bltn_sqrt,      NULL);
	cheax_defmacro(c, "tan",       bltn_tan,       NULL);
	cheax_defmacro(c, "tanh",      bltn_tanh,      NULL);
	cheax_defmacro(c, "tgamma",    bltn_tgamma,    NULL);
	cheax_defmacro(c, "trunc",     bltn_trunc,     NULL);

	cheax_def(c, "pi",   &cheax_double(c, M_PI)->base,      CHEAX_READONLY);
	cheax_def(c, "nan",  &cheax_double(c, +NAN)->base,      CHEAX_READONLY);
	cheax_def(c, "-nan", &cheax_double(c, -NAN)->base,      CHEAX_READONLY);
	cheax_def(c, "inf",  &cheax_double(c, +INFINITY)->base, CHEAX_READONLY);
	cheax_def(c, "-inf", &cheax_double(c, -INFINITY)->base, CHEAX_READONLY);
}
