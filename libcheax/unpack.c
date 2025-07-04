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

#include <stdarg.h>
#include <stdlib.h>

#include "core.h"
#include "err.h"
#include "unpack.h"

struct unpack_field {
	int t;  /* type code */
	bool e; /* evaluate? */
};

enum {
	ANY_TYPE  = -1,
	FILE_TYPE = -2,
	NUM_TYPE  = -3,
	NIL_TYPE  = -4,
};

static const struct unpack_field unpack_fields[] = {
	[' '] = { NIL_TYPE,        true  },
	['#'] = { NUM_TYPE,        false },
	['-'] = { NIL_TYPE,        false },
	['.'] = { ANY_TYPE,        true  },
	['B'] = { CHEAX_BOOL,      false },
	['C'] = { CHEAX_LIST,      false }, /* C: Cons */
	['D'] = { CHEAX_DOUBLE,    false },
	['E'] = { CHEAX_ENV,       false },
	['F'] = { FILE_TYPE,       false },
	['I'] = { CHEAX_INT,       false },
	['L'] = { CHEAX_FUNC,      false }, /* L: Lambda */
	['N'] = { CHEAX_ID,        false }, /* N: Name */
	['P'] = { CHEAX_EXT_FUNC,  false }, /* P: Procedure */
	['S'] = { CHEAX_STRING,    false },
	['X'] = { CHEAX_ERRORCODE, false },
	['_'] = { ANY_TYPE,        false },
	['b'] = { CHEAX_BOOL,      true  },
	['c'] = { CHEAX_LIST,      true  },
	['d'] = { CHEAX_DOUBLE,    true  },
	['e'] = { CHEAX_ENV,       true  },
	['f'] = { FILE_TYPE,       true  },
	['i'] = { CHEAX_INT,       true  },
	['l'] = { CHEAX_FUNC,      true  },
	['n'] = { CHEAX_ID,        true  },
	['p'] = { CHEAX_EXT_FUNC,  true  },
	['s'] = { CHEAX_STRING,    true  },
	['x'] = { CHEAX_ERRORCODE, true  },
};

/* storage options */
enum st_opts {
	STORE_DATA,
	STORE_DEEP_DATA,
	STORE_VALUE,
};

static int
unpack_once(CHEAX *c,
            struct chx_list **args,
            const char *ufs_i,
            const char *ufs_f,
	    enum st_opts *st_opts,
	    int *fty_out,
	    struct chx_value *out)
{
	bool has_evald = false;
	int res = 0, fty;

	struct chx_list *arg_cons = *args;
	if (arg_cons == NULL)
		return -CHEAX_EMATCH;

	struct chx_value v = arg_cons->value;

	for (char f; f = *ufs_i, ufs_i != ufs_f; ++ufs_i) {
		struct unpack_field uf = unpack_fields[(int)f];
		if (!has_evald && uf.e) {
			v = cheax_eval(c, v);
			cheax_ft(c, pad);
			has_evald = true;
		}

		fty = uf.t;

		if (v.type == fty) {
			*out = v;
			goto done;
		}

		if (fty >= 0)
			continue;

		if (fty == ANY_TYPE) {
			*st_opts = STORE_VALUE;
			*out = v;
			goto done;
		}
		if (fty == FILE_TYPE && v.type == c->fhandle_type) {
			*out = v;
			goto done;
		}
		if (fty == NUM_TYPE && (v.type == CHEAX_INT || v.type == CHEAX_DOUBLE)) {
			*out = v;
			goto done;
		}
		if (fty == NIL_TYPE && cheax_is_nil(v)) {
			*out = v;
			goto done;
		}
	}

	res = -CHEAX_ETYPE;
done:
	if (res == 0) {
		*args = arg_cons->next;
		if (fty_out != NULL)
			*fty_out = fty;
	}
	return res;
pad:
	return -CHEAX_EEVAL;
}

struct valueref {
	struct chx_value v;
	chx_ref ref;
};

static int
store_arg(CHEAX *c,
          int *argc,
          struct valueref *argv_out,
          enum st_opts st_opts,
          int fty,
          struct chx_value v,
          va_list ap)
{
	int i = (*argc)++;
	argv_out[i].v = v;
	argv_out[i].ref = cheax_ref(c, v);

	switch (st_opts) {
	case STORE_DEEP_DATA:
		switch (fty) {
		case CHEAX_ID:
			*va_arg(ap, const char **) = v.as_id->value;
			return 0;
		}

		return -1;

	case STORE_DATA:
		switch (fty) {
		case CHEAX_INT:
		case CHEAX_ERRORCODE:
			*va_arg(ap, chx_int *) = v.as_int;
			return 0;
		case CHEAX_DOUBLE:
			*va_arg(ap, chx_double *) = v.as_double;
			return 0;
		case CHEAX_BOOL:
			*va_arg(ap, bool *) = v.as_int;
			return 0;
		case NUM_TYPE:
			return cheax_try_vtod_(v, va_arg(ap, chx_double *)) ? 0 : -1;
		default:
			*va_arg(ap, void **) = v.user_ptr;
			return 0;
		}

		return -1;

	case STORE_VALUE:
		*va_arg(ap, struct chx_value *) = v;
		break;
	}

	return 0;
}

static int
unpack_arg(CHEAX *c,
	   int *argc,
	   struct valueref *argv_out,
	   struct chx_list **args,
           const char *ufs_i,
           const char *ufs_f,
	   enum st_opts st_opts,
           char mod,
           va_list ap)
{
	struct chx_list *lst = NULL, **nxt = &lst;
	struct chx_value v = CHEAX_NIL;
	int res, fty;

	bool needs_one = false;

	switch (mod) {
	case '!':
		st_opts = STORE_DEEP_DATA;
		break;

	case '?':
		res = unpack_once(c, args, ufs_i, ufs_f, &st_opts, &fty, &v);
		if (res == -CHEAX_EEVAL)
			return -CHEAX_EEVAL;
		return store_arg(c, argc, argv_out, STORE_VALUE, fty, v, ap);

	case '+':
		needs_one = true;
		/* fall through */
	case '*':
		/* special case optimisation */
		if (ufs_f - ufs_i == 1 && *ufs_i == '_') {
			*nxt = *args;
			*args = NULL;
			res = 0;
		} else {
			for (;;) {
				chx_ref lst_ref = cheax_ref_ptr(c, lst);
				res = unpack_once(c, args, ufs_i, ufs_f, &st_opts, &fty, &v);
				cheax_unref_ptr(c, lst, lst_ref);
				if (res != 0)
					break;

				*nxt = cheax_list(c, v, NULL).as_list;
				if (*nxt == NULL) {
					res = -CHEAX_EEVAL;
					break;
				}
				nxt = &(*nxt)->next;
			}
		}

		if (needs_one && lst == NULL)
			return -CHEAX_EMATCH;

		return (res == -CHEAX_EEVAL)
		     ? -CHEAX_EEVAL
		     : store_arg(c, argc, argv_out, STORE_DATA, CHEAX_LIST, cheax_list_value(lst), ap);
	}

	res = unpack_once(c, args, ufs_i, ufs_f, &st_opts, &fty, &v);
	if (res < 0)
		return res;
	return store_arg(c, argc, argv_out, st_opts, fty, v, ap);
}

int
cheax_unpack_(CHEAX *c, struct chx_list *args, const char *fmt, ...)
{
	int argc = 0, res = 0;
	struct valueref argv[16];
	enum st_opts st_opts;

	va_list ap;
	va_start(ap, fmt);

	while (*fmt != '\0') {
		st_opts = STORE_DATA;

		const char *ufs_i, *ufs_f;
		if (*fmt == '[') {
			ufs_i = ++fmt;
			while (*++fmt != ']')
				;
			ufs_f = fmt++;

			st_opts = STORE_VALUE;
		} else {
			ufs_i = fmt;
			ufs_f = ++fmt;
		}

		char mod = *fmt;

		switch (mod) {
		case '!':
		case '?':
		case '+':
		case '*':
			++fmt;
			break;
		default:
			mod = 0;
		}

		res = unpack_arg(c, &argc, argv, &args, ufs_i, ufs_f, st_opts, mod, ap);
		if (res < 0)
			break;
	}

	va_end(ap);

	for (int i = 0; i < argc; ++i)
		cheax_unref(c, argv[i].v, argv[i].ref);

	if (res == 0) {
		if (args != NULL) {
			cheax_throwf(c, CHEAX_EMATCH, "too many arguments");
			cheax_add_bt(c);
			res = -CHEAX_EMATCH;
		}
	} else if (res == -CHEAX_EMATCH) {
		cheax_throwf(c, CHEAX_EMATCH, "too few arguments");
		cheax_add_bt(c);
	} else if (res == -CHEAX_ETYPE) {
		cheax_throwf(c, CHEAX_ETYPE, "invalid argument type");
		cheax_add_bt(c);
	} else if (res == -CHEAX_EAPI) {
		cheax_throwf(c, CHEAX_EAPI, "internal error");
		cheax_add_bt(c);
	}

	return res;
}

static struct chx_value pp_pan_value(CHEAX *c,
                                     struct chx_value value,
                                     const uint8_t **prog_p,
                                     const char **errors);

static size_t
pp_pan_len(const uint8_t *prog)
{
	size_t len1;
	switch (*prog & PP_INSTR_BITS) {
	case PP_NIL:
	case PP_LIT:
	case PP_EXPR:
		return 1;
	case PP_NODE:
		len1 = pp_pan_len(prog + 1);
		return 1 + len1 + pp_pan_len(prog + 1 + len1);
	case PP_SEQ:
	case PP_MAYBE:
		return 1 + pp_pan_len(prog + 1);
	}

	abort();
}

static void
pp_error(CHEAX *c, uint8_t op, const char **errors, const char *deflt)
{
	const char *msg = deflt;
	if ((op & PP_ERRMSG_BITS) != 0)
		msg = errors[((op & PP_ERRMSG_BITS) >> PP_ERRMSG_OFS) - 1];
	cheax_throwf(c, CHEAX_ESTATIC, "%s", msg);
	cheax_add_bt(c);
}

static struct chx_list *
pp_pan_list(CHEAX *c, struct chx_value value, const uint8_t **prog_p, const char **errors)
{
	struct chx_value head;
	struct chx_list *tail, *lst, *out, **nextp;
	uint8_t op = *(*prog_p)++;

	if (value.type != CHEAX_LIST) {
		pp_error(c, op, errors, "failed to match list");
		return NULL;
	}

	lst = value.as_list;

	switch (op & PP_INSTR_BITS) {
	case PP_NIL:
		if (lst != NULL)
			pp_error(c, op, errors, "failed to match nil");
		return NULL;

	case PP_NODE:
		if (lst == NULL) {
			pp_error(c, op, errors, "failed to match node");
			return NULL;
		}

		head = pp_pan_value(c, lst->value, prog_p, errors);
		cheax_ft(c, pad);

		chx_ref head_ref = cheax_ref(c, head);
		tail = pp_pan_list(c, cheax_list_value(lst->next), prog_p, errors);
		cheax_unref(c, head, head_ref);
		cheax_ft(c, pad);

		struct chx_list *res = cheax_list(c, head, tail).as_list;
		cheax_ft(c, pad);
		cheax_set_orig_form_(c, res, lst);
		cheax_ft(c, pad);
		return res;

	case PP_MAYBE:
		if (lst == NULL) {
			*prog_p += pp_pan_len(*prog_p);
			return NULL;
		}

		return pp_pan_list(c, value, prog_p, errors);

	case PP_SEQ:
		out = NULL;
		nextp = &out;

		for (; lst != NULL; lst = lst->next) {
			const uint8_t *prog_cpy = *prog_p;

			chx_ref ref = cheax_ref_ptr(c, out);
			head = pp_pan_value(c, lst->value, &prog_cpy, errors);
			cheax_unref_ptr(c, out, ref);

			cheax_ft(c, pad);

			struct chx_list *new = cheax_list(c, head, NULL).as_list;
			cheax_ft(c, pad);
			cheax_set_orig_form_(c, new, lst);
			cheax_ft(c, pad);

			*nextp = new;
			nextp = &new->next;
		}

		*prog_p += pp_pan_len(*prog_p);
		return out;

	default:
		cheax_throwf(c, CHEAX_ESTATIC, "unsupported list pattern operator");
		cheax_add_bt(c);
		return NULL;
	}
pad:
	return NULL;
}

static struct chx_value
pp_pan_value(CHEAX *c, struct chx_value value, const uint8_t **prog_p, const char **errors)
{
	switch (*(*prog_p)++ & PP_INSTR_BITS) {
	case PP_LIT:
		return value;

	case PP_EXPR:
		return cheax_preproc(c, value);

	default:
		--*prog_p;
		return cheax_list_value(pp_pan_list(c, value, prog_p, errors));
	}
}

struct chx_value
cheax_preproc_pattern_(CHEAX *c, struct chx_value input, const uint8_t *prog, const char **errors)
{
	/* TODO optimise in case preprocess didn't change syntax */
	return pp_pan_value(c, input, &prog, errors);
}
