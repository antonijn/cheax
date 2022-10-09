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

#include <errno.h>
#include <string.h>

#include "core.h"
#include "err.h"
#include "feat.h"
#include "io.h"
#include "setup.h"
#include "strm.h"
#include "unpack.h"

static bool
mode_valid(const char *mode)
{
	if (mode[0] != 'r' && mode[0] != 'w' && mode[0] != 'a')
		return false;

	bool bin = false, plus = false;
	for (int i = 1; mode[i]; ++i) {
		if (mode[i] == '+') {
			if (!plus) {
				plus = true;
				continue;
			}
		} else if (mode[i] == 'b') {
			if (!bin) {
				bin = true;
				continue;
			}
		}

		return false;
	}

	return true;
}

static void
throw_io_error(CHEAX *c)
{
	const char *msg;

	switch (errno) {
#ifdef HAVE_EACCES
	case EACCES:
		msg = "permission denied";
		break;
#endif
#ifdef HAVE_EBADF
	case EBADF:
		msg = "bad file descriptor";
		break;
#endif
#ifdef HAVE_EBUSY
	case EBUSY:
		msg = "device or resource busy";
		break;
#endif
#ifdef HAVE_EDQUOT
	case EDQUOT:
		msg = "disk quota exceeded";
		break;
#endif
#ifdef HAVE_EEXIST
	case EEXIST:
		msg = "file exists";
		break;
#endif
#ifdef HAVE_EFAULT
	case EFAULT:
		msg = "bad address";
		break;
#endif
#ifdef HAVE_EFBIG
	case EFBIG:
		msg = "file too large";
		break;
#endif
#ifdef HAVE_EINTR
	case EINTR:
		msg = "interrupted system call";
		break;
#endif
#ifdef HAVE_EINVAL
	case EINVAL:
		msg = "invalid argument";
		break;
#endif
#ifdef HAVE_EISDIR
	case EISDIR:
		msg = "is a directory";
		break;
#endif
#ifdef HAVE_ELOOP
	case ELOOP:
		msg = "too many levels of symbolic links";
		break;
#endif
#ifdef HAVE_EMFILE
	case EMFILE:
		msg = "too many open files";
		break;
#endif
#ifdef HAVE_ENAMETOOLONG
	case ENAMETOOLONG:
		msg = "file name too long";
		break;
#endif
#ifdef HAVE_ENFILE
	case ENFILE:
		msg = "too many open files in system";
		break;
#endif
#ifdef HAVE_ENODEV
	case ENODEV:
		msg = "no such device";
		break;
#endif
#ifdef HAVE_ENOENT
	case ENOENT:
		msg = "no such file or directory";
		break;
#endif
#ifdef HAVE_ENOMEM
	case ENOMEM:
		msg = "cannot allocate memory";
		break;
#endif
#ifdef HAVE_ENOSPC
	case ENOSPC:
		msg = "no space left on device";
		break;
#endif
#ifdef HAVE_ENOTDIR
	case ENOTDIR:
		msg = "not a directory";
		break;
#endif
#ifdef HAVE_ENXIO
	case ENXIO:
		msg = "no such device or address";
		break;
#endif
#ifdef HAVE_EOPNOTSUPP
	case EOPNOTSUPP:
		msg = "operation not supported";
		break;
#endif
#ifdef HAVE_EOVERFLOW
	case EOVERFLOW:
		msg = "value too large for defined data type";
		break;
#endif
#ifdef HAVE_EPERM
	case EPERM:
		msg = "operation not permitted";
		break;
#endif
#ifdef HAVE_EROFS
	case EROFS:
		msg = "read-only file system";
		break;
#endif
#ifdef HAVE_ETXTBSY
	case ETXTBSY:
		msg = "text file busy";
		break;
#endif
#ifdef HAVE_EWOULDBLOCK
	case EWOULDBLOCK:
		msg = "resource temporarily unavailable";
		break;
#endif
	default:
		msg = "internal error";
		break;
	}

	cheax_throwf(c, CHEAX_EIO, "%s", msg);
}

static struct chx_value
sf_fopen(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *fname_val, *mode_val;
	if (unpack(c, args, "ss", &fname_val, &mode_val) < 0)
		return cheax_nil();

	int prev_errno = errno;
	errno = 0;

	struct chx_value res = cheax_nil();
	char *fname = NULL, *mode = NULL;

	fname = cheax_malloc(c, fname_val->len + 1);
	cheax_ft(c, pad);
	memcpy(fname, fname_val->value, fname_val->len);
	fname[fname_val->len] = '\0';

	mode = cheax_malloc(c, mode_val->len + 1);
	cheax_ft(c, pad);
	memcpy(mode, mode_val->value, mode_val->len);
	mode[mode_val->len] = '\0';

	if (mode_valid(mode)) {
		FILE *f = fopen(fname, mode);
		if (errno == 0)
			res = cheax_user_ptr(c, f, c->fhandle_type);
		else
			throw_io_error(c);
	} else {
		cheax_throwf(c, CHEAX_EVALUE, "invalid mode string %s", mode);
	}

pad:
	if (fname != NULL)
		cheax_free(c, fname);
	if (mode != NULL)
		cheax_free(c, mode);
	errno = prev_errno;
	return bt_wrap(c, res);
}

static struct chx_value
sf_fclose(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	if (0 == unpack(c, args, "f", &f))
		fclose(f);
	return cheax_nil();
}

static struct chx_value
sf_eof(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	return (0 == unpack(c, args, "f", &f))
	     ? cheax_bool(feof(f))
	     : cheax_nil();
}

static struct chx_value
sf_read_from(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	return (0 == unpack(c, args, "f", &f))
	     ? bt_wrap(c, cheax_read(c, f))
	     : cheax_nil();
}

static struct chx_value
sf_read_string(CHEAX *c, struct chx_list *args, void *info)
{
	struct chx_string *s;
	if (unpack(c, args, "s", &s) < 0)
		return cheax_nil();

	char *cstr = cheax_strdup(s);
	if (cstr == NULL)
		return cheax_nil();
	struct chx_value res = cheax_readstr(c, cstr);
	free(cstr);
	return bt_wrap(c, res);
}

static struct chx_value
sf_print_to(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	struct chx_value v;
	if (0 == unpack(c, args, "f.", &f, &v))
		cheax_print(c, f, v);
	return cheax_nil();
}

static struct chx_value
sf_put_to(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	struct chx_string *s;
	if (0 == unpack(c, args, "fs", &f, &s))
		fwrite(s->value, 1, s->len, f);
	return cheax_nil();
}

static struct chx_value
sf_get_byte_from(CHEAX *c, struct chx_list *args, void *info)
{
	FILE *f;
	if (unpack(c, args, "f", &f) < 0)
		return cheax_nil();

	int ch = fgetc(f);
	return (ch == EOF) ? cheax_nil() : bt_wrap(c, cheax_int(ch));
}

static struct chx_value
sf_get_line_from(CHEAX *c, struct chx_list *args, void *info)
{
	/*
	 * This could all be implemented in the prelude, but for the
	 * sake of performance it's done here.
	 */
	FILE *f;
	if (unpack(c, args, "f", &f) < 0)
		return cheax_nil();

	struct chx_value res = cheax_nil();
	struct sostrm ss;
	sostrm_init(&ss, c);

	int ch;
	while ((ch = fgetc(f)) != EOF) {
		if (ostrm_putc(&ss.strm, ch) == -1)
			goto pad;

		if (ch == '\n')
			break;
	}

	res = cheax_nstring(c, ss.buf, ss.idx);
pad:
	cheax_free(c, ss.buf);
	return bt_wrap(c, res);
}

void
load_io_feature(CHEAX *c, int bits)
{
	if (has_flag(bits, FILE_IO)) {
		cheax_def_special_form(c, "fopen", sf_fopen, NULL);
		cheax_def_special_form(c, "fclose", sf_fclose, NULL);
	}

	if (has_flag(bits, EXPOSE_STDIN)) {
		cheax_def(c, "stdin",
		          cheax_user_ptr(c, stdin,  c->fhandle_type),
		          CHEAX_READONLY);
	}

	if (has_flag(bits, EXPOSE_STDOUT)) {
		cheax_def(c, "stdout",
		          cheax_user_ptr(c, stdout, c->fhandle_type),
		          CHEAX_READONLY);
	}

	if (has_flag(bits, EXPOSE_STDERR)) {
		cheax_def(c, "stderr",
		          cheax_user_ptr(c, stderr, c->fhandle_type),
		          CHEAX_READONLY);
	}
}

void
export_io_bltns(CHEAX *c)
{
	c->fhandle_type = cheax_new_type(c, "FileHandle", CHEAX_USER_PTR);

	cheax_def_special_form(c, "eof?",          sf_eof,           NULL);
	cheax_def_special_form(c, "read-from",     sf_read_from,     NULL);
	cheax_def_special_form(c, "read-string",   sf_read_string,   NULL);
	cheax_def_special_form(c, "print-to",      sf_print_to,      NULL);
	cheax_def_special_form(c, "put-to",        sf_put_to,        NULL);
	cheax_def_special_form(c, "get-byte-from", sf_get_byte_from, NULL);
	cheax_def_special_form(c, "get-line-from", sf_get_line_from, NULL);
}
