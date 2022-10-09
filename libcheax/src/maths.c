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

static struct chx_value
sf_acos(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(acos(x)));
}

static struct chx_value
sf_acosh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(acosh(x)));
}

static struct chx_value
sf_asin(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(asin(x)));
}

static struct chx_value
sf_asinh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(asinh(x)));
}

static struct chx_value
sf_atan(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(atan(x)));
}

static struct chx_value
sf_atan2(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x, y;
	if (unpack(c, args, "##", &x, &y) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(atan2(x, y)));
}

static struct chx_value
sf_atanh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < -1.0 || x > 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}
	if (x == 1.0 || x == -1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "range error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(atanh(x)));
}

static struct chx_value
sf_cbrt(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(cbrt(x)));
}

static struct chx_value
sf_ceil(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_int(ceil(x)));
}

static struct chx_value
sf_cos(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(cos(x)));
}

static struct chx_value
sf_cosh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	/* TODO deal with ERANGE? */

	return bt_wrap(c, cheax_double(cosh(x)));
}

static struct chx_value
sf_erf(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x != 0.0 && !isnormal(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "floating point underflow");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(erf(x)));
}

static struct chx_value
sf_exp(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	/* TODO deal with ERANGE? */

	return bt_wrap(c, cheax_double(exp(x)));
}

static struct chx_value
sf_expm1(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	/* TODO deal with ERANGE? */

	return bt_wrap(c, cheax_double(expm1(x)));
}

static struct chx_value
sf_floor(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_int(floor(x)));
}

static struct chx_value
sf_ldexp(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	chx_int e;
	if (unpack(c, args, "#i", &x, &e) < 0)
		return cheax_nil();

	/* TODO deal with ERANGE? */

	return bt_wrap(c, cheax_double(ldexp(x, e)));
}

static struct chx_value
sf_lgamma(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x <= 0.0 && (int)x == x) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(lgamma(x)));
}

static struct chx_value
sf_log(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(log(x)));
}

static struct chx_value
sf_log10(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(log10(x)));
}

static struct chx_value
sf_log1p(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < 1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}
	if (x == -1.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(log1p(x)));
}

static struct chx_value
sf_log2(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(log2(x)));
}

static struct chx_value
sf_nextafter(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x, y;
	if (unpack(c, args, "##", &x, &y) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(nextafter(x, y)));
}

static struct chx_value
sf_pow(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x, y;
	if (unpack(c, args, "##", &x, &y) < 0)
		return cheax_nil();

	if (x < 0.0 && isfinite(y) && (int)y != y) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}
	if (x == 0.0 && y < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(pow(x, y)));
}

static struct chx_value
sf_round(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_int(round(x)));
}

static struct chx_value
sf_sin(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(sin(x)));
}

static struct chx_value
sf_sinh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(sinh(x)));
}

static struct chx_value
sf_sqrt(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(sqrt(x)));
}

static struct chx_value
sf_tan(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (isinf(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(tan(x)));
}

static struct chx_value
sf_tanh(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_double(tanh(x)));
}

static struct chx_value
sf_tgamma(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	if (x < 0.0 && isinf(x)) {
		cheax_throwf(c, CHEAX_EVALUE, "domain error");
		return bt_wrap(c, cheax_nil());
	}
	if (x == 0.0) {
		cheax_throwf(c, CHEAX_EVALUE, "pole error");
		return bt_wrap(c, cheax_nil());
	}

	return bt_wrap(c, cheax_double(tgamma(x)));
}

static struct chx_value
sf_trunc(CHEAX *c, struct chx_list *args, void *info)
{
	chx_double x;
	if (unpack(c, args, "#", &x) < 0)
		return cheax_nil();

	return bt_wrap(c, cheax_int(trunc(x)));
}

void
export_math_bltns(CHEAX *c)
{
	cheax_def_special_form(c, "acos",      sf_acos,      NULL);
	cheax_def_special_form(c, "acosh",     sf_acosh,     NULL);
	cheax_def_special_form(c, "asin",      sf_asin,      NULL);
	cheax_def_special_form(c, "asinh",     sf_asinh,     NULL);
	cheax_def_special_form(c, "atan",      sf_atan,      NULL);
	cheax_def_special_form(c, "atan2",     sf_atan2,     NULL);
	cheax_def_special_form(c, "atanh",     sf_atanh,     NULL);
	cheax_def_special_form(c, "cbrt",      sf_cbrt,      NULL);
	cheax_def_special_form(c, "ceil",      sf_ceil,      NULL);
	cheax_def_special_form(c, "cos",       sf_cos,       NULL);
	cheax_def_special_form(c, "cosh",      sf_cosh,      NULL);
	cheax_def_special_form(c, "erf",       sf_erf,       NULL);
	cheax_def_special_form(c, "exp",       sf_exp,       NULL);
	cheax_def_special_form(c, "expm1",     sf_expm1,     NULL);
	cheax_def_special_form(c, "floor",     sf_floor,     NULL);
	cheax_def_special_form(c, "ldexp",     sf_ldexp,     NULL);
	cheax_def_special_form(c, "lgamma",    sf_lgamma,    NULL);
	cheax_def_special_form(c, "log",       sf_log,       NULL);
	cheax_def_special_form(c, "log10",     sf_log10,     NULL);
	cheax_def_special_form(c, "log1p",     sf_log1p,     NULL);
	cheax_def_special_form(c, "log2",      sf_log2,      NULL);
	cheax_def_special_form(c, "nextafter", sf_nextafter, NULL);
	cheax_def_special_form(c, "pow",       sf_pow,       NULL);
	cheax_def_special_form(c, "round",     sf_round,     NULL);
	cheax_def_special_form(c, "sin",       sf_sin,       NULL);
	cheax_def_special_form(c, "sinh",      sf_sinh,      NULL);
	cheax_def_special_form(c, "sqrt",      sf_sqrt,      NULL);
	cheax_def_special_form(c, "tan",       sf_tan,       NULL);
	cheax_def_special_form(c, "tanh",      sf_tanh,      NULL);
	cheax_def_special_form(c, "tgamma",    sf_tgamma,    NULL);
	cheax_def_special_form(c, "trunc",     sf_trunc,     NULL);

	cheax_def(c, "pi",   cheax_double(M_PI),      CHEAX_READONLY);
	cheax_def(c, "nan",  cheax_double(+NAN),      CHEAX_READONLY);
	cheax_def(c, "-nan", cheax_double(-NAN),      CHEAX_READONLY);
	cheax_def(c, "inf",  cheax_double(+INFINITY), CHEAX_READONLY);
	cheax_def(c, "-inf", cheax_double(-INFINITY), CHEAX_READONLY);
}
