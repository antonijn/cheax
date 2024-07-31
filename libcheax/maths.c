/* Copyright (c) 2024, Antonie Blom
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

static struct chx_value
bltn_acos(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(acos(x)));
}

static struct chx_value
bltn_acosh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(acosh(x)));
}

static struct chx_value
bltn_asin(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(asin(x)));
}

static struct chx_value
bltn_asinh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(asinh(x)));
}

static struct chx_value
bltn_atan(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(atan(x)));
}

static struct chx_value
bltn_atan2(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x, y;
	if (cheax_unpack_(c, args, "##", &x, &y) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(atan2(x, y)));
}

static struct chx_value
bltn_atanh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}
	if (x == 1.0 || x == -1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "range error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(atanh(x)));
}

static struct chx_value
bltn_cbrt(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(cbrt(x)));
}

static struct chx_value
bltn_ceil(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_int(ceil(x)));
}

static struct chx_value
bltn_cos(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(cos(x)));
}

static struct chx_value
bltn_cosh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	/* TODO deal with ERANGE? */

	return cheax_bt_wrap_(c, cheax_double(cosh(x)));
}

static struct chx_value
bltn_erf(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x != 0.0 && !isnormal(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "floating point underflow");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(erf(x)));
}

static struct chx_value
bltn_exp(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	/* TODO deal with ERANGE? */

	return cheax_bt_wrap_(c, cheax_double(exp(x)));
}

static struct chx_value
bltn_expm1(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	/* TODO deal with ERANGE? */

	return cheax_bt_wrap_(c, cheax_double(expm1(x)));
}

static struct chx_value
bltn_floor(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_int(floor(x)));
}

static struct chx_value
bltn_ldexp(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	chx_int e;
	if (cheax_unpack_(c, args, "#I", &x, &e) < 0)
		return CHEAX_NIL;

	/* TODO deal with ERANGE? */

	return cheax_bt_wrap_(c, cheax_double(ldexp(x, e)));
}

static struct chx_value
bltn_lgamma(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x <= 0.0 && (int)x == x) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(lgamma(x)));
}

static struct chx_value
bltn_log(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(log(x)));
}

static struct chx_value
bltn_log10(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(log10(x)));
}

static struct chx_value
bltn_log1p(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}
	if (x == -1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(log1p(x)));
}

static struct chx_value
bltn_log2(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(log2(x)));
}

static struct chx_value
bltn_nextafter(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x, y;
	if (cheax_unpack_(c, args, "##", &x, &y) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(nextafter(x, y)));
}

static struct chx_value
bltn_pow(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x, y;
	if (cheax_unpack_(c, args, "##", &x, &y) < 0)
		return CHEAX_NIL;

	if (x < 0.0 && isfinite(y) && (int)y != y) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}
	if (x == 0.0 && y < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(pow(x, y)));
}

static struct chx_value
bltn_round(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_int(round(x)));
}

static struct chx_value
bltn_sin(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(sin(x)));
}

static struct chx_value
bltn_sinh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(sinh(x)));
}

static struct chx_value
bltn_sqrt(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(sqrt(x)));
}

static struct chx_value
bltn_tan(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (isinf(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(tan(x)));
}

static struct chx_value
bltn_tanh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_double(tanh(x)));
}

static struct chx_value
bltn_tgamma(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	if (x < 0.0 && isinf(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return cheax_bt_wrap_(c, CHEAX_NIL);
	}

	return cheax_bt_wrap_(c, cheax_double(tgamma(x)));
}

static struct chx_value
bltn_trunc(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (cheax_unpack_(c, args, "#", &x) < 0)
		return CHEAX_NIL;

	return cheax_bt_wrap_(c, cheax_int(trunc(x)));
}

void
cheax_export_math_bltns_(CHEAX *c)
{
	cheax_defun(c, "acos",      bltn_acos,      NULL);
	cheax_defun(c, "acosh",     bltn_acosh,     NULL);
	cheax_defun(c, "asin",      bltn_asin,      NULL);
	cheax_defun(c, "asinh",     bltn_asinh,     NULL);
	cheax_defun(c, "atan",      bltn_atan,      NULL);
	cheax_defun(c, "atan2",     bltn_atan2,     NULL);
	cheax_defun(c, "atanh",     bltn_atanh,     NULL);
	cheax_defun(c, "cbrt",      bltn_cbrt,      NULL);
	cheax_defun(c, "ceil",      bltn_ceil,      NULL);
	cheax_defun(c, "cos",       bltn_cos,       NULL);
	cheax_defun(c, "cosh",      bltn_cosh,      NULL);
	cheax_defun(c, "erf",       bltn_erf,       NULL);
	cheax_defun(c, "exp",       bltn_exp,       NULL);
	cheax_defun(c, "expm1",     bltn_expm1,     NULL);
	cheax_defun(c, "floor",     bltn_floor,     NULL);
	cheax_defun(c, "ldexp",     bltn_ldexp,     NULL);
	cheax_defun(c, "lgamma",    bltn_lgamma,    NULL);
	cheax_defun(c, "log",       bltn_log,       NULL);
	cheax_defun(c, "log10",     bltn_log10,     NULL);
	cheax_defun(c, "log1p",     bltn_log1p,     NULL);
	cheax_defun(c, "log2",      bltn_log2,      NULL);
	cheax_defun(c, "nextafter", bltn_nextafter, NULL);
	cheax_defun(c, "pow",       bltn_pow,       NULL);
	cheax_defun(c, "round",     bltn_round,     NULL);
	cheax_defun(c, "sin",       bltn_sin,       NULL);
	cheax_defun(c, "sinh",      bltn_sinh,      NULL);
	cheax_defun(c, "sqrt",      bltn_sqrt,      NULL);
	cheax_defun(c, "tan",       bltn_tan,       NULL);
	cheax_defun(c, "tanh",      bltn_tanh,      NULL);
	cheax_defun(c, "tgamma",    bltn_tgamma,    NULL);
	cheax_defun(c, "trunc",     bltn_trunc,     NULL);

	cheax_def(c, "pi",   cheax_double(M_PI),      CHEAX_READONLY);
	cheax_def(c, "nan",  cheax_double(+NAN),      CHEAX_READONLY);
	cheax_def(c, "-nan", cheax_double(-NAN),      CHEAX_READONLY);
	cheax_def(c, "inf",  cheax_double(+INFINITY), CHEAX_READONLY);
	cheax_def(c, "-inf", cheax_double(-INFINITY), CHEAX_READONLY);
}
