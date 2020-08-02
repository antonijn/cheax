#ifndef CHEAX_H
#define CHEAX_H

#include <stdio.h>
#include <stdbool.h>

typedef struct cheax CHEAX;

enum cheax_type {
	CHEAX_NIL,
	CHEAX_ID,
	CHEAX_INT,
	CHEAX_DOUBLE,
	CHEAX_BOOL,       /* unused */
	CHEAX_LIST,
	CHEAX_FUNC,
	CHEAX_EXT_FUNC,
	CHEAX_QUOTE,
	CHEAX_STRING,
	CHEAX_USER_PTR,

	CHEAX_LAST_BASIC_TYPE = CHEAX_USER_PTR,
	CHEAX_TYPESTORE_BIAS,

	CHEAX_TYPECODE = CHEAX_TYPESTORE_BIAS + 0,
	CHEAX_ERRORCODE,
};

struct chx_value {
	int type;
};

struct chx_quote {
	struct chx_value base;
	struct chx_value *value;
};
struct chx_quote *cheax_quote(CHEAX *c, struct chx_value *value);

struct chx_int {
	struct chx_value base;
	int value;
};
struct chx_int *cheax_int(CHEAX *c, int value);

struct chx_double {
	struct chx_value base;
	double value;
};
struct chx_double *cheax_double(CHEAX *c, double value);

struct chx_user_ptr {
	struct chx_value base;
	void *value;
};
struct chx_user_ptr *cheax_user_ptr(CHEAX *c, void *value, int type);

struct chx_id {
	struct chx_value base;
	char *id;
};
struct chx_id *cheax_id(CHEAX *c, char *id);

struct chx_list {
	struct chx_value base;
	struct chx_value *value;
	struct chx_list *next;
};
struct chx_list *cheax_list(CHEAX *c, struct chx_value *car, struct chx_list *cdr);

typedef struct chx_value *(*chx_func_ptr)(CHEAX *c, struct chx_list *args);

struct chx_ext_func {
	struct chx_value base;
	chx_func_ptr perform;
	const char *name;
};
struct chx_ext_func *cheax_ext_func(CHEAX *c, chx_func_ptr perform, const char *name);

struct chx_func {
	struct chx_value base;
	struct chx_value *args;
	struct chx_list *body;
	bool eval_args; /* true for (\), false for (\\) */
	/* the context in which the lambda was declared */
	struct variable *locals_top;
};

struct chx_string {
	struct chx_value base;
	char *value;
	size_t len;
};
struct chx_string *cheax_string(CHEAX *c, char *value);
struct chx_string *cheax_nstring(CHEAX *c, char *value, size_t len);

enum {
	/* Read errors */
	CHEAX_EREAD     = 0x0001,
	CHEAX_EEOF      = 0x0002,
	CHEAX_ELEX      = 0x0003,
	/* Eval errors */
	CHEAX_EEVAL     = 0x0101,
	CHEAX_ENOSYM    = 0x0102,
	CHEAX_ESTACK    = 0x0103,
	CHEAX_ETYPE     = 0x0104,
	CHEAX_EMATCH    = 0x0105,
	CHEAX_ENIL      = 0x0106,
	CHEAX_EDIVZERO  = 0x0107,
	CHEAX_EREADONLY = 0x0108,
	CHEAX_EVALUE    = 0x0109,
	CHEAX_EOVERFLOW = 0x010A,
	/* API error */
	CHEAX_EAPI      = 0x0200,
};

enum chx_varflags {
	CHEAX_SYNCED     = 0x01, /* implied by cheax_sync_*() */
	CHEAX_READONLY   = 0x02,
	CHEAX_CONST      = 0x04, /* reserved */
	CHEAX_NODUMP     = 0x08,
};

enum chx_builtins {
	CHEAX_FILE_IO             = 0x0001,
	CHEAX_SET_MAX_STACK_DEPTH = 0x0002,

	CHEAX_ALL_BUILTINS        = 0xFFFF,
};

/*
 * Initializes a CHEAX environment.
 */
CHEAX *cheax_init(void);
/*
 * Cleans up a CHEAX environment.
 * Calling any function on 'c' after calling this function results in
 * undefined behavior.
 */
void cheax_destroy(CHEAX *c);

void cheax_defmacro(CHEAX *c, char *id, chx_func_ptr perform);
void cheax_var(CHEAX *c, char *id, struct chx_value *value, enum chx_varflags flags);
void cheax_set(CHEAX *c, char *id, struct chx_value *value);
struct chx_value *cheax_get(CHEAX *c, char *id);
bool cheax_match(CHEAX *c, struct chx_value *pan, struct chx_value *match);
bool cheax_equals(CHEAX *c, struct chx_value *l, struct chx_value *r);

int cheax_errno(CHEAX *c);
void cheax_perror(CHEAX *c, const char *s);
void cheax_clear_errno(CHEAX *c);

void cheax_load_extra_builtins(CHEAX *c, enum chx_builtins builtins);

/*
 * Synchronizes a variable in C with a name in the CHEAX environment.
 */
void cheax_sync_int(CHEAX *c, const char *name, int *var, enum chx_varflags flags);
void cheax_sync_float(CHEAX *c, const char *name, float *var, enum chx_varflags flags);
void cheax_sync_double(CHEAX *c, const char *name, double *var, enum chx_varflags flags);

int cheax_get_max_stack_depth(CHEAX *c);
void cheax_set_max_stack_depth(CHEAX *c, int max_stack_depth);

int cheax_get_type(struct chx_value *v);
int cheax_new_type(CHEAX *c, const char *name, int base_type);
int cheax_find_type(CHEAX *c, const char *name);
bool cheax_is_valid_type(CHEAX *c, int type);
bool cheax_is_basic_type(CHEAX *c, int type);
int cheax_resolve_type(CHEAX *c, int type);
void cheax_defprint(CHEAX *c, int type, chx_func_ptr print);
void cheax_defcast(CHEAX *c, int from, int to, chx_func_ptr cast);

struct chx_value *cheax_eval(CHEAX *c, struct chx_value *expr);
struct chx_value *cheax_read(CHEAX *c, FILE *f);
struct chx_value *cheax_readstr(CHEAX *c, const char *str);
int cheax_load_prelude(CHEAX *c);
void cheax_print(CHEAX *c, FILE *output, struct chx_value *expr);

void cheax_exec(CHEAX *c, FILE *f);

#define cheax_ft(c, pad) { if (cheax_errno(c) != 0) goto pad; }

#endif
