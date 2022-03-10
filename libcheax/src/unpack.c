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

#include <stdarg.h>
#include <stdlib.h>

#include "core.h"
#include "err.h"
#include "unpack.h"

struct unpack_field {
	char f; /* field specifier */
	int t;  /* type code */
	bool e; /* evaluate? */
};

enum {
	ANY_TYPE  = -1,
	FILE_TYPE = -2,
};

/* in ascii order according to field specifier */
static const struct unpack_field unpack_fields[] = {
	{ ' ', CHEAX_NIL,       true  },
	{ '-', CHEAX_NIL,       false },
	{ '.', ANY_TYPE,        true  },
	{ 'B', CHEAX_BOOL,      false },
	{ 'C', CHEAX_LIST,      false }, /* C: Cons */
	{ 'D', CHEAX_DOUBLE,    false },
	{ 'E', CHEAX_ENV,       false },
	{ 'F', FILE_TYPE,       false },
	{ 'I', CHEAX_INT,       false },
	{ 'L', CHEAX_FUNC,      false }, /* L: Lambda */
	{ 'M', CHEAX_MACRO,     false },
	{ 'N', CHEAX_ID,        false }, /* N: Name */
	{ 'P', CHEAX_EXT_FUNC,  false }, /* P: Procedure */
	{ 'S', CHEAX_STRING,    false },
	{ 'X', CHEAX_ERRORCODE, false },
	{ '_', ANY_TYPE,        false },
	{ 'b', CHEAX_BOOL,      true  },
	{ 'c', CHEAX_LIST,      true  },
	{ 'd', CHEAX_DOUBLE,    true  },
	{ 'e', CHEAX_ENV,       true  },
	{ 'f', FILE_TYPE,       true  },
	{ 'i', CHEAX_INT,       true  },
	{ 'l', CHEAX_FUNC,      true  },
	{ 'm', CHEAX_MACRO,     true  },
	{ 'n', CHEAX_ID,        true  },
	{ 'p', CHEAX_EXT_FUNC,  true  },
	{ 's', CHEAX_STRING,    true  },
	{ 'x', CHEAX_ERRORCODE, true  },
};

/* for use in bsearch() */
static int
ufcompar(const char *f, const struct unpack_field *uf)
{
	return (*f > uf->f) - (*f < uf->f);
}

static const struct unpack_field *
find_unpack_field(char f)
{
	return bsearch(&f, unpack_fields,
	               sizeof(unpack_fields) / sizeof(unpack_fields[0]), sizeof(unpack_fields[0]),
	               (int (*)(const void *, const void *))ufcompar);
}

static int
unpack_once(CHEAX *c,
            struct chx_list **args,
            const char *ufs_i,
            const char *ufs_f,
	    struct chx_value **out)
{
	bool has_evald = false;
	int res = 0;

	struct chx_list *arg_cons = *args;
	if (arg_cons == NULL)
		return -CHEAX_EMATCH;

	struct chx_value *v = arg_cons->value;

	for (char f; f = *ufs_i, ufs_i != ufs_f; ++ufs_i) {
		const struct unpack_field *uf = find_unpack_field(f);
		if (!has_evald && uf->e) {
			v = cheax_eval(c, v);
			cheax_ft(c, pad);
			has_evald = true;
		}

		if (cheax_type_of(v) == uf->t) {
			*out = v;
			goto done;
		}

		if (uf->t >= 0)
			continue;

		if (uf->t == ANY_TYPE) {
			*out = v;
			goto done;
		}
		if (uf->t == FILE_TYPE && cheax_type_of(v) == c->fhandle_type) {
			*out = v;
			goto done;
		}
	}

	res = -CHEAX_ETYPE;
done:
	if (res == 0)
		*args = arg_cons->next;
	return res;
pad:
	return -CHEAX_EEVAL;
}

static int
store_value(CHEAX *c, struct chx_value *v, va_list ap)
{
	switch (cheax_type_of(v)) {
	case CHEAX_INT:
	case CHEAX_ERRORCODE:
		*va_arg(ap, int *) = ((struct chx_int *)v)->value;
		return 0;
	case CHEAX_DOUBLE:
		*va_arg(ap, double *) = ((struct chx_double *)v)->value;
		return 0;
	case CHEAX_BOOL:
		*va_arg(ap, bool *) = ((struct chx_int *)v)->value;
		return 0;
	case CHEAX_ID:
		*va_arg(ap, const char **) = ((struct chx_id *)v)->id;
		return 0;
	}

	if (cheax_type_of(v) == c->fhandle_type) {
		*va_arg(ap, FILE **) = ((struct chx_user_ptr *)v)->value;
		return 0;
	}

	return -1;
}

struct valueref {
	struct chx_value *v;
	chx_ref ref;
};

static int
store_arg(CHEAX *c, int *argc, struct valueref *argv_out, struct chx_value *v, va_list ap)
{
	int i = (*argc)++;
	argv_out[i].v = *va_arg(ap, struct chx_value **) = v;
	argv_out[i].ref = cheax_ref(c, v);
	return 0;
}

static int
unpack_arg(CHEAX *c,
	   int *argc,
	   struct valueref *argv_out,
	   struct chx_list **args,
           const char *ufs_i,
           const char *ufs_f,
           char mod,
           va_list ap)
{
	struct chx_list *lst = NULL, **nxt = &lst;
	struct chx_value *v = NULL;
	int res;

	bool has_refd_list = false;
	chx_ref lst_ref;

	switch (mod) {
	case '!':
		if (ufs_f - ufs_i > 1)
			return -CHEAX_EAPI;

		res = unpack_once(c, args, ufs_i, ufs_f, &v);
		if (res == 0 && store_value(c, v, ap) < 0)
			return -CHEAX_EAPI;
		return res;

	case '?':
		res = unpack_once(c, args, ufs_i, ufs_f, &v);
		if (res == -CHEAX_EEVAL)
			return -CHEAX_EEVAL;
		return store_arg(c, argc, argv_out, v, ap);

	case '+':
		res = unpack_once(c, args, ufs_i, ufs_f, &v);
		if (res < 0)
			return res;

		lst = cheax_list(c, v, NULL);
		if (lst == NULL)
			return -CHEAX_EEVAL;
		lst_ref = cheax_ref(c, lst);
		has_refd_list = true;
		nxt = &lst->next;
	case '*':
		while ((res = unpack_once(c, args, ufs_i, ufs_f, &v)) == 0) {
			*nxt = cheax_list(c, v, NULL);
			if (*nxt == NULL) {
				res = -CHEAX_EEVAL;
				break;
			}

			nxt = &(*nxt)->next;

			if (!has_refd_list) {
				lst_ref = cheax_ref(c, lst);
				has_refd_list = true;
			}
		}

		if (has_refd_list)
			cheax_unref(c, lst, lst_ref);

		return (res == -CHEAX_EEVAL)
		     ? -CHEAX_EEVAL
		     : store_arg(c, argc, argv_out, &lst->base, ap);

	default:
		res = unpack_once(c, args, ufs_i, ufs_f, &v);
		if (res < 0)
			return res;
		return store_arg(c, argc, argv_out, v, ap);
	}
}

int
unpack(CHEAX *c, const char *fname, struct chx_list *args, const char *fmt, ...)
{
	int argc = 0, res = 0;
	struct valueref argv[16];

	va_list ap;
	va_start(ap, fmt);

	while (*fmt != '\0') {
		const char *ufs_i, *ufs_f;
		if (*fmt == '[') {
			ufs_i = ++fmt;
			while (*++fmt != ']')
				;
			ufs_f = fmt++;
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

		res = unpack_arg(c, &argc, argv, &args, ufs_i, ufs_f, mod, ap);
		if (res < 0)
			break;
	}

	va_end(ap);

	for (int i = 0; i < argc; ++i)
		cheax_unref(c, argv[i].v, argv[i].ref);

	if (res == 0) {
		if (args != NULL) {
			cry(c, fname, CHEAX_EMATCH, "too many arguments");
			return -1;
		}

		return 0;
	}

	if (res == -CHEAX_EMATCH) {
		cry(c, fname, CHEAX_EMATCH, "too few arguments");
		return -1;
	}

	if (res == -CHEAX_ETYPE) {
		cry(c, fname, CHEAX_ETYPE, "invalid argument type");
		return -1;
	}

	if (res == -CHEAX_EAPI) {
		cry(c, fname, CHEAX_EAPI, "internal error");
		return -1;
	}

	return -1; /* error msg already current */
}
