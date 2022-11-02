/* Copyright (c) 2023, Antonie Blom
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
#include <string.h>

#include "cinfo.h"
#include "core.h"
#include "err.h"
#include "format.h"
#include "print.h"
#include "strm.h"
#include "unpack.h"

static int
read_int(CHEAX *c, const char *desc, struct scnr *fmt, int *out)
{
	for (*out = 0; c_isdigit(fmt->ch); scnr_adv(fmt)) {
		if (*out > INT_MAX / 10 || (*out * 10) > INT_MAX - (fmt->ch - '0')) {
			cheax_throwf(c, CHEAX_EVALUE, "%s too big", desc);
			return -1;
		}
		*out = (*out * 10) + (fmt->ch - '0');
	}

	return 0;
}

static int
type_is_num(int type)
{
	return type == CHEAX_INT || type == CHEAX_DOUBLE;
}

/* format (or field) specifier */
struct fspec {
	int index;          /* field index (default -1) */

	/* conversion specifier */
	enum {
		CONV_NONE,
		CONV_S,     /* for {!s} */
		CONV_R,     /* for {!r} */
	} conv;

	enum {
		ALN_NONE,
		ALN_LEFT,   /* for {:<} */
		ALN_CENTER, /* for {:^} */
		ALN_RIGHT,  /* for {:>} */
	} aln;

	char pad_char;      /* character to pad with: ' ' (default) or '0' */
	int field_width;    /* width (number of bytes) to pad to (default 0) */
	int precision;      /* floating point precision spec (default -1) */
	char misc_spec;     /* xXobcd for int, eEfFgG for double (default 0) */
};

static int
read_fspec(CHEAX *c, struct scnr *fmt, struct fspec *sp)
{
	sp->index = -1;
	sp->conv = CONV_NONE;
	sp->aln = ALN_NONE;
	sp->pad_char = ' ';
	sp->field_width = 0;
	sp->precision = -1;
	sp->misc_spec = '\0';

	if (c_isdigit(fmt->ch) && read_int(c, "index", fmt, &sp->index) < 0)
		return -1;

	if (fmt->ch == '!') {
		scnr_adv(fmt);
		if (fmt->ch == 's' || fmt->ch == 'r') {
			sp->conv = (scnr_adv(fmt) == 's') ? CONV_S : CONV_R;
		} else {
			cheax_throwf(c, CHEAX_EVALUE, "expected `s' or `r' after `!'");
			return -1;
		}
	}

	if (fmt->ch == ':') {
		scnr_adv(fmt);
		if (fmt->ch == '<')
			sp->aln = ALN_LEFT;
		else if (fmt->ch == '^')
			sp->aln = ALN_CENTER;
		else if (fmt->ch == '>')
			sp->aln = ALN_RIGHT;

		if (sp->aln != ALN_NONE)
			scnr_adv(fmt);

		if (fmt->ch == ' ' || fmt->ch == '0')
			sp->pad_char = scnr_adv(fmt);

		if (c_isdigit(fmt->ch) && read_int(c, "field width", fmt, &sp->field_width) < 0)
			return -1;

		if (fmt->ch == '.') {
			scnr_adv(fmt);
			if (!c_isdigit(fmt->ch)) {
				cheax_throwf(c, CHEAX_EVALUE, "expected precision specifier");
				return -1;
			}

			if (read_int(c, "precision", fmt, &sp->precision) < 0)
				return -1;
		}

		if (fmt->ch != '\0' && strchr("xXobcdeEfFgG", fmt->ch) != NULL)
			sp->misc_spec = scnr_adv(fmt);
	}

	if (scnr_adv(fmt) != '}') {
		cheax_throwf(c, CHEAX_EVALUE, "expected `}'");
		return -1;
	}

	return 0;
}

static int
check_spec(CHEAX *c, struct fspec *sp, struct chx_value arg, int *etp)
{
	int eff_type = arg.type;
	if (sp->conv != CONV_NONE)
		eff_type = CHEAX_STRING;

	*etp = eff_type;

	bool can_int = true, can_double = true, can_other = true;

	if (sp->precision != -1)
		can_int = can_other = false;

	if (sp->misc_spec != '\0') {
		if (strchr("xXobcd", sp->misc_spec) != NULL)
			can_double = can_other = false;
		else if (strchr("eEfFgG", sp->misc_spec) != NULL)
			can_int = can_other = false;
	}

	const char *invalid_for = NULL;

	if (eff_type == CHEAX_INT) {
		if (!can_int)
			invalid_for = "integer";
	} else if (eff_type == CHEAX_DOUBLE) {
		if (!can_double)
			invalid_for = "double";
	} else if (!can_other) {
		invalid_for = "given value";
	}

	if (invalid_for != NULL) {
		cheax_throwf(c, CHEAX_EVALUE, "invalid specifiers for %s", invalid_for);
		return -1;
	}

	return 0;
}

static int
format_num(CHEAX *c, struct ostrm *strm, struct fspec *sp, struct chx_value arg)
{
	/* wrapper function must deal with padding for anything but
	 * ALN_RIGHT */
	int field_width = (sp->aln == ALN_RIGHT) ? sp->field_width : 0;

	char ms = sp->misc_spec;
	char fmt_buf[32];

	switch (arg.type) {
	case CHEAX_INT:
		if (sp->misc_spec == 'c') {
			if (arg.data.as_int < 0 || arg.data.as_int >= 256) {
				cheax_throwf(c, CHEAX_EVALUE, "invalid character %d", arg.data.as_int);
				return -1;
			}

			ostrm_putc(strm, arg.data.as_int);
		} else {
			ostrm_printi(strm, arg.data.as_int, sp->pad_char, field_width, ms);
		}

		return 0;

	case CHEAX_DOUBLE:
		if (ms == '\0')
			ms = 'g';

		if (sp->pad_char == ' ')
			snprintf(fmt_buf, sizeof(fmt_buf), "%%*.*%c", ms);
		else
			snprintf(fmt_buf, sizeof(fmt_buf), "%%%c*.*%c", sp->pad_char, ms);

		ostrm_printf(strm, fmt_buf, field_width, sp->precision, arg.data.as_double);
		return 0;

	default:
		cheax_throwf(c, CHEAX_EEVAL, "internal error");
		return -1;
	}
}

static int
show_env(CHEAX *c, struct ostrm *strm, struct chx_env *env, const char *func_desc)
{
	struct chx_value showf = cheax_get_from(c, env, func_desc);
	cheax_ft(c, pad);

	if (showf.type != CHEAX_FUNC && showf.type != CHEAX_EXT_FUNC) {
		cheax_throwf(c, CHEAX_ETYPE, "env %s symbol must be function", func_desc);
		return -1;
	}

	struct chx_value ret = cheax_eval(c, cheax_list(c, showf, NULL));
	cheax_ft(c, pad);

	if (ret.type != CHEAX_STRING) {
		cheax_throwf(c, CHEAX_ETYPE, "env (%s) function must return string", func_desc);
		return -1;
	}

	for (size_t i = 0; i < ret.data.as_string->len; ++i)
		ostrm_putc(strm, ret.data.as_string->value[i]);

	return 0;
pad:
	return -1;
}

/* Does not do padding or alignment. */
static int
format_noalign(CHEAX *c, struct ostrm *strm, struct fspec *sp, struct chx_value arg, int eff_type)
{
	struct fspec nsp = *sp;
	nsp.field_width = 0;
	nsp.aln = ALN_LEFT;

	if (type_is_num(eff_type)) {
		if (format_num(c, strm, &nsp, arg) < 0)
			return -1;
	} else {
		if (arg.type == CHEAX_STRING && nsp.conv != CONV_R) {
			ostrm_write(strm, arg.data.as_string->value, arg.data.as_string->len);
		} else if (arg.type == CHEAX_ENV && nsp.conv != CONV_NONE) {
			const char *func = (nsp.conv == CONV_S) ? "show" : "repr";
			if (show_env(c, strm, arg.data.as_env, func) < 0)
				return -1;
		} else {
			ostrm_show(c, strm, arg);
		}
	}

	return 0;
}

/* TODO this should probably count graphemes rather than bytes */
static void
do_padding(struct ostrm *strm, int padding, char pad_char)
{
	for (int i = 0; i < padding; ++i)
		ostrm_putc(strm, pad_char);

}

static int
format_fspec(CHEAX *c, struct ostrm *strm, struct fspec *sp, struct chx_value arg)
{
	int eff_type;

	if (check_spec(c, sp, arg, &eff_type) < 0)
		return -1;

	if (sp->aln == ALN_NONE)
		sp->aln = type_is_num(eff_type) ? ALN_RIGHT : ALN_LEFT;

	/* This is primarily so we can catch any ENOMEM early if
	 * field_width is too big, instead of letting the mem use slowly
	 * creep up as we add more and more padding. */
	size_t ufield_width = sp->field_width;
	if (ostrm_expand(strm, ufield_width) < 0)
		return -1;

	/* Count output bytes to gauge how much padding is necessary. */
	struct costrm cs;
	if (ufield_width > 0) {
		costrm_init(&cs, strm);
		strm = &cs.strm;
	}

	if (sp->aln != ALN_CENTER && type_is_num(eff_type)) {
		if (format_num(c, strm, sp, arg) < 0)
			return -1;
	} else if (sp->aln == ALN_LEFT) {
		if (format_noalign(c, strm, sp, arg, eff_type) < 0)
			return -1;
	} else if (arg.type == CHEAX_STRING && sp->conv != CONV_R) {
		size_t prepad = (size_t)sp->field_width;
		if (prepad >= arg.data.as_string->len)
			prepad -= arg.data.as_string->len;
		else
			prepad = 0;

		if (sp->aln == ALN_CENTER)
			prepad /= 2;

		do_padding(strm, (int)prepad, sp->pad_char);
		if (ostrm_write(strm, arg.data.as_string->value, arg.data.as_string->len) < 0)
			return -1;
	} else {
		/* This should be seen as the default way to format
		 * and align, the cases above are just optimizations. */

		struct sostrm temp_ss;
		sostrm_init(&temp_ss, c);

		if (format_noalign(c, &temp_ss.strm, sp, arg, eff_type) < 0) {
			cheax_free(c, temp_ss.buf);
			return -1;
		}

		int prepad = sp->field_width - temp_ss.idx;
		if (prepad < 0)
			prepad = 0;

		if (sp->aln == ALN_CENTER)
			prepad /= 2;

		do_padding(strm, prepad, sp->pad_char);
		ostrm_write(strm, temp_ss.buf, temp_ss.idx);

		cheax_free(c, temp_ss.buf);
	}

	if (ufield_width > 0 && ufield_width > cs.written)
		do_padding(strm, ufield_width - cs.written, sp->pad_char);

	return 0;
}

static int
format(CHEAX *c, struct ostrm *strm, struct chx_string *fmt_str, struct chx_list *args)
{
	struct chx_value *arg_array;
	size_t arg_count;
	if (0 != cheax_list_to_array(c, args, &arg_array, &arg_count))
		return -1;

	struct chx_value args_val;
	args_val.type = CHEAX_LIST;
	args_val.data.as_list = args;
	chx_ref args_ref = cheax_ref(c, args_val);

	enum { UNSPECIFIED, AUTO_IDX, MAN_IDX } indexing = UNSPECIFIED;
	size_t auto_idx = 0;

	struct sistrm is;
	sistrm_initn(&is, fmt_str->value, fmt_str->len);

	struct scnr fmt;
	scnr_init(&fmt, &is.strm, 0, NULL, 1, 0);

	for (;;) {
		if (fmt.ch == EOF)
			break;

		if (fmt.ch == '}' && (scnr_adv(&fmt), fmt.ch) != '}') {
			cheax_throwf(c, CHEAX_EVALUE, "encountered single `}' in format string");
			break;
		}

		if (fmt.ch != '{' || (scnr_adv(&fmt), fmt.ch) == '{') {
			/* most likely condition: 'regular' character or
			 * double "{{" */
			ostrm_putc(strm, scnr_adv(&fmt));
			continue;
		}

		/* we're dealing with a field specifier */

		struct fspec spec;
		if (read_fspec(c, &fmt, &spec) < 0)
			break;

		if (indexing == AUTO_IDX && spec.index != -1) {
			cheax_throwf(c, CHEAX_EVALUE, "cannot switch from automatic indexing to manual indexing");
			break;
		}

		if (indexing == MAN_IDX && spec.index == -1) {
			cheax_throwf(c, CHEAX_EVALUE, "expected index (cannot switch from manual indexing to automatic indexing)");
			break;
		}

		if (indexing == UNSPECIFIED)
			indexing = (spec.index == -1) ? AUTO_IDX : MAN_IDX;

		size_t idx = (indexing == AUTO_IDX) ? auto_idx++ : (size_t)spec.index;
		if (idx >= arg_count) {
			cheax_throwf(c, CHEAX_EINDEX, "too few arguments");
			break;
		}

		if (format_fspec(c, strm, &spec, arg_array[idx]) < 0)
			break;
	}

	cheax_unref(c, args_val, args_ref);
	cheax_free(c, arg_array);
	return (cheax_errno(c) == CHEAX_ENOERR) ? 0 : -1;
}

struct chx_value
cheax_format(CHEAX *c, struct chx_string *fmt, struct chx_list *args)
{
	ASSERT_NOT_NULL("format", fmt, cheax_nil());

	struct sostrm ss;
	sostrm_init(&ss, c);
	ss.cap = fmt->len;
	ss.buf = cheax_malloc(c, ss.cap);

	struct chx_value res = cheax_nil();

	if (0 == format(c, &ss.strm, fmt, args))
		res = cheax_nstring(c, ss.buf, ss.idx);

	cheax_free(c, ss.buf);
	return res;
}

static struct chx_value
bltn_format(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *fmt;
	struct chx_list *lst;
	return (0 == unpack(c, args, "S_*", &fmt, &lst))
	     ? bt_wrap(c, cheax_format(c, fmt, lst))
	     : cheax_nil();
}

static struct chx_value
bltn_putf_to(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	struct chx_string *fmt;
	struct chx_list *lst;
	if (unpack(c, args, "FS_*", &f, &fmt, &lst) < 0)
		return cheax_nil();

	struct fostrm fs;
	fostrm_init(&fs, f, c);

	format(c, &fs.strm, fmt, lst);
	return bt_wrap(c, cheax_nil());
}


void
export_format_bltns(CHEAX *c)
{
	cheax_defun(c, "format",  bltn_format,  NULL);
	cheax_defun(c, "putf-to", bltn_putf_to, NULL);
}
