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

	CHEAX_USER_TYPE = 0x100,
};

struct chx_value {
	enum cheax_type type;
};

struct chx_quote {
	struct chx_value base;
	struct chx_value *value;
};

struct chx_int {
	struct chx_value base;
	int value;
};
struct chx_double {
	struct chx_value base;
	double value;
};
struct chx_ptr {
	struct chx_value base;
	void *ptr;
};

struct chx_id {
	struct chx_value base;
	char *id;
};

struct chx_list {
	struct chx_value base;
	struct chx_value *value;
	struct chx_list *next;
};

typedef struct chx_value *(*chx_funcptr)(CHEAX *c, struct chx_list *args);

struct chx_ext_func {
	struct chx_value base;
	chx_funcptr perform;
	const char *name;
};

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

struct chx_int *cheax_int(int value);
struct chx_double *cheax_double(double value);
struct chx_list *cheax_list(struct chx_value *car, struct chx_list *cdr);

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
	/* API error */
	CHEAX_EAPI      = 0x0200,
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

int cheax_errno(CHEAX *c);
void cheax_perror(CHEAX *c, const char *s);
void cheax_clear_errno(CHEAX *c);

enum cheax_builtin {
	CHEAX_FILE_IO             = 0x0001,
	CHEAX_SET_MAX_STACK_DEPTH = 0x0002,

	CHEAX_ALL_BUILTINS        = 0xFFFF,
};

void cheax_load_extra_builtins(CHEAX *c, enum cheax_builtin builtins);

enum cheax_varflags {
	CHEAX_SYNCED     = 0x01, /* implied by cheax_sync_*() */
	CHEAX_READONLY   = 0x02,
	CHEAX_CONST      = 0x04, /* reserved */
	CHEAX_NODUMP     = 0x08,
};

/*
 * Synchronizes a variable in C with a name in the CHEAX environment.
 */
void cheax_sync_int(CHEAX *c, const char *name, int *var, enum cheax_varflags flags);
void cheax_sync_float(CHEAX *c, const char *name, float *var, enum cheax_varflags flags);
void cheax_sync_double(CHEAX *c, const char *name, double *var, enum cheax_varflags flags);

void cheax_defmacro(CHEAX *c, const char *name, chx_funcptr fun);

void cheax_decl_user_data(CHEAX *c, const char *name, void *ptr, int user_type);

int cheax_get_max_stack_depth(CHEAX *c);
void cheax_set_max_stack_depth(CHEAX *c, int max_stack_depth);

int cheax_get_type(struct chx_value *v);
int cheax_new_user_type(CHEAX *c);
int cheax_is_user_type(int type);

struct chx_value *cheax_eval(CHEAX *c, struct chx_value *expr);
struct chx_value *cheax_read(CHEAX *c, FILE *f);
struct chx_value *cheax_readstr(CHEAX *c, const char *str);
int cheax_load_prelude(CHEAX *c);
void cheax_print(FILE *output, struct chx_value *expr);

void cheax_exec(CHEAX *c, FILE *f);

#endif
